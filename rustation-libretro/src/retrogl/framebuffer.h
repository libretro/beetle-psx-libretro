#ifndef RETROGL_FRAMEBUFFER_H
#define RETROGL_FRAMEBUFFER_H

#include <glsm/glsmsym.h>
#include "texture.h"
#include "error.h"

class Framebuffer {
public:
    GLuint id;
    Texture* _color_texture;

    Framebuffer(Texture* color_texture);
    ~Framebuffer();
};

#endif
