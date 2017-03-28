#ifndef RETROGL_TEXTURE_H
#define RETROGL_TEXTURE_H

#include "error.h"

#include <glsm/glsmsym.h>

#include <stdint.h>

class Texture {
public:
    GLuint id;
    uint32_t width;
    uint32_t height;

    Texture(uint32_t width, uint32_t height, GLenum internal_format);
    ~Texture();
};

#define  Texture_bind(tex, texture_unit) \
    glActiveTexture(texture_unit); \
    glBindTexture(GL_TEXTURE_2D, tex->id)

void Texture_set_sub_image(
      Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      GLenum format,
      GLenum ty,
      uint16_t* data);

void Texture_set_sub_image_window(
      Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      size_t row_len,
      GLenum format,
      GLenum ty,
      uint16_t* data);

#endif
