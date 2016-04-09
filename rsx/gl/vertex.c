#include <stdlib.h>

#include "vertex.h"

struct VertexArrayObject *vao_init(void)
{
   GLuint id = 0;
   struct VertexArrayObject *vao = (struct VertexArrayObject*)
      calloc(1, sizeof(*vao));

   if (!vao)
      return NULL;

   glGenVertexArrays(1, (GLuint*)&id);

   vao->id = id;

   return vao;
}

void vao_bind(struct VertexArrayObject *vao)
{
   if (!vao)
      return;
   glBindVertexArray(vao->id);
}

void vao_free(struct VertexArrayObject *vao)
{
   if (!vao)
      return;
   glDeleteBuffers(1, (const GLuint*)&vao->id);
}
