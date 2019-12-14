#ifndef __RSX_LIB_VULKAN_H__
#define __RSX_LIB_VULKAN_H__

#include <stdint.h>
#include "libretro.h"

//void rsx_vulkan_set_blend_mode(enum blending_modes mode);
void rsx_vulkan_set_environment(retro_environment_t cb);
void rsx_vulkan_set_video_refresh(retro_video_refresh_t cb);
void rsx_vulkan_get_system_av_info(struct retro_system_av_info *info);

bool rsx_vulkan_open(bool is_pal);
void rsx_vulkan_close(void);
void rsx_vulkan_refresh_variables(void);
void rsx_vulkan_prepare_frame(void);
void rsx_vulkan_finalize_frame(const void *fb, unsigned width,
                               unsigned height, unsigned pitch);

void rsx_vulkan_set_tex_window(uint8_t tww, uint8_t twh,
                               uint8_t twx, uint8_t twy);

void rsx_vulkan_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and);

void rsx_vulkan_set_draw_offset(int16_t x, int16_t y);

void rsx_vulkan_set_draw_area(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1);

void rsx_vulkan_set_horizontal_display_range(uint16_t x1, uint16_t x2);

void rsx_vulkan_set_vertical_display_range(uint16_t y1, uint16_t y2);

void rsx_vulkan_set_display_mode(uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h,
                                 bool depth_24bpp,
                                 bool is_pal,
                                 bool is_480i,
                                 int width_mode); //enum width_modes

void rsx_vulkan_push_triangle(float p0x, float p0y, float p0w,
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
                              int blend_mode, //enum blending_modes
                              bool mask_test, bool set_mask);

void rsx_vulkan_push_quad(float p0x, float p0y, float p0w,
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
                          int blend_mode, //enum blending_modes
                          bool mask_test, bool set_mask);

void rsx_vulkan_push_line(int16_t p0x, int16_t p0y,
                          int16_t p1x, int16_t p1y,
                          uint32_t c0,
                          uint32_t c1,
                          bool dither,
                          int blend_mode, //enum blending_modes
                          bool mask_test, bool set_mask);

void rsx_vulkan_load_image(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t *vram,
                           bool mask_test, bool set_mask);

bool rsx_vulkan_read_vram(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *vram);

void rsx_vulkan_fill_rect(uint32_t color,
                          uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h);

void rsx_vulkan_copy_rect(uint16_t src_x, uint16_t src_y,
                          uint16_t dst_x, uint16_t dst_y,
                          uint16_t w, uint16_t h, 
                          bool mask_test, bool set_mask);

void rsx_vulkan_toggle_display(bool status);

bool rsx_vulkan_has_software_renderer(void);

#endif /*__RSX_LIB_VULKAN_H__*/
