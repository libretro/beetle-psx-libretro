#include "rsx/rsx_lib_vulkan.h"

#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <vector>

#include "rsx/rsx_intf.h" //FPS and audio sample rate macros
#include "parallel-psx/renderer/renderer.hpp"
#include "libretro_vulkan.h"

// #include "mednafen/mednafen.h" is required
// for #include "mednafen/psx/gpu.h" to work properly.
#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"

#include "libretro_cbs.h"
#include "libretro_options.h"

using namespace Vulkan;
using namespace PSX;
using namespace std;

static Context *context;
static Device *device;
static Renderer *renderer;
static unsigned scaling = 4;

// Declare extern as workaround for now to avoid variable
// naming conflicts with beetle_psx_globals.h
extern "C" uint8_t widescreen_hack;
extern "C" bool content_is_pal;
extern "C" int filter_mode;

extern retro_log_printf_t log_cb;
namespace Granite
{
retro_log_printf_t libretro_log;
}

static retro_hw_render_callback hw_render;
static const struct retro_hw_render_interface_vulkan *vulkan;
static retro_vulkan_image swapchain_image;
static Renderer::SaveState save_state;
static bool inside_frame;
static bool has_software_fb;
static bool adaptive_smoothing;
static bool super_sampling;
static unsigned msaa = 1;
static bool mdec_yuv;
static vector<function<void ()>> defer;
static dither_mode dither_mode = DITHER_NATIVE;
static bool crop_overscan;
static int image_offset_cycles;
static int initial_scanline;
static int last_scanline;
static int initial_scanline_pal;
static int last_scanline_pal;

static retro_video_refresh_t video_refresh_cb;

static const VkApplicationInfo *get_application_info(void)
{
   static const VkApplicationInfo info = {
      VK_STRUCTURE_TYPE_APPLICATION_INFO,
      nullptr,
      "Beetle PSX",
      0,
      "parallel-psx",
      0,
      VK_MAKE_VERSION(1, 0, 32),
   };
   return &info;
}

static void vk_context_reset(void)
{
   if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
      return;

   if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
   {
      vulkan = nullptr;
      return;
   }

   assert(context);
   device = new Device;
   device->set_context(*context);

   renderer = new Renderer(*device, scaling, msaa, save_state.vram.empty() ? nullptr : &save_state);

   for (auto &func : defer)
      func();
   defer.clear();

   renderer->flush();
}

static void vk_context_destroy(void)
{
   save_state = renderer->save_vram_state();
   vulkan     = nullptr;

   delete renderer;
   delete device;
   delete context;
   renderer = nullptr;
   device = nullptr;
   context = nullptr;
}

static bool libretro_create_device(
      struct retro_vulkan_context *libretro_context,
      VkInstance instance,
      VkPhysicalDevice gpu,
      VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions,
      unsigned num_required_device_extensions,
      const char **required_device_layers,
      unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
   if (!Vulkan::Context::init_loader(get_instance_proc_addr))
      return false;

   if (context)
   {
      delete context;
      context = nullptr;
   }

   try
   {
      context = new Vulkan::Context(instance, gpu, surface, required_device_extensions, num_required_device_extensions,
                                    required_device_layers, num_required_device_layers,
                                    required_features);
   }
   catch (const std::exception &)
   {
      return false;
   }

   context->release_device();
   libretro_context->gpu = context->get_gpu();
   libretro_context->device = context->get_device();
   libretro_context->presentation_queue = context->get_graphics_queue();
   libretro_context->presentation_queue_family_index = context->get_graphics_queue_family();
   libretro_context->queue = context->get_graphics_queue();
   libretro_context->queue_family_index = context->get_graphics_queue_family();
   return true;
}

bool rsx_vulkan_open(bool is_pal)
{
   Granite::libretro_log = log_cb;
   content_is_pal = is_pal;

   hw_render.context_type    = RETRO_HW_CONTEXT_VULKAN;
   hw_render.version_major   = VK_MAKE_VERSION(1, 0, 32);
   hw_render.version_minor   = 0;
   hw_render.context_reset   = vk_context_reset;
   hw_render.context_destroy = vk_context_destroy;
   hw_render.cache_context   = false;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,

      get_application_info,
      libretro_create_device,
      nullptr,
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

   return true;
}

void rsx_vulkan_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void rsx_vulkan_set_video_refresh(retro_video_refresh_t cb)
{
   video_refresh_cb = cb;
}

void rsx_vulkan_get_system_av_info(struct retro_system_av_info *info)
{
   rsx_vulkan_refresh_variables();

   memset(info, 0, sizeof(*info));
   info->geometry.base_width  = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width   = MEDNAFEN_CORE_GEOMETRY_MAX_W * (super_sampling ? 1 : scaling);
   info->geometry.max_height  = MEDNAFEN_CORE_GEOMETRY_MAX_H * (super_sampling ? 1 : scaling);
   info->timing.sample_rate   = SOUND_FREQUENCY;

   info->geometry.aspect_ratio = !widescreen_hack ? MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO : 16.0 / 9.0;
   if (content_is_pal)
      info->timing.fps = FPS_PAL;
   else
      info->timing.fps = FPS_NTSC;
}

void rsx_vulkan_refresh_variables(void)
{
   struct retro_variable var = {0};

   var.key = BEETLE_OPT(renderer_software_fb);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         has_software_fb = true;
      else
         has_software_fb = false;
   }
   else
      /* If 'BEETLE_OPT(renderer_software_fb)' option is not found, then
       * we are running in software mode */
      has_software_fb = true;

   unsigned old_scaling = scaling;
   unsigned old_msaa = msaa;
   bool old_super_sampling = super_sampling;

   var.key = BEETLE_OPT(internal_resolution);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      /* Same limitations as libretro.cpp */
      scaling = var.value[0] - '0';
      if (var.value[1] != 'x')
      {
         scaling  = (var.value[0] - '0') * 10;
         scaling += var.value[1] - '0';
      }
   }

   var.key = BEETLE_OPT(adaptive_smoothing);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         adaptive_smoothing = true;
      else
         adaptive_smoothing = false;
   }

   var.key = BEETLE_OPT(super_sampling);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         super_sampling = true;
      else
         super_sampling = false;
   }

   var.key = BEETLE_OPT(msaa);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      msaa = strtoul(var.value, nullptr, 0);
   }

   var.key = BEETLE_OPT(mdec_yuv);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         mdec_yuv = true;
      else
         mdec_yuv = false;
   }

   var.key = BEETLE_OPT(dither_mode);
   dither_mode = DITHER_NATIVE;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "internal resolution"))
         dither_mode = DITHER_UPSCALED;
      else if (!strcmp(var.value, "disabled"))
         dither_mode = DITHER_OFF;
   }

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         crop_overscan = true;
      else
         crop_overscan = false;
   }

   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline_pal = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline_pal = atoi(var.value);
   }

   var.key = BEETLE_OPT(widescreen_hack);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         widescreen_hack = true;
      else
         widescreen_hack = false;
   }

   // Changing crop_overscan and scanlines will likely need to be included here in future geometry fixes
   if ((old_scaling != scaling || old_super_sampling != super_sampling || old_msaa != msaa) && renderer)
   {
      retro_system_av_info info;
      rsx_vulkan_get_system_av_info(&info);

      if (!environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info))
      {
         // Failed to change scale, just keep using the old one.
         scaling = old_scaling;
      }
   }
}

void rsx_vulkan_prepare_frame(void)
{
   inside_frame = true;
   device->flush_frame();
   vulkan->wait_sync_index(vulkan->handle);
   unsigned index = vulkan->get_sync_index(vulkan->handle);
   device->next_frame_context();
   renderer->reset_counters();

   renderer->set_filter_mode(static_cast<Renderer::FilterMode>(filter_mode));
}

static Renderer::ScanoutMode get_scanout_mode(bool bpp24)
{
   if (bpp24)
      return Renderer::ScanoutMode::BGR24;
   else if (dither_mode != DITHER_OFF)
      return Renderer::ScanoutMode::ABGR1555_Dither;
   else
      return Renderer::ScanoutMode::ABGR1555_555;
}

void rsx_vulkan_finalize_frame(const void *fb, unsigned width,
                               unsigned height, unsigned pitch)
{
   renderer->set_adaptive_smoothing(adaptive_smoothing);
   renderer->set_dither_native_resolution(dither_mode == DITHER_NATIVE);
   renderer->set_horizontal_overscan_cropping(crop_overscan);
   renderer->set_horizontal_offset_cycles(image_offset_cycles);
   renderer->set_visible_scanlines(initial_scanline, last_scanline, initial_scanline_pal, last_scanline_pal);

   if (renderer->get_scanout_mode() == Renderer::ScanoutMode::BGR24)
      renderer->set_display_filter(mdec_yuv ? Renderer::ScanoutFilter::MDEC_YUV : Renderer::ScanoutFilter::None);
   else
      renderer->set_display_filter(super_sampling ? Renderer::ScanoutFilter::SSAA : Renderer::ScanoutFilter::None);

   auto scanout = renderer->scanout_to_texture();

   retro_vulkan_image *image                          = &swapchain_image;

   image->create_info.sType                           = 
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   image->create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
   image->create_info.format                          = scanout->get_format();
   image->create_info.subresourceRange.baseMipLevel   = 0;
   image->create_info.subresourceRange.baseArrayLayer = 0;
   image->create_info.subresourceRange.levelCount     = 1;
   image->create_info.subresourceRange.layerCount     = 1;
   image->create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
   image->create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
   image->create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
   image->create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
   image->create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
   image->create_info.image                           = scanout->get_image();
   image->image_layout                                = 
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   image->image_view                                  = 
      scanout->get_view().get_view();

   vulkan->set_image(vulkan->handle, image, 0,
         nullptr, VK_QUEUE_FAMILY_IGNORED);
   renderer->flush();

   auto semaphore = device->request_semaphore();
   vulkan->set_signal_semaphore(vulkan->handle, semaphore->get_semaphore());
   semaphore->signal_external();
   renderer->set_scanout_semaphore(semaphore);
   video_refresh_cb(RETRO_HW_FRAME_BUFFER_VALID, scanout->get_width(), scanout->get_height(), 0);
   inside_frame = false;

   //fprintf(stderr, "Render passes: %u, Readback: %u, Writeout: %u\n",
   //      renderer->counters.render_passes, renderer->counters.fragment_readback_pixels, renderer->counters.fragment_writeout_pixels);
}

/* Draw commands */


void rsx_vulkan_set_tex_window(uint8_t tww, uint8_t twh,
                               uint8_t twx, uint8_t twy)
{
   uint8_t tex_x_mask = ~(tww << 3);
   uint8_t tex_y_mask = ~(twh << 3);
   uint8_t tex_x_or   = (twx & tww) << 3;
   uint8_t tex_y_or   = (twy & twh) << 3;

   if (renderer)
      renderer->set_texture_window({ tex_x_mask, tex_y_mask, tex_x_or, tex_y_or });
   else
   {
      defer.push_back([=]() {
            renderer->set_texture_window({ tex_x_mask, tex_y_mask, tex_x_or, tex_y_or});
            });
   }
}

void rsx_vulkan_set_draw_offset(int16_t x, int16_t y)
{
   if (renderer)
      renderer->set_draw_offset(x, y);
   else
   {
      defer.push_back([=]() {
            renderer->set_draw_offset(x, y);
            });
   }
}

void rsx_vulkan_set_draw_area(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
   int width  = x1 - x0 + 1;
   int height = y1 - y0 + 1;
   width  = max(width, 0);
   height = max(height, 0);

   width  = min(width, int(FB_WIDTH - x0));
   height = min(height, int(FB_HEIGHT - y0));

   if (renderer)
      renderer->set_draw_rect({ x0, y0, unsigned(width), unsigned(height) });
   else
   {
      defer.push_back([=]() {
            renderer->set_draw_rect({ x0, y0, unsigned(width), unsigned(height) });
            });
   }
}

void rsx_vulkan_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   if (renderer)
      renderer->set_horizontal_display_range(x1, x2);
   else
   {
      defer.push_back([=]() {
         renderer->set_horizontal_display_range(x1, x2);
      });
   }
}

void rsx_vulkan_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   if (renderer)
      renderer->set_vertical_display_range(y1, y2);
   else
   {
      defer.push_back([=]() {
         renderer->set_vertical_display_range(y1, y2);
      });
   }
}

void rsx_vulkan_set_display_mode(uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h,
                                 bool depth_24bpp,
                                 bool is_pal,
                                 bool is_480i,
                                 int width_mode)
{
   if (renderer)
      renderer->set_display_mode({ x, y, w, h }, get_scanout_mode(depth_24bpp), is_pal,
                                 is_480i, static_cast<Renderer::WidthMode>(width_mode));
   else
   {
      defer.push_back([=]() {
            renderer->set_display_mode({ x, y, w, h }, get_scanout_mode(depth_24bpp), is_pal,
                                       is_480i, static_cast<Renderer::WidthMode>(width_mode));
            });
   }
}

void rsx_vulkan_push_triangle(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   //renderer->set_dither(dither);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode::ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode::Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode::Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode::None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   Vertex vertices[3] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
   };

   renderer->draw_triangle(vertices);
}

void rsx_vulkan_push_quad(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      float p3x, float p3y, float p3w,
      uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
      uint16_t t0x, uint16_t t0y, 
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t t3x, uint16_t t3y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   //renderer->set_dither(dither);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode::ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode::Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode::Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode::None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   Vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };

   renderer->draw_quad(vertices);
}

void rsx_vulkan_push_line(
      int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_mode(TextureMode::None);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   Vertex vertices[2] = {
      { float(p0x), float(p0y), 1.0f, c0, 0, 0 },
      { float(p1x), float(p1y), 1.0f, c1, 0, 0 },
   };
   //renderer->set_dither(dither);
   renderer->set_texture_color_modulate(false);
   renderer->draw_line(vertices);
}

void rsx_vulkan_load_image(
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram,
      bool mask_test, bool set_mask)
{
   if (!renderer)
   {
      // Generally happens if someone loads a save state before the Vulkan context is created.
      defer.push_back([=]() {
            rsx_vulkan_load_image(x, y, w, h, vram, mask_test, set_mask);
      });
      return;
   }

   bool dual_copy = x + w > FB_WIDTH; // Check if we need to handle wrap-around in X.
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   auto handle   = renderer->copy_cpu_to_vram({ x, y, w, h });
   uint16_t *tmp = renderer->begin_copy(handle);
   for (unsigned off_y = 0; off_y < h; off_y++)
   {
      if (dual_copy)
      {
         unsigned first = FB_WIDTH - x;
         unsigned second = w - first;
         memcpy(tmp + off_y * w, vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH + x, first * sizeof(uint16_t));
         memcpy(tmp + off_y * w + first,
               vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH,
               second * sizeof(uint16_t));
      }
      else
      {
         memcpy(tmp + off_y * w,
               vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH + x,
               w * sizeof(uint16_t));
      }
   }
   renderer->end_copy(handle);

   // This is called on state loading. 
   if (!inside_frame)
      renderer->flush();
}

bool rsx_vulkan_read_vram(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h,
                          uint16_t *vram)
{
   if (!renderer)
      return false;

   renderer->copy_vram_to_cpu_synchronous({ x, y, w, h }, vram);
   return true;
}

void rsx_vulkan_fill_rect(uint32_t color,
                          uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
   if (renderer)
      renderer->clear_rect({ x, y, w, h }, color);
}

void rsx_vulkan_copy_rect(uint16_t src_x, uint16_t src_y,
                          uint16_t dst_x, uint16_t dst_y,
                          uint16_t w, uint16_t h, 
                          bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->blit_vram({ dst_x, dst_y, w, h }, { src_x, src_y, w, h });
}

void rsx_vulkan_toggle_display(bool status)
{
   if (renderer)
      renderer->toggle_display(status == 0);
   else
   {
      defer.push_back([=] {
            renderer->toggle_display(status == 0);
      });
   }
}

bool rsx_vulkan_has_software_renderer(void)
{
   return has_software_fb;
}
