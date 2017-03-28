#ifndef RETROGL_SHADER_H
#define RETROGL_SHADER_H

#include "error.h"

#include <glsm/glsmsym.h>

struct Shader
{
    GLuint id;
    char *info_log;
};

void Shader_init(Shader *shader,
      const char* source,
      GLenum shader_type);

void Shader_free(Shader *shader);

#endif
