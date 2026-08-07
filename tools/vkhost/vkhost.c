/* vkhost: headless libretro Vulkan frontend for renderer validation.
 *
 * Loads a libretro core with a Vulkan hardware context on whatever Vulkan
 * device is present (lavapipe in CI), with VK_LAYER_KHRONOS_validation
 * active and every message printed. Runs content, optionally loads a
 * savestate, and dumps the frame the core hands back through set_image()
 * as a PPM (tonemapped naively when the image is fp16/10-bit).
 *
 * This exists because two Vulkan regressions in a row shipped compile-clean:
 * a missing vertex attribute and a spec-constant capacity overflow, both of
 * which the validation layer reports by name on the first draw. The rule it
 * enforces: renderer changes get executed, not just compiled.
 *
 * Usage: vkhost <core.so> <content> [savestate] [frames] [outdir]
 *   VKHOST_VARS: semicolon list of key=value core option overrides.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include "libretro.h"
#include "libretro_vulkan.h"

/* ---- tiny dynamic table of core option overrides ---- */
static char *var_keys[512]; static char *var_vals[512]; static int n_vars;
static void add_var(const char *k, const char *v)
{ if (n_vars < 512) { var_keys[n_vars] = strdup(k); var_vals[n_vars] = strdup(v); n_vars++; } }
static const char *find_var(const char *k)
{ int i; for (i = 0; i < n_vars; i++) if (!strcmp(var_keys[i], k)) return var_vals[i]; return NULL; }

/* ---- state ---- */
static void *core;
static struct retro_hw_render_callback hw_render;
static const struct retro_hw_render_context_negotiation_interface_vulkan *negotiation;
static struct retro_hw_render_interface_vulkan iface;
static struct retro_vulkan_context vkctx;
static VkInstance instance;
static VkPhysicalDevice gpu;
static VkDebugUtilsMessengerEXT messenger;
static int validation_errors, validation_warnings;
static const struct retro_vulkan_image *last_image;
static unsigned frame_valid;
static unsigned last_w, last_h;
static char sysdir[512] = "/tmp/vkhost_sys";
static char savedir[512] = "/tmp/vkhost_save";

/* ---- validation output ---- */
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
      VkDebugUtilsMessageSeverityFlagBitsEXT sev,
      VkDebugUtilsMessageTypeFlagsEXT types,
      const VkDebugUtilsMessengerCallbackDataEXT *data, void *ud)
{
   (void)types; (void)ud;
   if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
   { validation_errors++;   fprintf(stderr, "[VVL:ERROR] %s\n", data->pMessage); }
   else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
   { validation_warnings++; fprintf(stderr, "[VVL:WARN ] %s\n", data->pMessage); }
   return VK_FALSE;
}

/* ---- log passthrough ---- */
static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap; (void)level;
   va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* ---- hw render interface the core consumes ---- */
static void vk_set_image(void *handle, const struct retro_vulkan_image *image,
      uint32_t num_semaphores, const VkSemaphore *semaphores, uint32_t src_queue_family)
{ (void)handle; (void)num_semaphores; (void)semaphores; (void)src_queue_family;
   if (!last_image || last_image->create_info.image != image->create_info.image ||
       last_image->create_info.format != image->create_info.format)
      fprintf(stderr, "[vkhost] set_image img=%p fmt=%d layout=%d extent-hint=%ux%u\n",
              (void*)image->create_info.image, (int)image->create_info.format,
              (int)image->image_layout, last_w, last_h);
   last_image = image; }
static uint32_t vk_get_sync_index(void *handle) { (void)handle; return 0; }
static uint32_t vk_get_sync_index_mask(void *handle) { (void)handle; return 1; }
static void vk_wait_sync_index(void *handle) { (void)handle; }
static void vk_set_command_buffers(void *handle, uint32_t num, const VkCommandBuffer *cmd)
{ (void)handle; (void)num; (void)cmd; }
static void vk_lock_queue(void *handle) { (void)handle; }
static void vk_unlock_queue(void *handle) { (void)handle; }
static void vk_set_signal_semaphore(void *handle, VkSemaphore sem) { (void)handle; (void)sem; }

/* ---- environment ---- */
static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = sysdir; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = savedir; return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool *)data = true; return true;
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      {
         const struct retro_variable *v = (const struct retro_variable *)data;
         for (; v && v->key; v++)
         {
            /* value format: "Label; default|alt|..." */
            const char *semi = strchr(v->value, ';');
            if (semi && !find_var(v->key))
            {
               char buf[256]; const char *p = semi + 1; size_t n = 0;
               while (*p == ' ') p++;
               while (p[n] && p[n] != '|' && n < sizeof(buf) - 1) n++;
               memcpy(buf, p, n); buf[n] = 0;
               add_var(v->key, buf);
            }
         }
         return true;
      }
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      {
         const struct retro_core_options_v2 *o2 =
            (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL)
            ? ((const struct retro_core_options_v2_intl *)data)->us
            : (const struct retro_core_options_v2 *)data;
         const struct retro_core_option_v2_definition *d;
         if (!o2) return true;
         for (d = o2->definitions; d && d->key; d++)
            if (d->default_value && !find_var(d->key))
               add_var(d->key, d->default_value);
         return true;
      }
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         const char *v = find_var(var->key);
         var->value = v;
         return v != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false; return true;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
         struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
         if (cb->context_type != RETRO_HW_CONTEXT_VULKAN) return false;
         hw_render = *cb;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
         negotiation = (const struct retro_hw_render_context_negotiation_interface_vulkan *)data;
         return true;
      case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
         *(const struct retro_hw_render_interface_vulkan **)data = &iface;
         return true;
      case RETRO_ENVIRONMENT_GET_HDR_PAPER_WHITE_NITS:
         *(float *)data = 200.0f; return true;
      case RETRO_ENVIRONMENT_GET_HDR_MAX_NITS:
         *(float *)data = 1000.0f; return true;
      case RETRO_ENVIRONMENT_GET_HDR_EXPAND_GAMUT:
         *(bool *)data = false; return true;
      case RETRO_ENVIRONMENT_GET_HDR_OUTPUT_MODE:
         *(unsigned *)data = 1; return true; /* HDR10 */
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{ (void)pitch; if (data == RETRO_HW_FRAME_BUFFER_VALID) { frame_valid++;
   if (last_w != width || last_h != height)
      fprintf(stderr, "[vkhost] geometry %ux%u\n", width, height);
   last_w = width; last_h = height; } }
static void input_poll_cb(void) {}
static int cur_frame;
/* VKHOST_INPUT: comma list of first-last:joypad_id held ranges, e.g.
 * "20-30:3,60-300:4" (3=START, 4=UP per RETRO_DEVICE_ID_JOYPAD_*). */
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
   const char *e = getenv("VKHOST_INPUT");
   (void)device; (void)index;
   if (port != 0 || !e) return 0;
   {
      char buf[256]; char *tok;
      strncpy(buf, e, sizeof(buf) - 1); buf[sizeof(buf)-1] = 0;
      tok = strtok(buf, ",");
      while (tok)
      {
         int a, b; unsigned bid;
         if (sscanf(tok, "%d-%d:%u", &a, &b, &bid) == 3 &&
             cur_frame >= a && cur_frame <= b && bid == id)
            return 1;
         tok = strtok(NULL, ",");
      }
   }
   return 0;
}
static size_t audio_batch_cb(const int16_t *data, size_t frames) { (void)data; return frames; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }

/* ---- vulkan bring-up ---- */
static PFN_vkGetInstanceProcAddr gipa;
static bool create_instance(void)
{
   const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
   const char *exts[]   = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
   VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
   VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };


   app.pApplicationName = "vkhost";
   app.apiVersion = VK_API_VERSION_1_2;
   if (negotiation && negotiation->get_application_info)
   {
      const VkApplicationInfo *ai = negotiation->get_application_info();
      if (ai) app = *ai;
   }
   ci.pApplicationInfo = &app;
   /* VKHOST_NO_VALIDATION=1 drops the layer: needed under ThreadSanitizer,
    * where the layer's own rwlock teardown races and aborts the run. */
   ci.enabledLayerCount = (getenv("VKHOST_NO_VALIDATION") != NULL) ? 0 : 1;
   ci.ppEnabledLayerNames = layers;
   ci.enabledExtensionCount = 1;
   ci.ppEnabledExtensionNames = exts;
   if (vkCreateInstance(&ci, NULL, &instance) != VK_SUCCESS)
      return false;

   {
      PFN_vkCreateDebugUtilsMessengerEXT cdm = (PFN_vkCreateDebugUtilsMessengerEXT)
         vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
      VkDebugUtilsMessengerCreateInfoEXT mi = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
      mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
      mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
      mi.pfnUserCallback = debug_cb;
      if (cdm) cdm(instance, &mi, NULL, &messenger);
   }

   {
      uint32_t n = 0; VkPhysicalDevice devs[8];
      vkEnumeratePhysicalDevices(instance, &n, NULL);
      if (!n) return false;
      if (n > 8) n = 8;
      vkEnumeratePhysicalDevices(instance, &n, devs);
      gpu = devs[0];
      {
         VkPhysicalDeviceProperties props;
         vkGetPhysicalDeviceProperties(gpu, &props);
         fprintf(stderr, "[vkhost] device: %s\n", props.deviceName);
      }
   }
   return true;
}

/* ---- frame dump: copy last_image to host memory and write PPM ---- */
/* The .raw next to each .ppm holds the exact copied bytes; decode it by the
 * numeric VkFormat in the log line, not by assumption. The trap that cost
 * this harness two debugging sessions: VkFormat 64 is
 * VK_FORMAT_A2B10G10R10_UNORM_PACK32 - 4-byte packed texels - while
 * R16G16B16A16_SFLOAT is 97. The core's HDR scanout prefers A2B10G10R10
 * when the device supports it (renderer_hdr_scanout_format), so an "HDR"
 * raw is normally 10-bit packed PQ, not fp16. Reading those 4-byte texels
 * as 8-byte half-float pairs manufactures a doubled side-by-side picture
 * over a dead lower half, +/-50000 pseudo-values, NaNs, and an
 * always-wrong alpha lane - which a correct 10-10-10-2 decode of the same
 * bytes reveals to be an ordinary, healthy PQ frame. */
static int dump_frame(const char *path)
{
   VkDevice dev = vkctx.device;
   VkQueue queue = vkctx.queue;
   uint32_t qfam = vkctx.queue_family_index;
   VkImage img;
   VkExtent2D ext;
   VkCommandPool pool; VkCommandBuffer cmd;
   VkBuffer buf; VkDeviceMemory mem;
   VkDeviceSize size;
   VkFormat fmt;
   if (!last_image) { fprintf(stderr, "[vkhost] no image set\n"); return -1; }
   /* The interface hands semaphores with set_image; a real frontend waits
    * them on its own submission. A harness can afford the sledgehammer. */
   vkDeviceWaitIdle(dev);
   img = last_image->create_info.image;
   fmt = last_image->create_info.format;
   { const char *e = getenv("VKHOST_DUMP_WH");
     if (e) sscanf(e, "%ux%u", &ext.width, &ext.height);
     else { ext.width = last_w ? last_w : 1024; ext.height = last_h ? last_h : 512; } }
   size = (VkDeviceSize)ext.width * ext.height * 8 + 65536; /* room for fp16 RGBA */

   { VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
     pi.queueFamilyIndex = qfam;
     vkCreateCommandPool(dev, &pi, NULL, &pool); }
   { VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
     ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
     vkAllocateCommandBuffers(dev, &ai, &cmd); }
   { VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
     bi.size = size; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
     vkCreateBuffer(dev, &bi, NULL, &buf); }
   { VkMemoryRequirements req; VkPhysicalDeviceMemoryProperties mp;
     VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
     uint32_t i;
     vkGetBufferMemoryRequirements(dev, buf, &req);
     vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
     mi.allocationSize = req.size;
     for (i = 0; i < mp.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        { mi.memoryTypeIndex = i; break; }
     vkAllocateMemory(dev, &mi, NULL, &mem);
     vkBindBufferMemory(dev, buf, mem, 0); }

   { VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
     VkImageMemoryBarrier bar = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
     VkBufferImageCopy copy;
     vkBeginCommandBuffer(cmd, &bi);
     bar.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
     bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
     bar.oldLayout = last_image->image_layout;
     bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
     bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
     bar.image = img;
     bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     bar.subresourceRange.levelCount = 1;
     bar.subresourceRange.layerCount = 1;
     vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, NULL, 0, NULL, 1, &bar);
     memset(&copy, 0, sizeof(copy));
     copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     copy.imageSubresource.layerCount = 1;
     copy.imageOffset.x = 0;
     copy.imageOffset.y = 0;
     copy.imageExtent.width  = ext.width;
     copy.imageExtent.height = ext.height;
     copy.imageExtent.depth  = 1;
     vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &copy);
     bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
     bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
     bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
     bar.newLayout = last_image->image_layout;
     vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          0, 0, NULL, 0, NULL, 1, &bar);
     vkEndCommandBuffer(cmd); }

   { VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
     VkFence fence; VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
     si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
     vkCreateFence(dev, &fi, NULL, &fence);
     vkQueueSubmit(queue, 1, &si, fence);
     vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
     vkDestroyFence(dev, fence, NULL); }

   { void *map = NULL; FILE *f; unsigned x, y;
     vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &map);
     { char rawpath[640]; FILE *rf;
       snprintf(rawpath, sizeof(rawpath), "%s.raw", path);
       rf = fopen(rawpath, "wb");
       if (rf) { fwrite(map, 1, (size_t)size, rf); fclose(rf); } }
     f = fopen(path, "wb");
     if (!f)
     {
        /* A full disk here used to crash the harness inside fprintf(NULL):
         * the .raw fopen above was guarded, this one was not. */
        fprintf(stderr, "[vkhost] cannot open %s for writing\n", path);
        vkUnmapMemory(dev, mem);
        vkDestroyBuffer(dev, buf, NULL);
        vkFreeMemory(dev, mem, NULL);
        return 0;
     }
     fprintf(f, "P6\n%u %u\n255\n", ext.width, ext.height);
     for (y = 0; y < ext.height; y++)
        for (x = 0; x < ext.width; x++)
        {
           unsigned char px[3] = {0,0,0};
           if (fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_UNORM)
           {
              const unsigned char *p = (const unsigned char *)map + (y * (size_t)ext.width + x) * 4;
              if (fmt == VK_FORMAT_B8G8R8A8_UNORM) { px[0]=p[2]; px[1]=p[1]; px[2]=p[0]; }
              else { px[0]=p[0]; px[1]=p[1]; px[2]=p[2]; }
           }
           else if (fmt == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
           {
              uint32_t v = ((const uint32_t *)map)[y * (size_t)ext.width + x];
              px[0] = (unsigned char)(((v      ) & 0x3FF) >> 2);
              px[1] = (unsigned char)(((v >> 10) & 0x3FF) >> 2);
              px[2] = (unsigned char)(((v >> 20) & 0x3FF) >> 2);
           }
           else if (fmt == VK_FORMAT_R16G16B16A16_SFLOAT)
           {
              const uint16_t *p = (const uint16_t *)map + (y * (size_t)ext.width + x) * 4;
              unsigned c;
              for (c = 0; c < 3; c++)
              {
                 uint16_t h = p[c];
                 int e = (h >> 10) & 0x1F; int m = h & 0x3FF;
                 float v = 0.0f;
                 if (e) v = (float)(m + 1024) * (1.0f / 1024.0f) * (float)(1 << e) / 32768.0f;
                 else   v = (float)m * (1.0f / 1024.0f) / 16384.0f;
                 if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
                 px[c] = (unsigned char)(v * 255.0f + 0.5f);
              }
           }
           fwrite(px, 1, 3, f);
        }
     fclose(f);
     vkUnmapMemory(dev, mem); }

   vkDestroyBuffer(dev, buf, NULL);
   vkFreeMemory(dev, mem, NULL);
   vkDestroyCommandPool(dev, pool, NULL);
   fprintf(stderr, "[vkhost] dumped %s (%ux%u fmt %d)\n", path, ext.width, ext.height, (int)fmt);
   return 0;
}

typedef void (*set_env_t)(retro_environment_t);
typedef void (*set_cb_t)(void *);

int main(int argc, char **argv)
{
   const char *core_path, *content;
   const char *state_path = NULL;
   int frames = 120;
   const char *outdir = "/tmp/vkhost_out";
   char cmdbuf[1024];

   if (argc < 3)
   { fprintf(stderr, "usage: %s core content [state] [frames] [outdir]\n", argv[0]); return 2; }
   core_path = argv[1]; content = argv[2];
   if (argc > 3 && strcmp(argv[3], "-")) state_path = argv[3];
   if (argc > 4) frames = atoi(argv[4]);
   if (argc > 5) outdir = argv[5];
   snprintf(cmdbuf, sizeof(cmdbuf), "mkdir -p %s %s %s", outdir, sysdir, savedir);
   system(cmdbuf);

   /* defaults, overridable via VKHOST_VARS */
   add_var("beetle_psx_hw_renderer", "vulkan");
   add_var("beetle_psx_hw_pgxp_mode", "memory only");
   add_var("beetle_psx_hw_color_format", "30bit_hdr");
   add_var("beetle_psx_hw_internal_resolution", "1x");
   add_var("beetle_psx_hw_filter", "nearest");
   {
      const char *e = getenv("VKHOST_VARS");
      if (e)
      {
         char *dup = strdup(e), *tok = strtok(dup, ";");
         while (tok)
         {
            char *eq = strchr(tok, '=');
            if (eq)
            {
               int i; *eq = 0;
               for (i = 0; i < n_vars; i++)
                  if (!strcmp(var_keys[i], tok)) { free(var_vals[i]); var_vals[i] = strdup(eq + 1); break; }
               if (i == n_vars) add_var(tok, eq + 1);
            }
            tok = strtok(NULL, ";");
         }
      }
   }

   core = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!core) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define SYM(name) *(void **)(&name##_fn) = dlsym(core, #name)
   { set_env_t retro_set_environment_fn; SYM(retro_set_environment);
     retro_set_environment_fn(env_cb); }
   { void (*retro_init_fn)(void); SYM(retro_init); retro_init_fn(); }
   { void (*f)(retro_video_refresh_t) = dlsym(core, "retro_set_video_refresh"); f(video_cb); }
   { void (*f)(retro_input_poll_t) = dlsym(core, "retro_set_input_poll"); f(input_poll_cb); }
   { void (*f)(retro_input_state_t) = dlsym(core, "retro_set_input_state"); f(input_state_cb); }
   { void (*f)(retro_audio_sample_t) = dlsym(core, "retro_set_audio_sample"); f(audio_cb); }
   { void (*f)(retro_audio_sample_batch_t) = dlsym(core, "retro_set_audio_sample_batch"); f(audio_batch_cb); }

   {
      struct retro_game_info info;
      bool (*retro_load_game_fn)(const struct retro_game_info *) = dlsym(core, "retro_load_game");
      memset(&info, 0, sizeof(info));
      info.path = content;
      if (!retro_load_game_fn(&info))
      { fprintf(stderr, "[vkhost] retro_load_game failed\n"); return 3; }
   }

   if (!negotiation && !hw_render.context_reset)
   {
      fprintf(stderr, "[vkhost] software renderer: no Vulkan bring-up\n");
      goto run_frames_sw;
   }
   if (!create_instance()) { fprintf(stderr, "[vkhost] instance failed\n"); return 3; }
   gipa = vkGetInstanceProcAddr;

   memset(&vkctx, 0, sizeof(vkctx));
   if (negotiation && negotiation->create_device)
   {
      static const VkPhysicalDeviceFeatures no_features; /* zeroed, as RetroArch passes */
      if (!negotiation->create_device(&vkctx, instance, gpu, VK_NULL_HANDLE,
               vkGetInstanceProcAddr, NULL, 0, NULL, 0, &no_features))
      { fprintf(stderr, "[vkhost] core create_device failed\n"); return 3; }
   }
   else { fprintf(stderr, "[vkhost] no negotiation interface\n"); return 3; }

   iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
   iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
   iface.handle = NULL;
   iface.instance = instance;
   iface.gpu = vkctx.gpu;
   iface.device = vkctx.device;
   iface.get_device_proc_addr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
   iface.get_instance_proc_addr = vkGetInstanceProcAddr;
   iface.queue = vkctx.queue;
   iface.queue_index = vkctx.queue_family_index;
   iface.set_image = vk_set_image;
   iface.get_sync_index = vk_get_sync_index;
   iface.get_sync_index_mask = vk_get_sync_index_mask;
   iface.wait_sync_index = vk_wait_sync_index;
   iface.set_command_buffers = vk_set_command_buffers;
   iface.lock_queue = vk_lock_queue;
   iface.unlock_queue = vk_unlock_queue;
   iface.set_signal_semaphore = vk_set_signal_semaphore;

   if (hw_render.context_reset) hw_render.context_reset();

run_frames_sw:

   if (state_path)
   {
      FILE *f = fopen(state_path, "rb"); long sz; void *data;
      bool (*retro_unserialize_fn)(const void *, size_t) = dlsym(core, "retro_unserialize");
      if (!f) { fprintf(stderr, "[vkhost] cannot open state\n"); return 4; }
      fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
      data = malloc(sz); fread(data, 1, sz, f); fclose(f);
      /* RetroArch RASTATE container: find the MEM chunk and use its payload. */
      if (sz > 8 && !memcmp(data, "RASTATE", 7))
      {
         unsigned char *p = (unsigned char *)data + 8;
         unsigned char *end = (unsigned char *)data + sz;
         void *payload = NULL; size_t paysz = 0;
         while (p + 8 <= end)
         {
            uint32_t chunk_sz;
            memcpy(&chunk_sz, p + 4, 4);
            if (!memcmp(p, "MEM ", 4)) { payload = p + 8; paysz = chunk_sz; break; }
            p += 8 + ((chunk_sz + 7u) & ~7u);
         }
         if (payload)
         { fprintf(stderr, "[vkhost] RASTATE MEM chunk %zu bytes\n", paysz);
           if (!retro_unserialize_fn(payload, paysz))
           { fprintf(stderr, "[vkhost] unserialize failed\n"); return 4; } }
         else { fprintf(stderr, "[vkhost] RASTATE without MEM chunk\n"); return 4; }
      }
      else if (!retro_unserialize_fn(data, (size_t)sz))
      { fprintf(stderr, "[vkhost] unserialize failed (raw)\n"); return 4; }
      free(data);
      fprintf(stderr, "[vkhost] savestate loaded\n");
   }

   {
      int i;
      void (*retro_run_fn)(void) = dlsym(core, "retro_run");
      for (i = 0; i < frames; i++)
      {
         cur_frame = i;
         retro_run_fn();
         if (vkctx.device && ((i % 30) == 29 || i == frames - 1))
         {
            char path[600];
            snprintf(path, sizeof(path), "%s/frame_%04d.ppm", outdir, i + 1);
            dump_frame(path);
         }
      }
   }

   fprintf(stderr, "[vkhost] done: %d frames run, %u valid, %d validation errors, %d warnings\n",
           frames, frame_valid, validation_errors, validation_warnings);
   { void (*f)(void) = dlsym(core, "retro_unload_game"); if (f) f(); }
   { void (*f)(void) = dlsym(core, "retro_deinit"); if (f) f(); }
   return validation_errors ? 5 : 0;
}
