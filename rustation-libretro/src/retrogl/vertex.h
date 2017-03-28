#ifndef RETROGL_VERTEX_H
#define RETROGL_VERTEX_H

#include <stdlib.h>
#include <string.h>

#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VertexArrayObject_bind(x) (glBindVertexArray((x)->id))

struct VertexArrayObject
{
    GLuint id;
};

void VertexArrayObject_init(struct VertexArrayObject *vao);

void VertexArrayObject_free(struct VertexArrayObject *vao);

struct Attribute
{
   char name[32];
   size_t offset;
   /// Attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...)
   GLenum ty;
   GLint components;
};

#ifdef __cplusplus
}
#endif

#endif
