#include "vertex.h"

#include <string.h> // strcpy()

void VertexArrayObject_init(VertexArrayObject *vao)
{
   GLuint id = 0;
   glGenVertexArrays(1, &id);

   vao->id = id;
}

void VertexArrayObject_free(VertexArrayObject *vao)
{
   if (vao)
      glDeleteVertexArrays(1, &vao->id);
}

Attribute::Attribute(const char* name, size_t offset, GLenum ty, GLint components)
{
    this->name = name;
    this->offset = offset;
    this->ty = ty;
    this->components = components;
}
