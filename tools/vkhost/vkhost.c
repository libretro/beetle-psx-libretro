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
 *   VKHOST_PREEMPT=1: model RetroArch's Preemptive Frames (rollback by
 *     one frame, glhost's GLHOST_PREEMPT=2): every frame restore the state
 *     saved before the previous one, replay that frame, save, then run.
 *   VKHOST_VARS_AT: semicolon list of frame:key=value option changes, each
 *     applied before that frame's retro_run and reported through
 *     GET_VARIABLE_UPDATE, as a frontend does when the user edits an option
 *     (glhost's GLHOST_VARS_AT).
 *   VKHOST_NEGOTIATION=1: a frontend that predates Vulkan negotiation v2
 *   (no support query, only a version 1 interface accepted). By default
 *   the host offers v2, as RetroArch does, and requires the core to create
 *   its device through create_device2's wrapper.
 *   VKHOST_QUEUE_THREAD: run a second thread on the frontend's queue, the
 *   way a threaded frontend does (see "the frontend's queue" below).
 *   HOST_HDR_*: the display presented through GET_HDR_METADATA and
 *   GET_DISPLAY_METADATA; see tools/hdr_display/host_metadata.h.
 *   VKHOST_LOAD_PAUSE_MS: wait this long between loading the content and
 *     the first frame, as a frontend's menu or load screen does.
 *   VKHOST_SAVE_DIR: the save directory handed to the core (default
 *     /tmp/vkhost_save).
 *   The pipelines the core builds are counted by thread: the run ends
 *   with how many the frame's thread built, before and after the first
 *   frame, and how many the core's own threads built.
 *   VKHOST_NEXT_CONTENT: after the teardown (context_destroy, unload,
 *   deinit), run this content as a second session in the same process -
 *   the core stays loaded, as with a statically linked frontend - and dump
 *   its frames to <outdir>/next.
 *   VKHOST_UPSCALER="rw,rh[,minw,minh,maxw,maxh[,outw,outh]]": offer the
 *   core a frontend temporal upscaler (GET_HW_RENDER_UPSCALER_INTERFACE,
 *   lrhost's LRHOST_UPSCALER) asking for rw x rh frames, by default within
 *   rw/4..rw*2 and an output twice the size. The core must ask for it in
 *   retro_load_game (counted) and get it after context_reset. Each dumped
 *   frame gets its motion vectors, depth and mask beside it
 *   (frame_NNNN.mv.raw, .depth.raw, .mask.raw).
 *   VKHOST_UPSCALER_LOG: one line per presented frame with the inputs the
 *   core handed over (jitter, flags, the images' sizes and formats), or
 *   "none".
 *   VKHOST_UPSCALER_STATS: read the transparency mask back every frame and
 *   append to the log line how much of it is set, in thousandths.
 *
 * Exits non-zero on any validation error, on any use the core makes of
 * the frontend's queue without holding lock_queue, and on any wait it
 * makes with lock_queue held.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <dlfcn.h>
#include <pthread.h>
#include <vulkan/vulkan.h>
#include "libretro.h"
#include "../hdr_display/host_metadata.h"
#include "libretro_vulkan.h"

/* ---- tiny dynamic table of core option overrides ---- */
static char *var_keys[512]; static char *var_vals[512]; static int n_vars;
static void add_var(const char *k, const char *v)
{ if (n_vars < 512) { var_keys[n_vars] = strdup(k); var_vals[n_vars] = strdup(v); n_vars++; } }
static const char *find_var(const char *k)
{ int i; for (i = 0; i < n_vars; i++) if (!strcmp(var_keys[i], k)) return var_vals[i]; return NULL; }
static int vars_updated;
static void set_var(const char *k, const char *v)
{
   int i;
   for (i = 0; i < n_vars; i++)
      if (!strcmp(var_keys[i], k))
      { free(var_vals[i]); var_vals[i] = strdup(v); vars_updated = 1; return; }
   add_var(k, v); vars_updated = 1;
}
/* Apply the VKHOST_VARS_AT entries scheduled for this frame. */
static void apply_vars_at(int frame)
{
   const char *at = getenv("VKHOST_VARS_AT");
   char *dup, *tok;
   if (!at) return;
   dup = strdup(at);
   for (tok = strtok(dup, ";"); tok; tok = strtok(NULL, ";"))
   {
      char *colon = strchr(tok, ':'), *eq;
      if (!colon || atoi(tok) != frame) continue;
      eq = strchr(colon + 1, '=');
      if (!eq) continue;
      *eq = 0;
      fprintf(stderr, "[vkhost] frame %d: %s=%s\n", frame, colon + 1, eq + 1);
      set_var(colon + 1, eq + 1);
   }
   free(dup);
}

/* ---- state ---- */
static void *core;
static struct retro_hw_render_callback hw_render;
static const struct retro_hw_render_context_negotiation_interface_vulkan *negotiation;
static struct retro_hw_render_interface_vulkan iface;
static struct retro_vulkan_context vkctx;
/* the core's context is up (context_reset ran, context_destroy has not) */
static int context_live;
static unsigned av_max_width, av_max_height;
static void host_reinit_context(void);
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

/* ---- the frontend's queue: its lock, and who uses the queue without it ----
 *
 * The queue the core hands back from create_device is the frontend's as
 * well: RetroArch submits its frames and presents on it, and under
 * threaded video does so on another thread. A core that submits for
 * itself has to hold lock_queue around every use of that queue, and
 * vkDeviceWaitIdle is a use of every queue. The core that skipped the
 * lock ran for years against an unthreaded frontend and then hung the
 * GPU the moment the frontend's submissions moved to a video thread.
 *
 * So the lock here is a real one, the core's Vulkan entry points for the
 * queue are handed out through this file, and each use of the frontend's
 * queue made without the lock held by the calling thread is counted and
 * fails the run.
 *
 * The other half of the rule is that the lock is held for the queue call
 * and nothing longer. A core that waits for the GPU with it held - a
 * fence wait, or vkDeviceWaitIdle / vkQueueWaitIdle, which are waits and
 * queue uses both - parks the frontend's video thread for as long as the
 * GPU takes, and is one dependency away from a deadlock. Each wait made
 * with the lock held is counted and fails the run as well. With VKHOST_QUEUE_THREAD set, a second thread submits on
 * the queue under the lock the whole time, as the frontend's video
 * thread does, so that the validation layer's own thread check sees a
 * core that skips it too. */
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t queue_owner;
static int queue_held;                 /* read and written with __atomic */
static int queue_violations;
/* the negotiation version offered, and create_device2 wrapper calls */
static unsigned negotiation_offer = 2;
static unsigned wrapper_calls;
static VkPhysicalDevice wrapper_gpu;
static int queue_waits_locked;
static PFN_vkWaitForFences real_wait_for_fences;
static PFN_vkGetDeviceProcAddr real_gdpa;
static PFN_vkQueueSubmit real_queue_submit;
/* vkQueueSubmit calls the core makes (each one a lock_queue/unlock_queue
 * pair when the queue is the frontend's) */
static unsigned long queue_submits, queue_submits_half;
static PFN_vkQueueWaitIdle real_queue_wait_idle;
static PFN_vkDeviceWaitIdle real_device_wait_idle;

static void host_lock_queue(void)
{
   pthread_mutex_lock(&queue_mutex);
   queue_owner = pthread_self();
   __atomic_store_n(&queue_held, 1, __ATOMIC_SEQ_CST);
}
static void host_unlock_queue(void)
{
   __atomic_store_n(&queue_held, 0, __ATOMIC_SEQ_CST);
   pthread_mutex_unlock(&queue_mutex);
}
static void vk_lock_queue(void *handle) { (void)handle; host_lock_queue(); }
static void vk_unlock_queue(void *handle) { (void)handle; host_unlock_queue(); }

static int host_queue_held_here(void)
{
   return __atomic_load_n(&queue_held, __ATOMIC_SEQ_CST)
      && pthread_equal(queue_owner, pthread_self());
}

static void core_waits(const char *what)
{
   if (host_queue_held_here() && !queue_waits_locked++)
      fprintf(stderr, "[vkhost] QUEUE: the core waited in %s with lock_queue held\n", what);
}

/* queue == VK_NULL_HANDLE: a call that uses every queue of the device. */
static void core_uses_queue(const char *what, VkQueue queue)
{
   if (!vkctx.queue || (queue && queue != vkctx.queue))
      return;
   if (host_queue_held_here())
      return;
   if (!queue_violations++)
      fprintf(stderr, "[vkhost] QUEUE: the core called %s on the frontend's queue"
            " without holding lock_queue\n", what);
}
static VKAPI_ATTR VkResult VKAPI_CALL core_vkQueueSubmit(VkQueue queue,
      uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
   core_uses_queue("vkQueueSubmit", queue);
   __atomic_add_fetch(&queue_submits, 1, __ATOMIC_SEQ_CST);
   return real_queue_submit(queue, count, submits, fence);
}
static VKAPI_ATTR VkResult VKAPI_CALL core_vkQueueWaitIdle(VkQueue queue)
{
   core_uses_queue("vkQueueWaitIdle", queue);
   core_waits("vkQueueWaitIdle");
   return real_queue_wait_idle(queue);
}
static VKAPI_ATTR VkResult VKAPI_CALL core_vkDeviceWaitIdle(VkDevice device)
{
   core_uses_queue("vkDeviceWaitIdle", VK_NULL_HANDLE);
   core_waits("vkDeviceWaitIdle");
   return real_device_wait_idle(device);
}
static VKAPI_ATTR VkResult VKAPI_CALL core_vkWaitForFences(VkDevice device,
      uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout)
{
   core_waits("vkWaitForFences");
   return real_wait_for_fences(device, count, fences, all, timeout);
}
/* Vulkan objects the core creates, counted so the steady-state report can
 * say how many a run of frames needed: a display path that keeps its
 * resources creates none once warmed up. */
static unsigned long object_creates, object_creates_half;
static PFN_vkCreateImage        real_create_image;
static PFN_vkCreateImageView    real_create_image_view;
static PFN_vkCreateBuffer       real_create_buffer;
static PFN_vkCreateBufferView   real_create_buffer_view;
static PFN_vkCreateFramebuffer  real_create_framebuffer;
static PFN_vkAllocateMemory     real_allocate_memory;
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateImage(VkDevice d,
      const VkImageCreateInfo *i, const VkAllocationCallbacks *a, VkImage *o)
{ object_creates++; return real_create_image(d, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateImageView(VkDevice d,
      const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *o)
{ object_creates++; return real_create_image_view(d, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateBuffer(VkDevice d,
      const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *o)
{ object_creates++; return real_create_buffer(d, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateBufferView(VkDevice d,
      const VkBufferViewCreateInfo *i, const VkAllocationCallbacks *a, VkBufferView *o)
{ object_creates++; return real_create_buffer_view(d, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateFramebuffer(VkDevice d,
      const VkFramebufferCreateInfo *i, const VkAllocationCallbacks *a, VkFramebuffer *o)
{ object_creates++; return real_create_framebuffer(d, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkAllocateMemory(VkDevice d,
      const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *o)
{ object_creates++; return real_allocate_memory(d, i, a, o); }

/* Compute workgroups the core dispatches. The renderer's CPU->VRAM uploads
 * are compute passes sized by the rect, so this measures how much VRAM a
 * run hands the GPU. */
static unsigned long long dispatch_groups, dispatch_groups_half;
static PFN_vkCmdDispatch        real_cmd_dispatch;
static VKAPI_ATTR void VKAPI_CALL core_vkCmdDispatch(VkCommandBuffer cb,
      uint32_t x, uint32_t y, uint32_t z)
{ dispatch_groups += (unsigned long long)x * y * z; real_cmd_dispatch(cb, x, y, z); }

/* Pipelines the core builds, by thread: on the thread that runs the core
 * (retro_load_game, context_reset, retro_run - the frame's thread), and on
 * any thread of the core's own. A pipeline built on the frame's thread
 * after the first frame is a stall in the middle of play. */
static pthread_t     host_thread;
static int           frames_started;
static unsigned long pipelines_host, pipelines_host_running, pipelines_other;
static PFN_vkCreateGraphicsPipelines real_create_graphics_pipelines;
static PFN_vkCreateComputePipelines  real_create_compute_pipelines;
/* The longest retro_run after the first, and which frame it was. */
static double longest_frame_ms;
static int    longest_frame;
static void count_pipelines(uint32_t n)
{
   if (pthread_equal(pthread_self(), host_thread))
   {
      __atomic_add_fetch(&pipelines_host, n, __ATOMIC_SEQ_CST);
      if (frames_started)
         __atomic_add_fetch(&pipelines_host_running, n, __ATOMIC_SEQ_CST);
   }
   else
      __atomic_add_fetch(&pipelines_other, n, __ATOMIC_SEQ_CST);
}
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateGraphicsPipelines(VkDevice d,
      VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo *i,
      const VkAllocationCallbacks *a, VkPipeline *o)
{ count_pipelines(n); return real_create_graphics_pipelines(d, c, n, i, a, o); }
static VKAPI_ATTR VkResult VKAPI_CALL core_vkCreateComputePipelines(VkDevice d,
      VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo *i,
      const VkAllocationCallbacks *a, VkPipeline *o)
{ count_pipelines(n); return real_create_compute_pipelines(d, c, n, i, a, o); }

static PFN_vkVoidFunction core_queue_entry(const char *name, PFN_vkVoidFunction real)
{
   if (!real)
      return real;
   if (!strcmp(name, "vkCreateGraphicsPipelines"))
   { real_create_graphics_pipelines = (PFN_vkCreateGraphicsPipelines)real; return (PFN_vkVoidFunction)core_vkCreateGraphicsPipelines; }
   if (!strcmp(name, "vkCreateComputePipelines"))
   { real_create_compute_pipelines = (PFN_vkCreateComputePipelines)real; return (PFN_vkVoidFunction)core_vkCreateComputePipelines; }
   if (!strcmp(name, "vkCmdDispatch"))
   { real_cmd_dispatch = (PFN_vkCmdDispatch)real; return (PFN_vkVoidFunction)core_vkCmdDispatch; }
   if (!strcmp(name, "vkCreateImage"))
   { real_create_image = (PFN_vkCreateImage)real; return (PFN_vkVoidFunction)core_vkCreateImage; }
   if (!strcmp(name, "vkCreateImageView"))
   { real_create_image_view = (PFN_vkCreateImageView)real; return (PFN_vkVoidFunction)core_vkCreateImageView; }
   if (!strcmp(name, "vkCreateBuffer"))
   { real_create_buffer = (PFN_vkCreateBuffer)real; return (PFN_vkVoidFunction)core_vkCreateBuffer; }
   if (!strcmp(name, "vkCreateBufferView"))
   { real_create_buffer_view = (PFN_vkCreateBufferView)real; return (PFN_vkVoidFunction)core_vkCreateBufferView; }
   if (!strcmp(name, "vkCreateFramebuffer"))
   { real_create_framebuffer = (PFN_vkCreateFramebuffer)real; return (PFN_vkVoidFunction)core_vkCreateFramebuffer; }
   if (!strcmp(name, "vkAllocateMemory"))
   { real_allocate_memory = (PFN_vkAllocateMemory)real; return (PFN_vkVoidFunction)core_vkAllocateMemory; }
   if (!strcmp(name, "vkQueueSubmit"))
   { real_queue_submit = (PFN_vkQueueSubmit)real; return (PFN_vkVoidFunction)core_vkQueueSubmit; }
   if (!strcmp(name, "vkQueueWaitIdle"))
   { real_queue_wait_idle = (PFN_vkQueueWaitIdle)real; return (PFN_vkVoidFunction)core_vkQueueWaitIdle; }
   if (!strcmp(name, "vkDeviceWaitIdle"))
   { real_device_wait_idle = (PFN_vkDeviceWaitIdle)real; return (PFN_vkVoidFunction)core_vkDeviceWaitIdle; }
   if (!strcmp(name, "vkWaitForFences"))
   { real_wait_for_fences = (PFN_vkWaitForFences)real; return (PFN_vkVoidFunction)core_vkWaitForFences; }
   return real;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL core_gdpa(VkDevice device, const char *name)
{
   return core_queue_entry(name, real_gdpa(device, name));
}
/* create_device2's wrapper: adds an extension of the frontend's own where
 * the GPU has it, as RetroArch adds its low-latency ones, and creates the
 * device. */
static VkDevice create_device_wrapper(VkPhysicalDevice pgpu, void *opaque, const VkDeviceCreateInfo *info)
{
   VkDeviceCreateInfo local = *info;
   const char *exts[256];
   VkExtensionProperties props[512];
   uint32_t i, j, n = 512, count = info->enabledExtensionCount;
   VkDevice device = VK_NULL_HANDLE;
   (void)opaque;
   wrapper_calls++;
   wrapper_gpu = pgpu;
   if (count > 255)
      return VK_NULL_HANDLE;
   if (count)
      memcpy(exts, info->ppEnabledExtensionNames, count * sizeof(*exts));
   if (vkEnumerateDeviceExtensionProperties(pgpu, NULL, &n, props) >= 0)
      for (i = 0; i < n; i++)
         if (!strcmp(props[i].extensionName, "VK_KHR_present_id"))
         {
            for (j = 0; j < count && strcmp(exts[j], "VK_KHR_present_id"); j++) ;
            if (j == count)
               exts[count++] = "VK_KHR_present_id";
         }
   local.enabledExtensionCount   = count;
   local.ppEnabledExtensionNames = exts;
   if (vkCreateDevice(pgpu, &local, NULL, &device) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return device;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL core_gipa(VkInstance inst, const char *name)
{
   PFN_vkVoidFunction real = vkGetInstanceProcAddr(inst, name);
   if (real && !strcmp(name, "vkGetDeviceProcAddr"))
   { real_gdpa = (PFN_vkGetDeviceProcAddr)real; return (PFN_vkVoidFunction)core_gdpa; }
   return core_queue_entry(name, real);
}

/* The frontend's video thread, as far as the queue is concerned. */
static pthread_t queue_thread;
static int queue_thread_run;
static void *queue_thread_main(void *unused)
{
   struct timespec nap = { 0, 50 * 1000 };
   (void)unused;
   while (__atomic_load_n(&queue_thread_run, __ATOMIC_SEQ_CST))
   {
      host_lock_queue();
      vkQueueSubmit(vkctx.queue, 0, NULL, VK_NULL_HANDLE);
      host_unlock_queue();
      nanosleep(&nap, NULL);
   }
   return NULL;
}
static void vk_set_signal_semaphore(void *handle, VkSemaphore sem) { (void)handle; (void)sem; }

/* ---- the frontend's temporal upscaler (VKHOST_UPSCALER) ----
 *
 * Offered the way lrhost offers it, so the same checker scripts read
 * both: the core asks in retro_load_game (false, counted), asks again
 * after context_reset and gets the interface, reads the request every
 * frame and hands its inputs over before set_image. Each presented frame
 * is logged, and a dumped frame gets its inputs read back beside it. */
static struct retro_hw_render_upscaler_interface up_iface;
static struct retro_hw_upscaler_request up_req;
static struct retro_hw_upscaler_inputs up_inputs;
static int  up_on, up_announced, up_have_inputs, up_ready, up_dumpable;
static long up_frames;
static FILE *up_log;

static bool up_get_request(void *h, struct retro_hw_upscaler_request *r)
{
   (void)h; *r = up_req; return true;
}
static void up_set_inputs(void *h, const struct retro_hw_upscaler_inputs *in)
{
   (void)h;
   up_have_inputs = in != NULL;
   if (in) up_inputs = *in;
}
static void up_init(void)
{
   unsigned v[8] = { 0 }; int n;
   const char *e = getenv("VKHOST_UPSCALER");
   if (!e) return;
   n = sscanf(e, "%u,%u,%u,%u,%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
   if (n < 2) return;
   if (n < 6) { v[2] = v[0] / 4; v[3] = v[1] / 4; v[4] = v[0] * 2; v[5] = v[1] * 2; }
   if (n < 8) { v[6] = v[0] * 2; v[7] = v[1] * 2; }
   up_req.render_width = v[0]; up_req.render_height = v[1];
   up_req.min_render_width = v[2]; up_req.min_render_height = v[3];
   up_req.max_render_width = v[4]; up_req.max_render_height = v[5];
   up_req.output_width = v[6]; up_req.output_height = v[7];
   up_iface.interface_version = RETRO_HW_RENDER_UPSCALER_INTERFACE_VERSION;
   up_iface.handle = &up_iface;
   up_iface.get_request = up_get_request;
   up_iface.set_inputs = up_set_inputs;
   up_on = 1;
}

/* The host's own readback objects for the upscaler's images, made on
 * first use and freed before context_destroy. Created and submitted
 * through the loader's entry points, not the core's wrapped ones, so the
 * host's reads count as neither the core's objects nor its submits. */
static VkCommandPool   up_pool;
static VkCommandBuffer up_cmd;
static VkFence         up_fence;
static VkBuffer        up_rb;
static VkDeviceMemory  up_rb_mem;
static VkDeviceSize    up_rb_cap;
static void           *up_rb_map;

static int up_readback_ensure(VkDeviceSize size)
{
   VkDevice dev = vkctx.device;
   if (!up_pool)
   {
      VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
      pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      pi.queueFamilyIndex = vkctx.queue_family_index;
      if (vkCreateCommandPool(dev, &pi, NULL, &up_pool) != VK_SUCCESS)
         return 0;
      ai.commandPool = up_pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
      vkAllocateCommandBuffers(dev, &ai, &up_cmd);
      vkCreateFence(dev, &fi, NULL, &up_fence);
   }
   if (size > up_rb_cap)
   {
      VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      VkMemoryRequirements req; VkPhysicalDeviceMemoryProperties mp;
      uint32_t i, type = UINT32_MAX;
      if (up_rb)
      {
         vkUnmapMemory(dev, up_rb_mem);
         vkDestroyBuffer(dev, up_rb, NULL);
         vkFreeMemory(dev, up_rb_mem, NULL);
         up_rb = VK_NULL_HANDLE; up_rb_mem = VK_NULL_HANDLE; up_rb_map = NULL; up_rb_cap = 0;
      }
      bi.size = size; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      if (vkCreateBuffer(dev, &bi, NULL, &up_rb) != VK_SUCCESS)
         return 0;
      vkGetBufferMemoryRequirements(dev, up_rb, &req);
      vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
      for (i = 0; i < mp.memoryTypeCount; i++)
      {
         VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
         if (!(req.memoryTypeBits & (1u << i)) || !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            continue;
         if (type == UINT32_MAX || (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            type = i;
         if (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            break;
      }
      if (type == UINT32_MAX)
      {
         vkDestroyBuffer(dev, up_rb, NULL); up_rb = VK_NULL_HANDLE;
         return 0;
      }
      mi.allocationSize = req.size;
      mi.memoryTypeIndex = type;
      if (vkAllocateMemory(dev, &mi, NULL, &up_rb_mem) != VK_SUCCESS)
      {
         vkDestroyBuffer(dev, up_rb, NULL); up_rb = VK_NULL_HANDLE;
         return 0;
      }
      vkBindBufferMemory(dev, up_rb, up_rb_mem, 0);
      vkMapMemory(dev, up_rb_mem, 0, VK_WHOLE_SIZE, 0, &up_rb_map);
      up_rb_cap = size;
   }
   return up_rb != VK_NULL_HANDLE;
}

static void up_readback_free(void)
{
   VkDevice dev = vkctx.device;
   if (!dev) return;
   if (up_rb)
   {
      vkUnmapMemory(dev, up_rb_mem);
      vkDestroyBuffer(dev, up_rb, NULL);
      vkFreeMemory(dev, up_rb_mem, NULL);
   }
   if (up_pool)
   {
      vkDestroyFence(dev, up_fence, NULL);
      vkDestroyCommandPool(dev, up_pool, NULL);
   }
   up_rb = VK_NULL_HANDLE; up_rb_mem = VK_NULL_HANDLE; up_rb_map = NULL; up_rb_cap = 0;
   up_pool = VK_NULL_HANDLE; up_cmd = VK_NULL_HANDLE; up_fence = VK_NULL_HANDLE;
}

/* Copies one of the upscaler's images to the host, after the frame:
 * malloc'd, *size bytes, NULL when there is no image. The copy follows the
 * core's submissions on the same queue, so the barrier from the image's
 * state orders it after the writes without waiting the device idle. */
static void *up_read_image(const struct retro_hw_upscaler_image *img, size_t *size)
{
   VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
   VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   VkBufferImageCopy region; VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
   unsigned bpp = 4; void *out;
   if (img->format == VK_FORMAT_D32_SFLOAT) aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
   else if (img->format == VK_FORMAT_D16_UNORM) { aspect = VK_IMAGE_ASPECT_DEPTH_BIT; bpp = 2; }
   else if (img->format == VK_FORMAT_R32G32_SFLOAT) bpp = 8;
   else if (img->format == VK_FORMAT_R8_UNORM) bpp = 1;
   *size = (size_t)img->width * img->height * bpp;
   if (!img->image || !*size || !vkctx.device || !up_readback_ensure(*size))
      return NULL;

   vkResetCommandBuffer(up_cmd, 0);
   bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   vkBeginCommandBuffer(up_cmd, &bi);
   b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   b.oldLayout = (VkImageLayout)img->state; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   b.image = (VkImage)(uintptr_t)img->image;
   b.subresourceRange.aspectMask = aspect; b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
   vkCmdPipelineBarrier(up_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   memset(&region, 0, sizeof(region));
   region.imageSubresource.aspectMask = aspect;
   region.imageSubresource.layerCount = 1;
   region.imageExtent.width = img->width; region.imageExtent.height = img->height; region.imageExtent.depth = 1;
   vkCmdCopyImageToBuffer(up_cmd, b.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, up_rb, 1, &region);
   b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.newLayout = (VkImageLayout)img->state;
   vkCmdPipelineBarrier(up_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   vkEndCommandBuffer(up_cmd);
   si.commandBufferCount = 1; si.pCommandBuffers = &up_cmd;

   /* the host's own submit, under the frontend's lock as dump_frame's is */
   host_lock_queue();
   vkResetFences(vkctx.device, 1, &up_fence);
   vkQueueSubmit(vkctx.queue, 1, &si, up_fence);
   host_unlock_queue();
   vkWaitForFences(vkctx.device, 1, &up_fence, VK_TRUE, UINT64_MAX);

   out = malloc(*size);
   if (out) memcpy(out, up_rb_map, *size);
   return out;
}

/* One line per presented frame to VKHOST_UPSCALER_LOG: n is the 1-based
 * presented frame number, the colour dump's frame_NNNN. */
static void up_log_frame(unsigned long n, unsigned w, unsigned h)
{
   if (!up_log && getenv("VKHOST_UPSCALER_LOG"))
      up_log = fopen(getenv("VKHOST_UPSCALER_LOG"), "w");
   if (up_log)
   {
      if (up_have_inputs)
      {
         fprintf(up_log, "%lu %ux%u jitter %.6f %.6f flags %u mv %ux%u fmt %u depth %ux%u fmt %u mask %ux%u fmt %u", n, w, h,
                 up_inputs.jitter_x, up_inputs.jitter_y, up_inputs.flags,
                 up_inputs.motion_vectors.width, up_inputs.motion_vectors.height, up_inputs.motion_vectors.format,
                 up_inputs.depth.width, up_inputs.depth.height, up_inputs.depth.format,
                 up_inputs.transparency_mask.width, up_inputs.transparency_mask.height,
                 up_inputs.transparency_mask.format);
         /* the colour image's format, for reading its dump */
         if (last_image)
            fprintf(up_log, " color fmt %u", (unsigned)last_image->create_info.format);
         /* VKHOST_UPSCALER_STATS: the mask read back, how much of the
          * frame it covers, in thousandths */
         if (getenv("VKHOST_UPSCALER_STATS"))
         {
            size_t sz, i, set = 0;
            unsigned char *px = (unsigned char *)up_read_image(&up_inputs.transparency_mask, &sz);
            if (px)
            {
               for (i = 0; i < sz; i++)
                  set += px[i] != 0;
               fprintf(up_log, " masked %lu", (unsigned long)(sz ? set * 1000 / sz : 0));
            }
            free(px);
         }
         fputc('\n', up_log);
      }
      else
         fprintf(up_log, "%lu %ux%u none\n", n, w, h);
      fflush(up_log);
   }
   if (up_have_inputs)
      up_frames++;
   /* the inputs stay valid as long as the colour image: dump_frame's
    * caller reads them beside it */
   up_dumpable    = up_have_inputs;
   up_have_inputs = 0;
}

/* Beside a dumped frame_NNNN.ppm: frame_NNNN.mv.raw, .depth.raw, .mask.raw */
static void up_dump_images(const char *outdir, int n)
{
   const struct retro_hw_upscaler_image *imgs[3];
   const char *names[3] = { "mv", "depth", "mask" };
   unsigned k;
   if (!up_dumpable)
      return;
   imgs[0] = &up_inputs.motion_vectors; imgs[1] = &up_inputs.depth;
   imgs[2] = &up_inputs.transparency_mask;
   for (k = 0; k < 3; k++)
   {
      char fn[640]; size_t sz; FILE *df;
      void *px = up_read_image(imgs[k], &sz);
      snprintf(fn, sizeof(fn), "%s/frame_%04d.%s.raw", outdir, n, names[k]);
      if (px && (df = fopen(fn, "wb")) != NULL) { fwrite(px, 1, sz, df); fclose(df); }
      free(px);
   }
}

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
         fprintf(stderr, "[vkhost] pixel format %d\n",
               (int)*(const enum retro_pixel_format *)data);
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
      {
         /* As RetroArch: when the maximum size changes the video driver
          * is reinitialised at once, the core's context destroyed and
          * reset, with nothing in flight; otherwise only the geometry
          * is taken */
         struct retro_system_av_info *info = (struct retro_system_av_info *)data;
         bool reinit = info->geometry.max_width != av_max_width
               || info->geometry.max_height != av_max_height;
         av_max_width  = info->geometry.max_width;
         av_max_height = info->geometry.max_height;
         if (reinit && context_live && hw_render.context_destroy && hw_render.context_reset && vkctx.device)
         {
            fprintf(stderr, "[vkhost] av_info %ux%u (max %ux%u): context reset\n",
                  info->geometry.base_width, info->geometry.base_height,
                  info->geometry.max_width, info->geometry.max_height);
            host_reinit_context();
         }
         return true;
      }
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
         *(bool *)data = vars_updated != 0; vars_updated = 0; return true;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
         struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
         if (cb->context_type != RETRO_HW_CONTEXT_VULKAN) return false;
         hw_render = *cb;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
         negotiation = (const struct retro_hw_render_context_negotiation_interface_vulkan *)data;
         /* a frontend that predates v2 drops any other version */
         if (negotiation_offer < 2 && negotiation->interface_version != 1)
         {
            fprintf(stderr, "[vkhost] interface version %u dropped\n", negotiation->interface_version);
            negotiation = NULL;
         }
         return true;
      case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT:
      {
         struct retro_hw_render_context_negotiation_interface *n =
            (struct retro_hw_render_context_negotiation_interface *)data;
         if (negotiation_offer < 2)
            return false;
         n->interface_version = n->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN ? 2 : 0;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
         *(const struct retro_hw_render_interface_vulkan **)data = &iface;
         return true;
      case RETRO_ENVIRONMENT_GET_HDR_METADATA:
      case RETRO_ENVIRONMENT_GET_DISPLAY_METADATA:
         return host_hdr_metadata(cmd, data);
      case RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE:
         if (!up_on) return false;
         /* in retro_load_game, before the context: counted, no interface */
         if (!up_ready) { up_announced++; return false; }
         *(const struct retro_hw_render_upscaler_interface **)data = &up_iface;
         return true;
      default:
         return false;
   }
}

/* The core's device through its negotiation interface, as RetroArch
 * makes it at load and again at each video reinit, and the render
 * interface over it */
static int host_create_device(void)
{
   memset(&vkctx, 0, sizeof(vkctx));
   if (negotiation_offer >= 2)
   {
      /* RetroArch: create_device2 when the core has it */
      if (!negotiation || negotiation->interface_version < 2 || !negotiation->create_device2)
      { fprintf(stderr, "[vkhost] the core did not offer create_device2\n"); return 0; }
      wrapper_calls = 0;
      if (!negotiation->create_device2(&vkctx, instance, gpu, VK_NULL_HANDLE,
               core_gipa, create_device_wrapper, NULL))
      { fprintf(stderr, "[vkhost] core create_device2 failed\n"); return 0; }
      if (wrapper_calls != 1 || wrapper_gpu != gpu || vkctx.gpu != gpu)
      { fprintf(stderr, "[vkhost] the core did not create its device through the frontend (%u)\n", wrapper_calls); return 0; }
   }
   else if (negotiation && negotiation->create_device)
   {
      static const VkPhysicalDeviceFeatures no_features; /* zeroed, as RetroArch passes */
      if (!negotiation->create_device(&vkctx, instance, gpu, VK_NULL_HANDLE,
               core_gipa, NULL, 0, NULL, 0, &no_features))
      { fprintf(stderr, "[vkhost] core create_device failed\n"); return 0; }
   }
   else { fprintf(stderr, "[vkhost] no negotiation interface\n"); return 0; }

   iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
   iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
   iface.handle = NULL;
   iface.instance = instance;
   iface.gpu = vkctx.gpu;
   iface.device = vkctx.device;
   iface.get_device_proc_addr = (PFN_vkGetDeviceProcAddr)core_gipa(instance, "vkGetDeviceProcAddr");
   iface.get_instance_proc_addr = core_gipa;
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
   return 1;
}

/* RetroArch on SET_SYSTEM_AV_INFO: the video driver is reinitialised at
 * once, with nothing in flight: the core's context destroyed, its device
 * destroyed and made again through the negotiation interface, the
 * context reset. The image presented last is gone with the device. */
static void host_reinit_context(void)
{
   host_lock_queue();
   vkDeviceWaitIdle(vkctx.device);
   host_unlock_queue();
   context_live = 0;
   up_readback_free();
   hw_render.context_destroy();
   if (negotiation && negotiation->destroy_device)
      negotiation->destroy_device();
   vkDestroyDevice(vkctx.device, NULL);
   last_image = NULL;
   if (!host_create_device())
   {
      fprintf(stderr, "[vkhost] device recreation failed\n");
      exit(3);
   }
   hw_render.context_reset();
   context_live = 1;
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{ (void)pitch; if (data == RETRO_HW_FRAME_BUFFER_VALID) { frame_valid++;
   if (last_w != width || last_h != height)
      fprintf(stderr, "[vkhost] geometry %ux%u\n", width, height);
   last_w = width; last_h = height;
   if (up_on) up_log_frame(frame_valid, width, height); } }
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
      {
         /* Not GRANITE_VULKAN_DEVICE_INDEX: that one is read by the core's
          * own device creation, which the negotiation interface bypasses -
          * the harness picks the GPU here and hands it to create_device. */
         const char *e  = getenv("VKHOST_DEVICE_INDEX");
         uint32_t    ix = e ? (uint32_t)strtoul(e, NULL, 0) : 0;
         if (ix >= n)
            ix = 0;
         gpu = devs[ix];
      }
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
   host_lock_queue();
   vkDeviceWaitIdle(dev);
   host_unlock_queue();
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
     host_lock_queue();
     vkQueueSubmit(queue, 1, &si, fence);
     host_unlock_queue();
     vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
     vkDestroyFence(dev, fence, NULL); }

   { void *map = NULL; FILE *f; unsigned x, y;
     vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &map);
     { char rawpath[640]; FILE *rf;
       if (!getenv("VKHOST_DUMP_RAW")) rawpath[0] = 0;
       else snprintf(rawpath, sizeof(rawpath), "%s.raw", path);
       rf = rawpath[0] ? fopen(rawpath, "wb") : NULL;
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
                 if (v < 0.0f) v = 0.0f;
                 if (v > 1.0f) v = 1.0f;
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
   const char *next_content = getenv("VKHOST_NEXT_CONTENT");
   if (getenv("VKHOST_NEGOTIATION"))
      negotiation_offer = (unsigned)atoi(getenv("VKHOST_NEGOTIATION"));
   char next_outdir[600];
   char cmdbuf[1024];

   if (argc < 3)
   { fprintf(stderr, "usage: %s core content [state] [frames] [outdir]\n", argv[0]); return 2; }
   core_path = argv[1]; content = argv[2];
   if (argc > 3 && strcmp(argv[3], "-")) state_path = argv[3];
   if (argc > 4) frames = atoi(argv[4]);
   if (argc > 5) outdir = argv[5];
   host_thread = pthread_self();
   if (getenv("VKHOST_SAVE_DIR"))
      snprintf(savedir, sizeof(savedir), "%s", getenv("VKHOST_SAVE_DIR"));
   snprintf(cmdbuf, sizeof(cmdbuf), "mkdir -p %s %s %s", outdir, sysdir, savedir);
   if (system(cmdbuf) != 0)
      fprintf(stderr, "[vkhost] could not create %s\n", outdir);

   /* defaults, overridable via VKHOST_VARS */
   /* Must be a value the option declares; an undeclared value leaves
    * the internal upscale on the CPU side, a state no frontend
    * produces (same defect the GL harness had). */
   add_var("beetle_psx_hw_renderer", "hardware_vk");
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

   up_init();

   core = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!core) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define SYM(name) *(void **)(&name##_fn) = dlsym(core, #name)
session:
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
   {
      /* Every frontend asks for the AV info right after loading; the core
       * applies its renderer options (internal resolution, VRAM view) there. */
      struct retro_system_av_info av;
      void (*retro_get_system_av_info_fn)(struct retro_system_av_info *) =
         dlsym(core, "retro_get_system_av_info");
      if (retro_get_system_av_info_fn)
      {
         retro_get_system_av_info_fn(&av);
         av_max_width  = av.geometry.max_width;
         av_max_height = av.geometry.max_height;
      }
   }

   if (!negotiation && !hw_render.context_reset)
   {
      fprintf(stderr, "[vkhost] software renderer: no Vulkan bring-up\n");
      goto run_frames_sw;
   }
   if (!create_instance()) { fprintf(stderr, "[vkhost] instance failed\n"); return 3; }
   gipa = vkGetInstanceProcAddr;

   if (!host_create_device())
      return 3;

   if (getenv("VKHOST_QUEUE_THREAD") && vkctx.queue)
   {
      queue_thread_run = 1;
      pthread_create(&queue_thread, NULL, queue_thread_main, NULL);
   }

   /* from here the upscaler interface is handed out (a core asks for it
    * in context_reset), until context_destroy */
   up_ready = 1;
   if (hw_render.context_reset) hw_render.context_reset();
   context_live = hw_render.context_reset != NULL;

run_frames_sw:

   if (state_path)
   {
      FILE *f = fopen(state_path, "rb"); long sz; void *data;
      bool (*retro_unserialize_fn)(const void *, size_t) = dlsym(core, "retro_unserialize");
      if (!f) { fprintf(stderr, "[vkhost] cannot open state\n"); return 4; }
      fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
      data = malloc(sz);
      if (!data || fread(data, 1, sz, f) != (size_t)sz)
      { fprintf(stderr, "[vkhost] cannot read state\n"); fclose(f); return 4; }
      fclose(f);
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
      /* VKHOST_BENCH turns the harness into a stopwatch: the periodic PPM
       * dump is suppressed (it does a vkDeviceWaitIdle, a full image copy
       * and a file write every 30 frames, which is most of the wall time
       * at any real frame rate), and the first VKHOST_BENCH_SKIP frames
       * run untimed so pipeline creation and first-use shader compilation
       * stay out of the measurement. */
      int bench      = getenv("VKHOST_BENCH") != NULL;
      int bench_skip = 0;
      struct timespec t0, t1;
      void (*retro_run_fn)(void) = dlsym(core, "retro_run");

      if (bench)
      {
         const char *e = getenv("VKHOST_BENCH_SKIP");
         bench_skip = e ? atoi(e) : 120;
         if (bench_skip >= frames)
            bench_skip = frames / 4;
      }

      typedef bool   (*vk_ser_t)(void *, size_t);
      typedef bool   (*vk_unser_t)(const void *, size_t);
      typedef size_t (*vk_ssize_t)(void);
      vk_ser_t   ser   = (vk_ser_t)dlsym(core, "retro_serialize");
      vk_unser_t unser = (vk_unser_t)dlsym(core, "retro_unserialize");
      vk_ssize_t ssz   = (vk_ssize_t)dlsym(core, "retro_serialize_size");
      int        preempt = getenv("VKHOST_PREEMPT") != NULL;
      /* VKHOST_RELOAD: save a state and load it straight back before
       * every frame. Nothing changes in between, so the output must be
       * exactly what a run without it produces. */
      int        reload  = getenv("VKHOST_RELOAD") != NULL;
      void      *pbuf = NULL, *pbuf2 = NULL;
      size_t     pcap = 0;
      int        have_prev = 0;
      for (i = 0; i < frames; i++)
      {
         cur_frame = i;
         if (preempt && ser && unser && ssz)
         {
            size_t need = ssz();
            if (need > pcap)
            {
               free(pbuf); free(pbuf2);
               pbuf = malloc(need); pbuf2 = malloc(need); pcap = need;
            }
            if (pbuf && pbuf2)
            {
               if (have_prev)
               {
                  if (!unser(pbuf, pcap))
                     fprintf(stderr, "[vkhost] rollback unser FAILED frame %d\n", i);
                  retro_run_fn();   /* replay the previous frame */
               }
               if (!ser(pbuf2, need))
                  fprintf(stderr, "[vkhost] rollback ser FAILED frame %d\n", i);
               { void *t = pbuf; pbuf = pbuf2; pbuf2 = t; }
               have_prev = 1;
            }
         }
         if (reload && ser && unser && ssz)
         {
            size_t need = ssz();
            if (need > pcap)
            {
               free(pbuf); free(pbuf2);
               pbuf = malloc(need); pbuf2 = malloc(need); pcap = need;
            }
            if (!pbuf || !ser(pbuf, need) || !unser(pbuf, need))
               fprintf(stderr, "[vkhost] reload FAILED frame %d\n", i);
         }
         if (bench && i == bench_skip)
         {
            if (vkctx.device)
            {
               host_lock_queue();
               vkDeviceWaitIdle(vkctx.device);
               host_unlock_queue();
            }
            clock_gettime(CLOCK_MONOTONIC, &t0);
         }
         apply_vars_at(i);
         /* VKHOST_LOAD_PAUSE_MS: the time a frontend leaves between loading
          * the content and running it (its menu, a load screen), in which
          * the core's own threads keep working. */
         if (i == 0 && getenv("VKHOST_LOAD_PAUSE_MS"))
         {
            long ms = atol(getenv("VKHOST_LOAD_PAUSE_MS"));
            struct timespec ts;
            ts.tv_sec  = ms / 1000;
            ts.tv_nsec = (ms % 1000) * 1000000L;
            while (nanosleep(&ts, &ts) != 0) ;
         }
         if (i == frames / 2)
         {
            object_creates_half  = object_creates;
            dispatch_groups_half = dispatch_groups;
            queue_submits_half   = __atomic_load_n(&queue_submits, __ATOMIC_SEQ_CST);
         }
         {
            struct timespec f0, f1;
            double ms;
            clock_gettime(CLOCK_MONOTONIC, &f0);
            retro_run_fn();
            clock_gettime(CLOCK_MONOTONIC, &f1);
            ms = (f1.tv_sec - f0.tv_sec) * 1e3 + (f1.tv_nsec - f0.tv_nsec) / 1e6;
            if (frames_started && ms > longest_frame_ms)
            {
               longest_frame_ms = ms;
               longest_frame    = i;
            }
         }
         /* (the first frame makes the render passes, and with them what
          * the pipelines are made for) */
         frames_started = 1;
         {
            /* VKHOST_SAVE_STATE=path:frame - checkpoint mid-run so long
             * cold-boot treks can be chained across invocations. */
            static char sv_path[512]; static int sv_at = -1;
            if (sv_at < 0)
            {
               const char *ss = getenv("VKHOST_SAVE_STATE");
               sv_at = 0;
               if (ss)
               {  const char *c = strrchr(ss, ':');
                  if (c) { sv_at = atoi(c + 1);
                           snprintf(sv_path, sizeof(sv_path), "%.*s",
                                    (int)(c - ss), ss); } }
            }
            if (sv_at && i + 1 == sv_at && sv_path[0])
            {
               size_t (*ssz_fn)(void) = dlsym(core, "retro_serialize_size");
               bool (*ser_fn)(void *, size_t) = dlsym(core, "retro_serialize");
               size_t need = ssz_fn();
               void *sb = malloc(need);
               if (sb && ser_fn(sb, need))
               {  FILE *sf = fopen(sv_path, "wb");
                  if (sf) { fwrite(sb, 1, need, sf); fclose(sf);
                            fprintf(stderr, "[vkhost] state saved %s (%zu) at frame %d\n",
                                    sv_path, need, i + 1); } }
               free(sb);
            }
         }
         { static int dump_iv = 0;
           if (!dump_iv) { const char *e = getenv("VKHOST_DUMP_INTERVAL");
                           dump_iv = e ? atoi(e) : 30; if (dump_iv < 1) dump_iv = 30; }
         if (!bench && vkctx.device && ((i % dump_iv) == dump_iv - 1 || i == frames - 1))
         {
            char path[600];
            snprintf(path, sizeof(path), "%s/frame_%04d.ppm", outdir, i + 1);
            dump_frame(path);
            if (up_on)
               up_dump_images(outdir, i + 1);
         } }
      }

      if (bench)
      {
         double secs;
         /* Charge queued-but-unretired GPU work to the measurement rather
          * than to teardown: wait_sync_index is a no-op in this harness,
          * so the core is free to run ahead of the device. */
         if (vkctx.device)
         {
            host_lock_queue();
            vkDeviceWaitIdle(vkctx.device);
            host_unlock_queue();
         }
         clock_gettime(CLOCK_MONOTONIC, &t1);
         secs = (double)(t1.tv_sec - t0.tv_sec)
              + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
         fprintf(stderr, "[vkhost] BENCH %d frames in %.4f s = %.1f fps\n",
               frames - bench_skip, secs,
               secs > 0.0 ? (double)(frames - bench_skip) / secs : 0.0);
      }
   }

   /* The core's teardown uses the queue as well, so it is counted before
    * the verdict; the queue thread runs until then. */
   up_ready = 0;
   up_readback_free();
   context_live = 0;
   if (hw_render.context_destroy && vkctx.device) hw_render.context_destroy();
   if (queue_thread_run)
   {
      __atomic_store_n(&queue_thread_run, 0, __ATOMIC_SEQ_CST);
      pthread_join(queue_thread, NULL);
   }
   fprintf(stderr, "[vkhost] steady state: %lu Vulkan objects created in the second half of the run\n",
         object_creates - object_creates_half);
   fprintf(stderr, "[vkhost] steady state: %llu compute workgroups dispatched in the second half of the run\n",
         dispatch_groups - dispatch_groups_half);
   fprintf(stderr, "[vkhost] steady state: %lu queue submits in the second half of the run (%d frames, with teardown)\n",
         __atomic_load_n(&queue_submits, __ATOMIC_SEQ_CST) - queue_submits_half, frames - frames / 2);
   fprintf(stderr, "[vkhost] pipelines: %lu built on the frame's thread (%lu of them during play), %lu on the core's own threads\n",
         __atomic_load_n(&pipelines_host, __ATOMIC_SEQ_CST),
         __atomic_load_n(&pipelines_host_running, __ATOMIC_SEQ_CST),
         __atomic_load_n(&pipelines_other, __ATOMIC_SEQ_CST));
   fprintf(stderr, "[vkhost] longest frame after the first: %.2f ms (frame %d)\n",
         longest_frame_ms, longest_frame);
   if (up_on)
      fprintf(stderr, "[vkhost] upscaler: asked for in retro_load_game %d, frames with inputs %ld\n",
            up_announced, up_frames);
   fprintf(stderr, "[vkhost] done: %d frames run, %u valid, %d validation errors, %d warnings,"
           " %d unlocked uses of the frontend's queue, %d waits with its lock held\n",
           frames, frame_valid, validation_errors, validation_warnings,
           queue_violations, queue_waits_locked);
   { void (*f)(void) = dlsym(core, "retro_unload_game"); if (f) f(); }
   { void (*f)(void) = dlsym(core, "retro_deinit"); if (f) f(); }
   if (next_content && !(validation_errors || queue_violations || queue_waits_locked))
   {
      content      = next_content;
      next_content = NULL;
      state_path   = NULL;
      snprintf(next_outdir, sizeof(next_outdir), "%.590s/next", outdir);
      outdir = next_outdir;
      snprintf(cmdbuf, sizeof(cmdbuf), "mkdir -p %s", outdir);
      if (system(cmdbuf) != 0)
         fprintf(stderr, "[vkhost] cannot create %s\n", outdir);
      memset(&hw_render, 0, sizeof(hw_render));
      negotiation = NULL;
      last_image  = NULL;
      frame_valid = 0;
      last_w = last_h = 0;
      fprintf(stderr, "[vkhost] next session: %s\n", content);
      goto session;
   }
   return (validation_errors || queue_violations || queue_waits_locked) ? 5 : 0;
}
