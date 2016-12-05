#include "rsx_lib_vulkan.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h> /* exit() */
#include <boolean.h>
#include "renderer.hpp"
#include "libretro_vulkan.h"
#include <vector>
#include <functional>



using namespace Vulkan;
using namespace PSX;
using namespace std;

static Context *context;
static Device *device;
static Renderer *renderer;
static bool video_is_pal;
static unsigned scaling = 4;

static retro_hw_render_callback hw_render;
static const struct retro_hw_render_interface_vulkan *vulkan;
static vector<retro_vulkan_image> swapchain_images;
static unsigned swapchain_index;
static Renderer::SaveState save_state;
static bool inside_frame;
static bool has_software_fb;
static bool adaptive_smoothing;
static bool widescreen_hack;
static vector<function<void ()>> defer;

static retro_video_refresh_t video_refresh_cb;

void rsx_vulkan_init(void)
{
}

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

static void context_reset(void)
{
   if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
   {
      return;
   }

   if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
   {
      vulkan = nullptr;
      return;
   }

   Context::init_loader(vulkan->get_instance_proc_addr);
   context = new Context(vulkan->instance, vulkan->gpu, vulkan->device, vulkan->queue, vulkan->queue_index);
   device = new Device;
   device->set_context(*context);

   unsigned num_images = 0;
   uint32_t mask = vulkan->get_sync_index_mask(vulkan->handle);
   for (unsigned i = 0; i < 32; i++)
      if (mask & (1u << i))
         num_images = i + 1;

   device->init_virtual_swapchain(num_images);
   swapchain_images.resize(num_images);
   renderer = new Renderer(*device, scaling, save_state.vram.empty() ? nullptr : &save_state);

   for (auto &func : defer)
      func();
   defer.clear();

   renderer->flush();
}

static void context_destroy(void)
{
   save_state = renderer->save_vram_state();

   vulkan = nullptr;
   delete renderer;
   delete device;
   delete context;
   renderer = nullptr;
   device = nullptr;
   context = nullptr;
}

bool rsx_vulkan_open(bool is_pal)
{
   video_is_pal = is_pal;

   hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
   hw_render.version_major = VK_MAKE_VERSION(1, 0, 32);
   hw_render.version_minor = 0;
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.cache_context = false;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,

      get_application_info,
      nullptr,
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

   return true;
}

void rsx_vulkan_close(void)
{
}

void rsx_vulkan_refresh_variables(void)
{
    struct retro_variable var = {0};
    var.key = option_renderer_software_fb;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
       if (!strcmp(var.value, "enabled"))
          has_software_fb = true;
       else
          has_software_fb = false;
    }

    unsigned old_scaling = scaling;
    var.key = option_internal_resolution;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        /* Same limitations as libretro.cpp */
        scaling = var.value[0] - '0';
    }

    var.key = option_adaptive_smoothing;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (!strcmp(var.value, "enabled"))
           adaptive_smoothing = true;
        else
           adaptive_smoothing = false;
    }
    
    var.key = option_widescreen_hack;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (!strcmp(var.value, "enabled"))
            widescreen_hack = true;
        else
            widescreen_hack = false;
    }

    if (old_scaling != scaling && renderer)
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

bool rsx_vulkan_has_software_renderer(void)
{
   return has_software_fb;
}

void rsx_vulkan_prepare_frame(void)
{
   inside_frame = true;
   unsigned num_images = 0;
   uint32_t mask = vulkan->get_sync_index_mask(vulkan->handle);
   for (unsigned i = 0; i < 32; i++)
      if (mask & (1u << i))
         num_images = i + 1;

   device->flush_frame();

   if (device->get_num_swapchain_images() != num_images)
   {
      device->init_virtual_swapchain(num_images);
      swapchain_images.resize(num_images);
   }

   vulkan->wait_sync_index(vulkan->handle);
   unsigned index = vulkan->get_sync_index(vulkan->handle);
   swapchain_index = index;
   device->begin_frame(index);
   renderer->reset_counters();
}

void rsx_vulkan_finalize_frame(const void *fb, unsigned width,
      unsigned height, unsigned pitch)
{
   (void)fb;
   (void)width;
   (void)height;
   (void)pitch;

   renderer->set_adaptive_smoothing(adaptive_smoothing);
   auto scanout = renderer->scanout_to_texture();

   auto &image = swapchain_images[swapchain_index];
   image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   image.create_info.format = scanout->get_format();
   image.create_info.subresourceRange.baseMipLevel = 0;
   image.create_info.subresourceRange.baseArrayLayer = 0;
   image.create_info.subresourceRange.levelCount = 1;
   image.create_info.subresourceRange.layerCount = 1;
   image.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   image.create_info.components.r = VK_COMPONENT_SWIZZLE_R;
   image.create_info.components.g = VK_COMPONENT_SWIZZLE_G;
   image.create_info.components.b = VK_COMPONENT_SWIZZLE_B;
   image.create_info.components.a = VK_COMPONENT_SWIZZLE_A;
   image.create_info.image = scanout->get_image();
   image.image_layout = scanout->get_layout();
   image.image_view = scanout->get_view().get_view();

   vulkan->set_image(vulkan->handle, &image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
   renderer->flush();

   auto semaphore = device->request_semaphore();
   vulkan->set_signal_semaphore(vulkan->handle, semaphore->get_semaphore());
   renderer->set_scanout_semaphore(semaphore);
   video_refresh_cb(RETRO_HW_FRAME_BUFFER_VALID, scanout->get_width(), scanout->get_height(), 0);
   inside_frame = false;

   //fprintf(stderr, "Render passes: %u, Readback: %u, Writeout: %u\n",
   //      renderer->counters.render_passes, renderer->counters.fragment_readback_pixels, renderer->counters.fragment_writeout_pixels);
}

void rsx_vulkan_set_environment(retro_environment_t callback)
{
   environ_cb = callback;
}

void rsx_vulkan_set_video_refresh(retro_video_refresh_t callback)
{
   video_refresh_cb = callback;
}

void rsx_vulkan_get_system_av_info(struct retro_system_av_info *info)
{
   rsx_vulkan_refresh_variables();

   memset(info, 0, sizeof(*info));
   info->geometry.base_width = 320;
   info->geometry.base_height = 240;
   info->geometry.max_width = 640 * scaling;
   info->geometry.max_height = 480 * scaling;
   info->timing.sample_rate = 44100.0;

   info->geometry.aspect_ratio = widescreen_hack ? 16.0 / 9.0 : 4.0 / 3.0;
   if (video_is_pal)
      info->timing.fps = 49.76;
   else
      info->timing.fps = 59.941;
}

/* Draw commands */

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

void rsx_vulkan_set_tex_window(uint8_t tww, uint8_t twh,
      uint8_t twx, uint8_t twy)
{
   auto tex_x_mask = ~(tww << 3);
   auto tex_y_mask = ~(twh << 3);
   auto tex_x_or = (twx & tww) << 3;
   auto tex_y_or = (twy & twh) << 3;

   if (renderer)
      renderer->set_texture_window({ uint8_t(tex_x_mask), uint8_t(tex_y_mask), uint8_t(tex_x_or), uint8_t(tex_y_or) });
   else
   {
      defer.push_back([=]() {
            renderer->set_texture_window({ uint8_t(tex_x_mask), uint8_t(tex_y_mask), uint8_t(tex_x_or), uint8_t(tex_y_or) });
      });
   }
}

void rsx_vulkan_set_draw_area(uint16_t x0,
      uint16_t y0,
      uint16_t x1,
      uint16_t y1)
{
   int width = x1 - x0 + 1;
   int height = y1 - y0 + 1;
   width = max(width, 0);
   height = max(height, 0);

   width = min(width, int(FB_WIDTH - x0));
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

void rsx_vulkan_set_display_mode(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h,
      bool depth_24bpp)
{
   if (renderer)
      renderer->set_display_mode({ x, y, w, h }, depth_24bpp);
   else
   {
      defer.push_back([=]() {
            renderer->set_display_mode({ x, y, w, h }, depth_24bpp);
      });
   }
}

void rsx_vulkan_push_quad(
      float p0x,
      float p0y,
      float p0w,
      float p1x,
      float p1y,
      float p1w,
      float p2x,
      float p2y,
      float p2w,
      float p3x,
      float p3y,
      float p3w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint32_t c3,
      uint16_t t0x,
      uint16_t t0y,
      uint16_t t1x,
      uint16_t t1y,
      uint16_t t2x,
      uint16_t t2y,
      uint16_t t3x,
      uint16_t t3y,
      uint16_t texpage_x,
      uint16_t texpage_y,
      uint16_t clut_x,
      uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode, bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_dither(dither);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
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

void rsx_vulkan_push_triangle(
      float p0x,
      float p0y,
      float p0w,
      float p1x,
      float p1y,
      float p1w,
      float p2x,
      float p2y,
      float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x,
      uint16_t t0y,
      uint16_t t1x,
      uint16_t t1y,
      uint16_t t2x,
      uint16_t t2y,
      uint16_t texpage_x,
      uint16_t texpage_y,
      uint16_t clut_x,
      uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode, bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_dither(dither);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
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

void rsx_vulkan_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
   if (renderer)
      renderer->clear_rect({ x, y, w, h }, color);
}

void rsx_vulkan_copy_rect(
      uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h, bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->blit_vram({ dst_x, dst_y, w, h }, { src_x, src_y, w, h });
}

void rsx_vulkan_push_line(int16_t p0x,
      int16_t p0y,
      int16_t p1x,
      int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode, bool mask_test, bool set_mask)
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
   renderer->set_dither(dither);
   renderer->set_texture_color_modulate(false);
   renderer->draw_line(vertices);
}

void rsx_vulkan_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram, bool mask_test, bool set_mask)
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
   auto handle = renderer->copy_cpu_to_vram({ x, y, w, h });
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
