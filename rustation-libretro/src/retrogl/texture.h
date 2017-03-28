#ifndef RETROGL_TEXTURE_H
#define RETROGL_TEXTURE_H

#include <stdint.h>

#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Texture
{
    GLuint id;
    uint32_t width;
    uint32_t height;
};

#define  Texture_bind(tex, texture_unit) \
    glActiveTexture(texture_unit); \
    glBindTexture(GL_TEXTURE_2D, tex->id)

void Texture_init(
      struct Texture *tex,
      uint32_t width,
      uint32_t height,
      GLenum internal_format);

void Texture_free(struct Texture *tex);

void Texture_set_sub_image(
      struct Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      GLenum format,
      GLenum ty,
      uint16_t* data);

void Texture_set_sub_image_window(
      struct Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      size_t row_len,
      GLenum format,
      GLenum ty,
      uint16_t* data);

#ifdef __cplusplus
}
#endif

#endif
