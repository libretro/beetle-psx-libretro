#ifndef RETROGL_FRAMEBUFFER_H
#define RETROGL_FRAMEBUFFER_H

#include <glsm/glsmsym.h>
#include "texture.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Framebuffer
{
    GLuint id;
    struct Texture* _color_texture;
};

void Framebuffer_init(struct Framebuffer *fb, struct Texture* color_texture);

#define Framebuffer_free(fb) glDeleteFramebuffers(1, fb.id)

#ifdef __cplusplus
}
#endif

#endif
