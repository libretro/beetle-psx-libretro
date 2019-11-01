#ifndef RSX_DUMP_H
#define RSX_DUMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rsx_dump_init(const char *path);
void rsx_dump_deinit(void);

void rsx_dump_prepare_frame(void);
void rsx_dump_finalize_frame(void);

void rsx_dump_set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx, uint8_t twy);
void rsx_dump_set_draw_offset(int16_t x, int16_t y);
void rsx_dump_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void rsx_dump_set_horizontal_display_range(uint16_t x1, uint16_t x2);
void rsx_dump_set_vertical_display_range(uint16_t y1, uint16_t y2);
void rsx_dump_set_display_mode(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool depth_24bpp, bool is_pal, bool is_480i, int width_mode);

struct rsx_dump_vertex
{
   float x, y, w;
   uint32_t color;
   uint16_t tx, ty;
};

struct rsx_render_state
{
   uint16_t texpage_x, texpage_y;
   uint16_t clut_x, clut_y;
   uint8_t texture_blend_mode;
   uint8_t depth_shift;
   bool dither;
   int blend_mode;
   bool mask_test;
   bool set_mask;
};

void rsx_dump_triangle(const struct rsx_dump_vertex *vertices, const struct rsx_render_state *state);
void rsx_dump_quad(const struct rsx_dump_vertex *vertices, const struct rsx_render_state *state);

struct rsx_dump_line_data
{
   int16_t x0, y0, x1, y1;
   uint32_t c0, c1;
   bool dither;
   int blend_mode;
   bool mask_test;
   bool set_mask;
};

void rsx_dump_line(const struct rsx_dump_line_data *line);
void rsx_dump_load_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
      const uint16_t *vram, bool mask_test, bool set_mask);

void rsx_dump_fill_rect(uint32_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void rsx_dump_copy_rect(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h, bool mask_test, bool set_mask);
void rsx_dump_toggle_display(bool status);

#ifdef __cplusplus
}
#endif
#endif
