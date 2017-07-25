#include <stdlib.h>
#include <stdio.h>

#include "texture.h"

void Texture_init(
      struct Texture *tex,
      uint32_t width,
      uint32_t height,
      GLenum internal_format)
{
    GLuint id = 0;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexStorage2D(GL_TEXTURE_2D,
                    1,
                    internal_format,
                    (GLsizei) width,
                    (GLsizei) height);

    tex->id     = id;
    tex->width  = width;
    tex->height = height;
}

void Texture_free(struct Texture *tex)
{
   if (tex)
      glDeleteTextures(1, &tex->id);
}

void Texture_set_sub_image(
      struct Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      GLenum format,
      GLenum ty,
      uint16_t* data)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    (GLint) top_left[0],
                    (GLint) top_left[1],
                    (GLsizei) resolution[0],
                    (GLsizei) resolution[1],
                    format,
                    ty,
                    (void*) data);
}

void Texture_set_sub_image_window(
      struct Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      size_t row_len,
      GLenum format,
      GLenum ty,
      uint16_t* data)
{
   uint16_t x         = top_left[0];
   uint16_t y         = top_left[1];

   size_t index       = ((size_t) y) * row_len + ((size_t) x);

   /* TODO - Am I indexing data out of bounds? */
   uint16_t* sub_data = &( data[index] );

   glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) row_len);

   Texture_set_sub_image(tex, top_left, resolution, format, ty, sub_data);

   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}
