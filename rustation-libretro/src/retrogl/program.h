#ifndef RETROGL_PROGRAM_H
#define RETROGL_PROGRAM_H

#include "shader.h"
#include "error.h"

#include <glsm/glsmsym.h>

#include <map>
#include <string>

#define program_bind(x) (glUseProgram((x)->id))

#define program_uniform1i(x, name, i) \
{ \
   program_bind(x); \
   GLint u = (x)->uniform(name); \
   glUniform1i(u, i); \
}

#define program_uniform1ui(x, name, i) \
{ \
   program_bind(x); \
   GLint u = (x)->uniform(name); \
   glUniform1ui(u, i); \
}

#define program_uniform2i(x, name, a, b) \
{ \
   program_bind(x); \
   GLint u = (x)->uniform(name); \
   glUniform2i(u, a, b); \
}

#define program_uniform2ui(x, name, a, b) \
{ \
   program_bind(x); \
   GLint u = (x)->uniform(name); \
   glUniform2ui(u, a, b); \
}

typedef std::map<std::string, GLint> UniformMap;

struct Program
{
    GLuint id;
    /// Hash map of all the active uniforms in this program
    UniformMap uniforms;
    char *info_log;
};

void Program_init(
      Program *program,
      Shader* vertex_shader,
      Shader* fragment_shader);

void Program_free(Program *program);

GLint Program_uniform(Program *program, const char* name);
GLint Program_find_attribute(Program *program, const char* attr);


// Return a hashmap of all uniform names contained in `program` with
// their corresponding location.
UniformMap load_program_uniforms(GLuint program);

#endif
