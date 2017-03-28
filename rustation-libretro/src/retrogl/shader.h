#ifndef RETROGL_SHADER_H
#define RETROGL_SHADER_H

#include "error.h"

#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Shader
{
    GLuint id;
    char *info_log;
};

void Shader_init(struct Shader *shader,
      const char* source,
      GLenum shader_type);

void Shader_free(struct Shader *shader);

#ifdef __cplusplus
}
#endif

#endif
