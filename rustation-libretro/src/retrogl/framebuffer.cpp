#include "framebuffer.h"

#include <stdlib.h> // exit()
#include <stdio.h>

Framebuffer::Framebuffer(Texture* color_texture)
{
   Framebuffer *fb = this;
   GLuint id = 0;
   glGenFramebuffers(1, &id);

   fb->id = id;
   fb->_color_texture = color_texture;

   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->id);

   glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
         GL_COLOR_ATTACHMENT0,
         color_texture->id,
         0);

   GLenum col_attach_0 = GL_COLOR_ATTACHMENT0;
   glDrawBuffers(1, &col_attach_0);
   glViewport( 0,
         0,
         (GLsizei) color_texture->width,
         (GLsizei) color_texture->height);

#ifdef DEBUG
   get_error();
#endif
}

Framebuffer::~Framebuffer()
{
    glDeleteFramebuffers(1, &this->id);
}
