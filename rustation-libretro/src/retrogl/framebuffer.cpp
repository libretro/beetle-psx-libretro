#include "framebuffer.h"

#include <stdlib.h> // exit()
#include <stdio.h>

Framebuffer::Framebuffer(Texture* color_texture)
{
    InitializeWithColorTexture(color_texture);
}

Framebuffer::Framebuffer(Texture* color_texture, Texture* depth_texture)
{
    InitializeWithColorTexture(color_texture);

    glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
                            GL_DEPTH_ATTACHMENT,
                            depth_texture->id,
                            0);

    /* error_or(fb) */
    get_error();
}

void Framebuffer::InitializeWithColorTexture(Texture* color_texture)
{
    GLuint id = 0;
    glGenFramebuffers(1, &id);

    this->id = id;
    this->_color_texture = color_texture;

    this->bind();

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

Framebuffer::~Framebuffer()
{
    this->drop();
}

void Framebuffer::bind()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->id);
}

void Framebuffer::drop()
{
    glDeleteFramebuffers(1, &this->id);
}
