#ifndef __RSX_INTF_H__
#define __RSX_INTF_H__

#include "libretro.h"

#define SOUND_FREQUENCY 44100
#define FPS_NTSC 59.941
#define FPS_PAL 49.76

enum rsx_renderer_type
{
   RSX_SOFTWARE = 0,
   RSX_OPENGL,
   RSX_VULKAN
};

enum blending_modes
{
   BLEND_MODE_OPAQUE     = -1,
   BLEND_MODE_AVERAGE    =  0,
   BLEND_MODE_ADD        =  1,
   BLEND_MODE_SUBTRACT   =  2,
   BLEND_MODE_ADD_FOURTH =  3
};

enum width_modes
{
   WIDTH_MODE_256 = 0,
   WIDTH_MODE_320,
   WIDTH_MODE_512,
   WIDTH_MODE_640,
   WIDTH_MODE_368
};

void rsx_intf_set_environment(retro_environment_t cb);
void rsx_intf_set_video_refresh(retro_video_refresh_t cb);
void rsx_intf_get_system_av_info(struct retro_system_av_info *info);

bool rsx_intf_open(bool is_pal, bool force_software);
void rsx_intf_close(void);
void rsx_intf_refresh_variables(void);
void rsx_intf_prepare_frame(void);
void rsx_intf_finalize_frame(const void *fb, unsigned width,
                             unsigned height, unsigned pitch);

void rsx_intf_set_tex_window(uint8_t tww, uint8_t twh,
                             uint8_t twx, uint8_t twy);

void rsx_intf_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and);
void rsx_intf_set_draw_offset(int16_t x, int16_t y);
void rsx_intf_set_draw_area(uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1);
void rsx_intf_set_horizontal_display_range(uint16_t x1, uint16_t x2);
void rsx_intf_set_vertical_display_range(uint16_t y1, uint16_t y2);
void rsx_intf_set_display_mode(uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               bool depth_24bpp,
                               bool is_pal,
                               bool is_480i,
                               int width_mode); //enum

void rsx_intf_push_triangle(float p0x, float p0y, float p0w,
                            float p1x, float p1y, float p1w,
                            float p2x, float p2y, float p2w,
                            uint32_t c0, uint32_t c1, uint32_t c2,
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
                            /* This is really an `enum blending_modes`
                             * but I don't want to deal with enums in the
                             * FFI */
                            int blend_mode,
                            uint32_t mask_test,
                            uint32_t set_mask);

void rsx_intf_push_quad(float p0x, float p0y, float p0w,
                        float p1x, float p1y, float p1w,
                        float p2x, float p2y, float p2w,
                        float p3x, float p3y, float p3w,
                        uint32_t c0,
                        uint32_t c1,
                        uint32_t c2,
                        uint32_t c3,
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
                        uint32_t mask_test,
                        uint32_t set_mask);

void rsx_intf_push_line(int16_t p0x, int16_t p0y,
                        int16_t p1x, int16_t p1y,
                        uint32_t c0,
                        uint32_t c1,
                        bool dither,
                        /* This is really an `enum blending_modes`
                         * but I don't want to deal with enums in the FFI */
                        int blend_mode,
                        uint32_t mask_test,
                        uint32_t set_mask);

void rsx_intf_load_image(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint16_t *vram,
                         uint32_t mask_test,
                         uint32_t set_mask);

bool rsx_intf_read_vram(uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        uint16_t *vram);

void rsx_intf_fill_rect(uint32_t color,
                        uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h);

void rsx_intf_copy_rect(uint16_t src_x, uint16_t src_y,
                        uint16_t dst_x, uint16_t dst_y,
                        uint16_t w, uint16_t h, 
                        uint32_t mask_test, uint32_t set_mask);

enum rsx_renderer_type rsx_intf_is_type(void);

void rsx_intf_toggle_display(bool status);

bool rsx_intf_has_software_renderer(void);

#endif /*__RSX_H__ */
