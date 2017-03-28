#include "shader.h"

#include <stdlib.h>
#include <stdio.h>

Shader::Shader(const char* source, GLenum shader_type)
    : info_log(NULL)
{
   Shader *shader = this;
   GLuint id = glCreateShader(shader_type);
   if (id == 0)
   {
      puts("An error occured creating the shader object\n");
      exit(EXIT_FAILURE);
   }

   glShaderSource( id,
         1,
         &source,
         NULL);
   glCompileShader(id);

   GLint status = (GLint) GL_FALSE;
   glGetShaderiv(id, GL_COMPILE_STATUS, &status);

   GLint log_len = 0;

   glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_len);

   if (log_len > 0)
   {
      shader->info_log = (char*)malloc(log_len);
      GLsizei len = (GLsizei) log_len;
      glGetShaderInfoLog(id,
            len,
            &log_len,
            (char*)shader->info_log);

      if (log_len > 0)
      {
         // The length returned by GetShaderInfoLog *excludes*
         // the ending \0 unlike the call to GetShaderiv above
         // so we can get rid of it by truncating here.
         /* log.truncate(log_len as usize); */
         /* Don't want to spend time thinking about the above, I'll just put a \0
            in the last index */
         shader->info_log[log_len - 1] = '\0';
      }
   }

   if (status != (GLint) GL_TRUE)
   {
      puts("Shader compilation failed:\n");

      /* print shader source */
      puts( source );

      puts("Shader info log:\n");
      puts(info_log);

      exit(EXIT_FAILURE);
      return;
   }

   this->id = id;
}

Shader::~Shader()
{
   glDeleteShader(this->id);
   free(info_log);
}
