#ifndef RSX_GL_FRAMEBUFFER_H
#define RSX_GL_FRAMEBUFFER_H

#include <stdint.h>

#include <glsm/glsmsym.h>

#include "texture.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Framebuffer
{
   uintptr_t id;
   struct Texture *color_texture;
};

void framebuffer_bind(struct Framebuffer *self);

struct Framebuffer *framebuffer_init(struct Texture *color_texture);

void framebuffer_free(struct Framebuffer *self);

#ifdef __cplusplus
}
#endif

#endif
