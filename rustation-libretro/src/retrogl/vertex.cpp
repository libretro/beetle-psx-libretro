#include "vertex.h"

#include <string.h> // strcpy()

VertexArrayObject::VertexArrayObject()
{
    GLuint id = 0;
    glGenVertexArrays(1, &id);

    get_error();

    this->id = id;
}

VertexArrayObject::~VertexArrayObject()
{
    glDeleteVertexArrays(1, &this->id);
}

void VertexArrayObject::bind()
{
    glBindVertexArray(this->id);
}

Attribute::Attribute(const char* name, size_t offset, GLenum ty, GLint components)
{
    this->name = name;
    this->offset = offset;
    this->ty = ty;
    this->components = components;
}
