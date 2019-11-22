#include "rsx_dump.h"
#include <stdio.h>

static FILE *file;

enum
{
   RSX_END = 0,
   RSX_PREPARE_FRAME,
   RSX_FINALIZE_FRAME,
   RSX_TEX_WINDOW,
   RSX_DRAW_OFFSET,
   RSX_DRAW_AREA,
   RSX_HORIZONTAL_RANGE,
   RSX_VERTICAL_RANGE,
   RSX_DISPLAY_MODE,
   RSX_TRIANGLE,
   RSX_QUAD,
   RSX_LINE,
   RSX_LOAD_IMAGE,
   RSX_FILL_RECT,
   RSX_COPY_RECT,
   RSX_TOGGLE_DISPLAY
};

static void write_u32(uint32_t value)
{
   fwrite(&value, sizeof(value), 1, file);
}

static void write_f32(float value)
{
   fwrite(&value, sizeof(value), 1, file);
}

static void write_u16(const uint16_t *values, unsigned w, unsigned h)
{
   for (unsigned y = 0; y < h; y++)
      fwrite(values + y * 1024, sizeof(uint16_t), w, file);
}

static void write_i32(int32_t value)
{
   fwrite(&value, sizeof(value), 1, file);
}

static void rsx_dump_vertex(const rsx_dump_vertex &vertex)
{
   write_f32(vertex.x);
   write_f32(vertex.y);
   write_f32(vertex.w);
   write_u32(vertex.color);
   write_u32(vertex.tx);
   write_u32(vertex.ty);
}

static void rsx_dump_state(const rsx_render_state &state)
{
   write_u32(state.texpage_x);
   write_u32(state.texpage_y);
   write_u32(state.clut_x);
   write_u32(state.clut_y);
   write_u32(state.texture_blend_mode);
   write_u32(state.depth_shift);
   write_u32(state.dither);
   write_u32(state.blend_mode);
   write_u32(state.mask_test);
   write_u32(state.set_mask);
}

void rsx_dump_init(const char *path)
{
   if (file)
      return;

   file = fopen(path, "wb");
   if (file)
      fwrite("RSXDUMP2", 8, 1, file);
}

void rsx_dump_deinit(void)
{
   if (!file)
      return;
   write_u32(RSX_END);
   fclose(file);
   file = NULL;
}

void rsx_dump_prepare_frame(void)
{
   if (!file)
      return;
   write_u32(RSX_PREPARE_FRAME);
}

void rsx_dump_finalize_frame(void)
{
   if (!file)
      return;
   write_u32(RSX_FINALIZE_FRAME);
}

void rsx_dump_set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx, uint8_t twy)
{
   if (!file)
      return;
   write_u32(RSX_TEX_WINDOW);
   write_u32(tww);
   write_u32(twh);
   write_u32(twx);
   write_u32(twy);
}

void rsx_dump_set_draw_offset(int16_t x, int16_t y)
{
   if (!file)
      return;
   write_u32(RSX_DRAW_OFFSET);
   write_i32(x);
   write_i32(y);
}

void rsx_dump_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
   if (!file)
      return;
   write_u32(RSX_DRAW_AREA);
   write_u32(x0);
   write_u32(y0);
   write_u32(x1);
   write_u32(y1);
}

void rsx_dump_set_horizontal_display_range(uint16_t x1, uint16_t x2);
{
   if (!file)
      return;
   write_u32(RSX_HORIZONTAL_RANGE);
   write_u32(x1);
   write_u32(x2);
}

void rsx_dump_set_vertical_display_range(uint16_t y1, uint16_t y2);
{
   if (!file)
      return;
   write_u32(RSX_VERTICAL_RANGE);
   write_u32(y1);
   write_u32(y2);
}

void rsx_dump_set_display_mode(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool depth_24bpp, bool is_pal, bool is_480i, int width_mode)
{
   if (!file)
      return;
   write_u32(RSX_DISPLAY_MODE);
   write_u32(x);
   write_u32(y);
   write_u32(w);
   write_u32(h);
   write_u32(depth_24bpp);
   write_u32(is_pal);
   write_u32(is_480i);
   write_u32(width_mode);
}

void rsx_dump_triangle(const struct rsx_dump_vertex *vertices, const struct rsx_render_state *state)
{
   if (!file)
      return;
   write_u32(RSX_TRIANGLE);
   for (unsigned i = 0; i < 3; i++)
      rsx_dump_vertex(vertices[i]);
   rsx_dump_state(*state);
}

void rsx_dump_quad(const struct rsx_dump_vertex *vertices, const struct rsx_render_state *state)
{
   if (!file)
      return;
   write_u32(RSX_QUAD);
   for (unsigned i = 0; i < 4; i++)
      rsx_dump_vertex(vertices[i]);
   rsx_dump_state(*state);
}

void rsx_dump_line(const struct rsx_dump_line_data *line)
{
   if (!file)
      return;
   write_u32(RSX_LINE);
   write_i32(line->x0);
   write_i32(line->y0);
   write_i32(line->x1);
   write_i32(line->y1);
   write_u32(line->c0);
   write_u32(line->c1);
   write_u32(line->dither);
   write_u32(line->blend_mode);
   write_u32(line->mask_test);
   write_u32(line->set_mask);
}

void rsx_dump_load_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *vram, bool mask_test, bool set_mask)
{
   if (!file)
      return;
   write_u32(RSX_LOAD_IMAGE);
   write_u32(x);
   write_u32(y);
   write_u32(w);
   write_u32(h);
   write_u32(mask_test);
   write_u32(set_mask);
   write_u16(vram + y * 1024 + x, w, h);
}

void rsx_dump_fill_rect(uint32_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
   if (!file)
      return;
   write_u32(RSX_FILL_RECT);
   write_u32(color);
   write_u32(x);
   write_u32(y);
   write_u32(w);
   write_u32(h);
}

void rsx_dump_copy_rect(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h, bool mask_test, bool set_mask)
{
   if (!file)
      return;
   write_u32(RSX_COPY_RECT);
   write_u32(src_x);
   write_u32(src_y);
   write_u32(dst_x);
   write_u32(dst_y);
   write_u32(w);
   write_u32(h);
   write_u32(mask_test);
   write_u32(set_mask);
}

void rsx_dump_toggle_display(bool status)
{
   if (!file)
      return;
   write_u32(RSX_TOGGLE_DISPLAY);
   write_u32(status);
}
