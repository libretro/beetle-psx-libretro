#include "vertex.h"

#include <string.h> // strcpy()

void VertexArrayObject_init(struct VertexArrayObject *vao)
{
   GLuint id = 0;
   glGenVertexArrays(1, &id);

   vao->id = id;
}

void VertexArrayObject_free(struct VertexArrayObject *vao)
{
   if (vao)
      glDeleteVertexArrays(1, &vao->id);
}
