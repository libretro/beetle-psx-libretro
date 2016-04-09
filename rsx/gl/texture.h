#ifndef RSX_GL_TEXTURE_H
#define RSX_GL_TEXTURE_H

#include <stdint.h>

#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Texture
{
    uintptr_t id;
    uint32_t width;
    uint32_t height;
};

struct Texture *texture_init(
      uint32_t width,
      uint32_t height,
      GLenum internal_format);

void texture_bind(struct Texture *self, GLenum texture_unit);

void texture_set_sub_image(struct Texture *self,
      uint16_t top_left_x, uint16_t top_left_y,
      uint16_t resolution_w, uint16_t resolution_h,
      GLenum format, GLenum ty, const void *data);

void texture_set_sub_image_window(struct Texture *self,
      uint16_t top_left_x, uint16_t top_left_y,
      uint16_t resolution_w, uint16_t resolution_h,
      unsigned row_len,
      GLenum format,
      GLenum ty,
      void *data);

void texture_free(struct Texture *self);

#ifdef __cplusplus
}
#endif

#endif
