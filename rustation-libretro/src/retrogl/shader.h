#ifndef RETROGL_SHADER_H
#define RETROGL_SHADER_H

#include "error.h"

#include <glsm/glsmsym.h>

class Shader {
public:
    GLuint id;

    Shader(const char* source, GLenum shader_type);
    ~Shader();
    void attach_to(GLuint program);
    void detach_from(GLuint program);
    void drop();

private:
    /* TODO/FIXME - Might as well make this a class/static method. 
    Same goes for Program::get_program_info_log() */
    void get_shader_info_log(GLuint id);
    char *info_log;
};

#endif
