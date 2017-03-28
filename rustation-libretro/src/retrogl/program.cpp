#include "program.h"

#include <stdlib.h>
#include <stdio.h>

static void get_program_info_log(Program *pg, GLuint id)
{
    GLint log_len = 0;

    glGetProgramiv(id, GL_INFO_LOG_LENGTH, &log_len);

    if (log_len <= 0)
        return;

    pg->info_log = (char*)malloc(log_len);
    GLsizei len = (GLsizei) log_len;
    glGetProgramInfoLog(id,
                        len,
                        &log_len,
                        (char*)pg->info_log);

    if (log_len <= 0)
        return;

    // The length returned by GetShaderInfoLog *excludes*
    // the ending \0 unlike the call to GetShaderiv above
    // so we can get rid of it by truncating here.
    /* log.truncate(log_len as usize); */
    /* Don't want to spend time thinking about the above, I'll just put a \0
    in the last index */
    pg->info_log[log_len - 1] = '\0';
}

void Program_init(
      Program *program,
      Shader* vertex_shader,
      Shader* fragment_shader)
{
   program->info_log = NULL;
   GLuint id = glCreateProgram();
   if (id == 0)
   {
      puts("An error occured creating the program object\n");
      exit(EXIT_FAILURE);
   }

   glAttachShader(id, vertex_shader->id);
   glAttachShader(id, fragment_shader->id);

   glLinkProgram(id);

   glDetachShader(id, vertex_shader->id);
   glDetachShader(id, fragment_shader->id);

   /* Program owns the two pointers, so we clean them up now */
   if (vertex_shader)
   {
      delete vertex_shader;
      vertex_shader = NULL;
   }

   if (fragment_shader)
   {
      delete fragment_shader;
      fragment_shader = NULL;
   }

   // Check if the program linking was successful
   GLint status = (GLint) GL_FALSE;
   glGetProgramiv(id, GL_LINK_STATUS, &status);
   get_program_info_log(program, id);

   if (status != (GLint) GL_TRUE)
   {
      puts("OpenGL program linking failed\n");
      puts("Program info log:\n");
      puts(program->info_log );

      exit(EXIT_FAILURE);
      return;
   }

   /* Rust code has a try statement here, perhaps we should fail fast with
      exit(EXIT_FAILURE) ? */
   UniformMap uniforms = load_program_uniforms(id);

   program->id       = id;
   program->uniforms = uniforms;
}

void Program_free(Program *program)
{
   if (!program)
      return;

   glDeleteProgram(program->id);
   free(program->info_log);
}

GLint Program_find_attribute(Program *program, const char* attr)
{
    return glGetAttribLocation(program->id, attr);
}

GLint Program_uniform(Program *program, const char* name)
{
    bool found = program->uniforms.find(name) != program->uniforms.end();
    if (!found)
    {
        printf("Attempted to access unknown uniform %s\n", name);
        exit(EXIT_FAILURE);
    }

    return program->uniforms[name];
}

UniformMap load_program_uniforms(GLuint program)
{
    GLint n_uniforms = 0;

    glGetProgramiv( program,
                    GL_ACTIVE_UNIFORMS,
                    &n_uniforms );

    UniformMap uniforms;

    // Figure out how long a uniform name can be
    GLint max_name_len = 0;

    glGetProgramiv( program,
                    GL_ACTIVE_UNIFORM_MAX_LENGTH,
                    &max_name_len);

    size_t u;
    for (u = 0; u < n_uniforms; ++u)
    {
       // Retrieve the name of this uniform. Don't use the size we just fetched, because it's inconvenient. Use something monstrously large.
       char name[256];
       size_t name_len = max_name_len;
       GLsizei len = 0;
       // XXX we might want to validate those at some point
       GLint size = 0;
       GLenum ty = 0;

       glGetActiveUniform( program,
             (GLuint) u,
             (GLsizei) name_len,
             &len,
             &size,
             &ty,
             (char*) name);
       if (len <= 0) {
          printf("Ignoring uniform name with size %d\n", len);
          continue;
       }

       // Retrieve the location of this uniform
       GLint location = glGetUniformLocation(program, (const char*) name);

       /* name.truncate(len as usize); */
       /* name[len - 1] = '\0'; */

       if (location < 0) {
          printf("Uniform \"%s\" doesn't have a location", name);
          continue;
       }

       uniforms[name] = location;
    }

    return uniforms;
}
