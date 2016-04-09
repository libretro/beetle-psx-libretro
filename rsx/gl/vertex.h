#ifndef RSX_GL_VERTEX_H
#define RSX_GL_VERTEX_H

#include <stdint.h>

#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

struct VertexArrayObject
{
   uintptr_t id;
};

struct Attribute
{
   char name[32];
   unsigned offset;
   //* Attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...) */
   GLenum ty;
   GLint components;
};

struct VertexArrayObject *vao_init(void);

void vao_bind(struct VertexArrayObject *vao);

void vao_free(struct VertexArrayObject *vao);

#ifdef __cplusplus
}
#endif

#endif
