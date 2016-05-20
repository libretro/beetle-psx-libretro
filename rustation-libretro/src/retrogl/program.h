#ifndef RETROGL_PROGRAM_H
#define RETROGL_PROGRAM_H

#include "shader.h"
#include "error.h"

#include <glsm/glsmsym.h>

#include <map>
#include <string>

typedef std::map<std::string, GLint> UniformMap;

class Program {
public:
    GLuint id;
    /// Hash map of all the active uniforms in this program
    UniformMap uniforms;

    Program(Shader* vertex_shader, Shader* fragment_shader);
    ~Program();
    GLint find_attribute(const char* attr);
    void bind();
    GLint uniform(const char* name);
    void uniform1i(const char* name, GLint i);
    void uniform1ui(const char* name, GLuint i);
    void uniform2i(const char* name, GLint a, GLint b);
    void drop();

private:
    void get_program_info_log(GLuint id);
    char *info_log;
};


// Return a hashmap of all uniform names contained in `program` with
// their corresponding location.
UniformMap load_program_uniforms(GLuint program);

#endif
