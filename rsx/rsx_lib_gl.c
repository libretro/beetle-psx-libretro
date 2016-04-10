#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <boolean.h>

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include <glsm/glsm.h>
#endif

#include "rsx_intf.h"
#include "rsx.h"

static retro_video_refresh_t rsx_gl_video_cb;
static retro_environment_t   rsx_gl_environ_cb;
extern uint8_t widescreen_hack;
extern uint8_t psx_gpu_upscale_shift;

#if 0
static mut static_renderer: *mut retrogl::RetroGl = 0 as *mut _;
#endif

/* Width of the VRAM in 16bit pixels */
static const uint16_t VRAM_WIDTH_PIXELS = 1024;

/* Height of the VRAM in lines */
static const uint16_t VRAM_HEIGHT = 512;

static bool rsx_gl_is_pal = false;

/* The are a few hardware differences between PAL and NTSC consoles,
 * in particular the pixelclock runs slightly slower on PAL consoles. */
enum VideoClock
{
   Ntsc,
   Pal,
};

static bool fb_ready = false;

static void context_reset(void)
{
   printf("context_reset.\n");
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

   if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
      return;

   fb_ready = true;
}

static void context_destroy(void)
{
}

static bool context_framebuffer_lock(void *data)
{
   if (fb_ready)
      return false;
   return true;
}

void renderer_gl_free(void)
{
#if 0
   if !static_renderer.is_null()
   {
      let _ = Box::from_raw(static_renderer);
      static_renderer = ptr::null_mut();
   }
#endif
}

#if 0
fn set_renderer(renderer: RetroGl) {
    let r = Box::new(renderer);

    drop_renderer();

    unsafe {
        static_renderer = Box::into_raw(r);
    }
}

fn maybe_renderer() -> Option<&'static mut RetroGl> {
    unsafe {
        if static_renderer.is_null() {
            None
        } else {
            Some(&mut *static_renderer)
        }
    }

}

fn renderer() -> &'static mut RetroGl {
    match maybe_renderer() {
        Some(r) => r,
        None => panic!("Attempted to use a NULL renderer"),
    }
}
#endif

void rsx_gl_init(void)
{
#if 0
   static mut first_init: bool = true;

   unsafe {
      if first_init {
         retrolog::init();
         first_init = false;
      }
   }
#endif
}

bool rsx_gl_open(bool is_pal)
{
   glsm_ctx_params_t params = {0};

   params.context_reset         = context_reset;
   params.context_destroy       = context_destroy;
   params.environ_cb            = rsx_gl_environ_cb;
   params.stencil               = false;
   params.imm_vbo_draw          = NULL;
   params.imm_vbo_disable       = NULL;
   params.framebuffer_lock      = context_framebuffer_lock;

   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      return false;

   rsx_gl_is_pal = is_pal;

#if 0
   match RetroGl::new(clock) {
      Ok(r) => {
         set_renderer(r);
         true
      }
      Err(_) => false,
   }
#endif

   return true;
}

void rsx_gl_close(void)
{
#if 0
    drop_renderer();
#endif
}

void rsx_gl_refresh_variables(void)
{
#if 0
   if let Some(renderer) = maybe_renderer()
      renderer.refresh_variables();
#endif
}

void rsx_gl_prepare_frame(void)
{
   if (!fb_ready)
      return;

   glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
#if 0
   renderer().prepare_render();
#endif
}

void rsx_gl_finalize_frame(const void *fb, unsigned width,
      unsigned height, unsigned pitch)
{
   if (!fb_ready)
      return;
  
   rsx_gl_video_cb(RETRO_HW_FRAME_BUFFER_VALID,
         width, height, pitch);

   glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
#if 0
   renderer().finalize_frame();
#endif
}

void rsx_gl_set_environment(retro_environment_t callback)
{
   rsx_gl_environ_cb = callback;
#if 0
    libretro::set_environment(callback);
#endif
}

void rsx_gl_set_video_refresh(retro_video_refresh_t callback)
{
   rsx_gl_video_cb = callback;
#if 0
   libretro::set_video_refresh(callback);
#endif
}

/* Precise FPS values for the video output for the given
 * VideoClock. It's actually possible to configure the PlayStation GPU
 * to output with NTSC timings with the PAL clock (and vice-versa)
 * which would make this code invalid but it wouldn't make a lot of
 * sense for a game to do that.
 */
static float video_output_framerate(void)
{
   /* NTSC - 53.690MHz GPU clock frequency, 263 lines per field,
    * 3413 cycles per line */
   return rsx_gl_is_pal ? 49.76 : 59.81;
}

void rsx_gl_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = video_output_framerate();
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W << psx_gpu_upscale_shift;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H << psx_gpu_upscale_shift;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W << psx_gpu_upscale_shift;
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H << psx_gpu_upscale_shift;
   info->geometry.aspect_ratio = !widescreen_hack ? MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO : (float)16/9;
#if 0
    let info = ptr_as_mut_ref(info).unwrap();

    *info = renderer().get_system_av_info();
#endif
}

/* Draw commands */

void rsx_gl_set_draw_offset(int16_t x, int16_t y)
{
#if 0
   renderer().gl_renderer().set_draw_offset(x as i16, y as i16);
#endif
}

void  rsx_gl_set_draw_area(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h)
{
#if 0
   renderer().gl_renderer().set_draw_area((x as u16, y as u16),
         (w as u16, h as u16));
#endif
}

void rsx_gl_set_display_mode(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h,
      bool depth_24bpp)
{
#if 0
   renderer().gl_renderer().set_display_mode((x as u16, y as u16),
         (w as u16, h as u16),
         depth_24bpp);
#endif
}

void rsx_gl_push_triangle(
      int16_t p0x,
      int16_t p0y,
      int16_t p1x,
      int16_t p1y,
      int16_t p2x,
      int16_t p2y,
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
      bool dither)
{
#if 0
   let texture_page = [texpage_x as u16, texpage_y as u16];
   let clut = [clut_x as u16, clut_y as u16];
   let depth_shift = depth_shift as u8;
   let texture_blend_mode = texture_blend_mode as u8;

   let v = [
      CommandVertex {
position: [p0x as i16, p0y as i16],
          color: [c0 as u8, (c0 >> 8) as u8, (c0 >> 16) as u8],
          texture_coord: [t0x as u16, t0y as u16],
          texture_page: texture_page,
          clut: clut,
          texture_blend_mode: texture_blend_mode,
          depth_shift: depth_shift,
          dither: dither as u8,
      },
                    CommandVertex {
position: [p1x as i16, p1y as i16],
          color: [c1 as u8, (c1 >> 8) as u8, (c1 >> 16) as u8],
          texture_coord: [t1x as u16, t1y as u16],
          texture_page: texture_page,
          clut: clut,
          texture_blend_mode: texture_blend_mode,
          depth_shift: depth_shift,
          dither: dither as u8,
                    },
                    CommandVertex {
position: [p2x as i16, p2y as i16],
          color: [c2 as u8, (c2 >> 8) as u8, (c2 >> 16) as u8],
          texture_coord: [t2x as u16, t2y as u16],
          texture_page: texture_page,
          clut: clut,
          texture_blend_mode: texture_blend_mode,
          depth_shift: depth_shift,
          dither: dither as u8,
                    }];

   renderer().gl_renderer().push_triangle(&v);
#endif
}

void rsx_gl_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
#if 0
   renderer().gl_renderer()
      .fill_rect([color as u8, (color >> 8) as u8, (color >> 16) as u8],
            (x as u16, y as u16),
            (w as u16, h as u16));
#endif
}

void rsx_gl_copy_rect(
      uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h)
{
#if 0
    renderer().gl_renderer()
        .copy_rect((src_x as u16, src_y as u16),
                   (dst_x as u16, dst_y as u16),
                   (w as u16, h as u16));
#endif
}

void rsx_gl_push_line(int16_t p0x,
      int16_t p0y,
      int16_t p1x,
      int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither)
{
#if 0
    let v = [
        CommandVertex {
            position: [p0x as i16, p0y as i16],
            color: [c0 as u8, (c0 >> 8) as u8, (c0 >> 16) as u8],
            texture_coord: [0, 0],
            texture_page: [0, 0],
            clut: [0, 0],
            texture_blend_mode: 0,
            depth_shift: 0,
            dither: dither as u8,
        },
        CommandVertex {
            position: [p1x as i16, p1y as i16],
            color: [c1 as u8, (c1 >> 8) as u8, (c1 >> 16) as u8],
            texture_coord: [0, 0],
            texture_page: [0, 0],
            clut: [0, 0],
            texture_blend_mode: 0,
            depth_shift: 0,
            dither: dither as u8,
        }];

    renderer().gl_renderer().push_line(&v);
#endif
}


void rsx_gl_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      const uint16_t *vram)
{
#if 0
   let vram = unsafe {
      ::std::slice::from_raw_parts(vram as *const u16, 1024 * 512)
   };

   renderer().gl_renderer().upload_vram_window((x as u16, y as u16),
         (w as u16, h as u16),
         vram).unwrap();
#endif
}

#if 0
libretro_variables!(
    struct CoreVariables (prefix = "beetle_psx") {
        internal_resolution: u32, parse_upscale
            => "Internal upscaling factor; \
                1x (native)|2x|3x|4x|5x|6x|7x|8x|9x|10x|11x|12x",
        internal_color_depth: u8, parse_color_depth
            => "Internal color depth; dithered 16bpp (native)|32bpp",
        scale_dither: bool, parse_bool
            => "Scale dithering pattern with internal resolution; \
                enabled|disabled",
        wireframe: bool, parse_bool
            => "Wireframe mode; disabled|enabled",
        bios_menu: bool, parse_bool
            => "Boot to BIOS menu; disabled|enabled",
        display_internal_fps: bool, parse_bool
            => "Display internal FPS; disabled|enabled"
    });

fn parse_upscale(opt: &str) -> Result<u32, <u32 as FromStr>::Err> {
    let num = opt.trim_matches(|c: char| !c.is_numeric());

    num.parse()
}

fn parse_color_depth(opt: &str) -> Result<u8, <u8 as FromStr>::Err> {
    let num = opt.trim_matches(|c: char| !c.is_numeric());

    num.parse()
}

fn parse_bool(opt: &str) -> Result<bool, ()> {
    match opt {
        "true" | "enabled" | "on" => Ok(true),
        "false" | "disabled" | "off" => Ok(false),
        _ => Err(()),
    }
}
#endif


#if 0
fn get_av_info(std: VideoClock, upscaling: u32) -> libretro::SystemAvInfo {

    // Maximum resolution supported by the PlayStation video
    // output is 640x480
    let max_width = (640 * upscaling) as c_uint;
    let max_height = (480 * upscaling) as c_uint;

    libretro::SystemAvInfo {
        geometry: libretro::GameGeometry {
            // The base resolution will be overriden using
            // ENVIRONMENT_SET_GEOMETRY before rendering a frame so
            // this base value is not really important
            base_width: max_width,
            base_height: max_height,
            max_width: max_width,
            max_height: max_height,
            aspect_ratio: 4./3.,
        },
        timing: libretro::SystemTiming {
            fps: video_output_framerate(std) as f64,
            sample_rate: 44_100.
        }
    }
}
#endif

void rsx_gl_set_blend_mode(enum blending_modes mode)
{
   if (!fb_ready)
      return;

   switch (mode)
   {
      /* 0.5xB + 0.5 x F */
      case BLEND_MODE_AVERAGE:
         glBlendEquation(GL_FUNC_ADD);
         glBlendColor(1.0, 1.0, 1.0, 0.5);
         glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
         break;
      case BLEND_MODE_ADD:
         /* 1.0xB + 1.0 x F */
         glBlendEquation(GL_FUNC_ADD);
         glBlendFunc(GL_ONE, GL_ONE);
         break;
      case BLEND_MODE_SUBTRACT:
         /* 1.0xB - 1.0 x F */
         glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
         glBlendFunc(GL_ONE, GL_ONE);
         break;
      case BLEND_MODE_ADD_FOURTH:
         /* 1.0xB + 0.25 x F */
         glBlendEquation(GL_FUNC_ADD);
         glBlendColor(1.0, 1.0, 1.0, 0.25);
         glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
         break;
   }
}
