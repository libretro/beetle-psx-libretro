#include "framebuffer.h"

#include <stdlib.h> // exit()
#include <stdio.h>

static void InitializeWithColorTexture(Framebuffer *fb, Texture* color_texture)
{
    GLuint id = 0;
    glGenFramebuffers(1, &id);

    fb->id = id;
    fb->_color_texture = color_texture;

    fb->bind();

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

    /* error_or(fb) */
    get_error();
}

Framebuffer::Framebuffer(Texture* color_texture)
{
    InitializeWithColorTexture(this, color_texture);
}

Framebuffer::Framebuffer(Texture* color_texture, Texture* depth_texture)
{
    InitializeWithColorTexture(this, color_texture);

    glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
                            GL_DEPTH_ATTACHMENT,
                            depth_texture->id,
                            0);

    /* error_or(fb) */
    get_error();
}


Framebuffer::~Framebuffer()
{
    glDeleteFramebuffers(1, &this->id);
}

void Framebuffer::bind()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->id);
}
