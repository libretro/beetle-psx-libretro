#include <stdlib.h>

#include "framebuffer.h"

void framebuffer_bind(struct Framebuffer *self)
{
   if (!self)
      return;
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self->id);
}

struct Framebuffer *framebuffer_init(struct Texture *color_texture)
{
   static const GLenum buffs[] = { GL_COLOR_ATTACHMENT0 };
   uintptr_t id    = 0;
   struct Framebuffer *fb = (struct Framebuffer*)
      calloc(1, sizeof(*fb));

   if (!fb)
      return NULL;

   glGenFramebuffers(1, (GLuint*)&id);


   fb->id            = id;
   fb->color_texture = color_texture;

   framebuffer_bind(fb);

   glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
         GL_COLOR_ATTACHMENT0,
         color_texture->id,
         0);

   glDrawBuffers(1, buffs);
   glViewport(0,
         0,
         (GLsizei)color_texture->width,
         (GLsizei)color_texture->height);

   return fb;
}

void framebuffer_free(struct Framebuffer *self)
{
   if (!self)
      return;
   glDeleteFramebuffers(1, (const GLuint*)&self->id);
}
