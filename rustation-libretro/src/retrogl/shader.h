#ifndef RETROGL_SHADER_H
#define RETROGL_SHADER_H

#include "error.h"

#include <glsm/glsmsym.h>

class Shader {
public:
    GLuint id;

    Shader(const char* source, GLenum shader_type);
    ~Shader();

    char *info_log;
};

#endif
