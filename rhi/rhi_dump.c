#include <stdio.h>
#include <stddef.h>

#include "rhi_dump.h"

static FILE *file;

enum
{
   RHI_END = 0,
   RHI_PREPARE_FRAME,
   RHI_FINALIZE_FRAME,
   RHI_TEX_WINDOW,
   RHI_DRAW_OFFSET,
   RHI_DRAW_AREA,
   RHI_VRAM_COORDS,
   RHI_HORIZONTAL_RANGE,
   RHI_VERTICAL_RANGE,
   RHI_DISPLAY_MODE,
   RHI_TRIANGLE,
   RHI_QUAD,
   RHI_LINE,
   RHI_LOAD_IMAGE,
   RHI_FILL_RECT,
   RHI_COPY_RECT,
   RHI_TOGGLE_DISPLAY
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
   unsigned y;
   for (y = 0; y < h; y++)
      fwrite(values + y * 1024, sizeof(uint16_t), w, file);
}

static void write_i32(int32_t value)
{
   fwrite(&value, sizeof(value), 1, file);
}

static void rhi_dump_vertex_write(const rhi_dump_vertex *vertex)
{
   write_f32(vertex->x);
   write_f32(vertex->y);
   write_f32(vertex->w);
   write_u32(vertex->color);
   write_u32(vertex->tx);
   write_u32(vertex->ty);
}

static void rhi_dump_state_write(const rhi_render_state *state)
{
   write_u32(state->texpage_x);
   write_u32(state->texpage_y);
   write_u32(state->clut_x);
   write_u32(state->clut_y);
   write_u32(state->texture_blend_mode);
   write_u32(state->depth_shift);
   write_u32(state->dither);
   write_u32(state->blend_mode);
   write_u32(state->mask_test);
   write_u32(state->set_mask);
}

void rhi_dump_init(const char *path)
{
   if (file)
      return;

   file = fopen(path, "wb");
   if (file)
      fwrite("RSXDUMP3", 8, 1, file);
}

void rhi_dump_deinit(void)
{
   if (!file)
      return;
   write_u32(RHI_END);
   fclose(file);
   file = NULL;
}

void rhi_dump_prepare_frame(void)
{
   if (!file)
      return;
   write_u32(RHI_PREPARE_FRAME);
}

void rhi_dump_finalize_frame(void)
{
   if (!file)
      return;
   write_u32(RHI_FINALIZE_FRAME);
}

void rhi_dump_set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx, uint8_t twy)
{
   if (!file)
      return;
   write_u32(RHI_TEX_WINDOW);
   write_u32(tww);
   write_u32(twh);
   write_u32(twx);
   write_u32(twy);
}

void rhi_dump_set_draw_offset(int16_t x, int16_t y)
{
   if (!file)
      return;
   write_u32(RHI_DRAW_OFFSET);
   write_i32(x);
   write_i32(y);
}

void rhi_dump_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
   if (!file)
      return;
   write_u32(RHI_DRAW_AREA);
   write_u32(x0);
   write_u32(y0);
   write_u32(x1);
   write_u32(y1);
}

void rhi_dump_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
   if (!file)
      return;
   write_u32(RHI_VRAM_COORDS);
   write_u32(xstart);
   write_u32(ystart);
}

void rhi_dump_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   if (!file)
      return;
   write_u32(RHI_HORIZONTAL_RANGE);
   write_u32(x1);
   write_u32(x2);
}

void rhi_dump_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   if (!file)
      return;
   write_u32(RHI_VERTICAL_RANGE);
   write_u32(y1);
   write_u32(y2);
}

void rhi_dump_set_display_mode(bool depth_24bpp, bool is_pal, bool is_480i, int width_mode)
{
   if (!file)
      return;
   write_u32(RHI_DISPLAY_MODE);
   write_u32(depth_24bpp);
   write_u32(is_pal);
   write_u32(is_480i);
   write_u32(width_mode);
}

void rhi_dump_triangle(const struct rhi_dump_vertex *vertices, const struct rhi_render_state *state)
{
   unsigned i;
   if (!file)
      return;
   write_u32(RHI_TRIANGLE);
   for (i = 0; i < 3; i++)
      rhi_dump_vertex_write(&vertices[i]);
   rhi_dump_state_write(state);
}

void rhi_dump_quad(const struct rhi_dump_vertex *vertices, const struct rhi_render_state *state)
{
   unsigned i;
   if (!file)
      return;
   write_u32(RHI_QUAD);
   for (i = 0; i < 4; i++)
      rhi_dump_vertex_write(&vertices[i]);
   rhi_dump_state_write(state);
}

void rhi_dump_line(const struct rhi_dump_line_data *line)
{
   if (!file)
      return;
   write_u32(RHI_LINE);
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

void rhi_dump_load_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *vram, bool mask_test, bool set_mask)
{
   if (!file)
      return;
   write_u32(RHI_LOAD_IMAGE);
   write_u32(x);
   write_u32(y);
   write_u32(w);
   write_u32(h);
   write_u32(mask_test);
   write_u32(set_mask);
   write_u16(vram + y * 1024 + x, w, h);
}

void rhi_dump_fill_rect(uint32_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
   if (!file)
      return;
   write_u32(RHI_FILL_RECT);
   write_u32(color);
   write_u32(x);
   write_u32(y);
   write_u32(w);
   write_u32(h);
}

void rhi_dump_copy_rect(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h, bool mask_test, bool set_mask)
{
   if (!file)
      return;
   write_u32(RHI_COPY_RECT);
   write_u32(src_x);
   write_u32(src_y);
   write_u32(dst_x);
   write_u32(dst_y);
   write_u32(w);
   write_u32(h);
   write_u32(mask_test);
   write_u32(set_mask);
}

void rhi_dump_toggle_display(bool status)
{
   if (!file)
      return;
   write_u32(RHI_TOGGLE_DISPLAY);
   write_u32(status);
}
