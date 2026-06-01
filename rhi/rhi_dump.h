#ifndef RHI_DUMP_H
#define RHI_DUMP_H

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

void rhi_dump_init(const char *path);
void rhi_dump_deinit(void);

void rhi_dump_prepare_frame(void);
void rhi_dump_finalize_frame(void);

void rhi_dump_set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx, uint8_t twy);
void rhi_dump_set_draw_offset(int16_t x, int16_t y);
void rhi_dump_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void rhi_dump_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart);
void rhi_dump_set_horizontal_display_range(uint16_t x1, uint16_t x2);
void rhi_dump_set_vertical_display_range(uint16_t y1, uint16_t y2);
void rhi_dump_set_display_mode(bool depth_24bpp, bool is_pal, bool is_480i, int width_mode);

typedef struct rhi_dump_vertex
{
   float x, y, w;
   uint32_t color;
   uint16_t tx, ty;
} rhi_dump_vertex;

typedef struct rhi_render_state
{
   uint16_t texpage_x, texpage_y;
   uint16_t clut_x, clut_y;
   uint8_t texture_blend_mode;
   uint8_t depth_shift;
   bool dither;
   int blend_mode;
   bool mask_test;
   bool set_mask;
} rhi_render_state;

void rhi_dump_triangle(const struct rhi_dump_vertex *vertices, const struct rhi_render_state *state);
void rhi_dump_quad(const struct rhi_dump_vertex *vertices, const struct rhi_render_state *state);

typedef struct rhi_dump_line_data
{
   int16_t x0, y0, x1, y1;
   uint32_t c0, c1;
   bool dither;
   int blend_mode;
   bool mask_test;
   bool set_mask;
} rhi_dump_line_data;

void rhi_dump_line(const struct rhi_dump_line_data *line);
void rhi_dump_load_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
      const uint16_t *vram, bool mask_test, bool set_mask);

void rhi_dump_fill_rect(uint32_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void rhi_dump_copy_rect(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h, bool mask_test, bool set_mask);
void rhi_dump_toggle_display(bool status);

#ifdef __cplusplus
}
#endif

#endif /* RHI_DUMP_H */
