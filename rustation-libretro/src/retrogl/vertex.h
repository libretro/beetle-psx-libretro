#ifndef RETROGL_VERTEX_H
#define RETROGL_VERTEX_H

#include <glsm/glsmsym.h>

#include <stdlib.h>
#include <string>

#define VertexArrayObject_bind(x) (glBindVertexArray((x)->id))

struct VertexArrayObject
{
    GLuint id;
};

void VertexArrayObject_init(VertexArrayObject *vao);

void VertexArrayObject_free(VertexArrayObject *vao);

struct Attribute
{
    std::string name;
    size_t offset;
    /// Attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...)
    GLenum ty;
    GLint components;
};

#endif
