#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void get_error(void)
{
#ifdef DEBUG
   GLenum error = glGetError();
   switch (error)
   {
      case GL_NO_ERROR:
         //puts("GL error flag: GL_NO_ERROR\n");
         return;
      case GL_INVALID_ENUM:
         puts("GL error flag: GL_INVALID_ENUM\n");
         break;
      case GL_INVALID_VALUE:
         puts("GL error flag: GL_INVALID_VALUE\n");
         break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
         puts("GL error flag: GL_INVALID_FRAMEBUFFER_OPERATION\n");
         break;
      case GL_OUT_OF_MEMORY:
         puts("GL error flag: GL_OUT_OF_MEMORY\n");
         break;
      case GL_STACK_UNDERFLOW:
         puts("GL error flag: GL_STACK_UNDERFLOW\n");
         break;
      case GL_STACK_OVERFLOW:
         puts("GL error flag: GL_STACK_OVERFLOW\n");
         break;
      case GL_INVALID_OPERATION:
         puts("GL error flag: GL_INVALID_OPERATION\n");
         break;
      default:
         printf("GL error flag: %d\n", (int) error);
         break;
   }

   assert(error == GL_NO_ERROR);
#endif
}
