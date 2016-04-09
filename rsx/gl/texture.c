#include <stdlib.h>

#include "texture.h"

struct Texture *texture_init(
      uint32_t width,
      uint32_t height,
      GLenum internal_format)
{
   uintptr_t id        = 0;
   struct Texture *tex = (struct Texture*)
      calloc(1, sizeof(*tex));

   if (!tex)
      return NULL;

   glGenTextures(1, (GLuint*)&id);
   glBindTexture(GL_TEXTURE_2D, id);
   glTexStorage2D(GL_TEXTURE_2D,
         1,
         internal_format,
         (GLsizei)width,
         (GLsizei)height);

   tex->id     = id;
   tex->width  = width;
   tex->height = height;

   return tex;
}

void texture_bind(struct Texture *self, GLenum texture_unit)
{
   glActiveTexture(texture_unit);
   glBindTexture(GL_TEXTURE_2D, self->id);
}

void texture_set_sub_image(struct Texture *self,
      uint16_t top_left_x, uint16_t top_left_y,
      uint16_t resolution_w, uint16_t resolution_h,
      GLenum format, GLenum ty, const void *data)
{
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glBindTexture(GL_TEXTURE_2D, self->id);

   glTexSubImage2D(GL_TEXTURE_2D,
         0,
         (GLint)top_left_x,
         (GLint)top_left_y,
         (GLsizei)resolution_w,
         (GLsizei)resolution_h,
         format,
         ty,
         (const GLvoid*)data);
}

void texture_set_sub_image_window(struct Texture *self,
      uint16_t top_left_x, uint16_t top_left_y,
      uint16_t resolution_w, uint16_t resolution_h,
      unsigned row_len,
      GLenum format,
      GLenum ty,
      void *data)
{
   uint16_t     x       = top_left_x;
   uint16_t     y       = top_left_y;
   unsigned index       = y * row_len + x;
   const void *new_data = (const void*)data + index;

   glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)row_len);

   texture_set_sub_image(self, top_left_x, top_left_y, resolution_w, resolution_h, format, ty, new_data);

   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void texture_free(struct Texture *self)
{
   if (!self)
      return;
   glDeleteTextures(1, (GLuint*)&self->id);
}
