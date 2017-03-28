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

class Attribute {
public:
    std::string name;
    size_t offset;
    /// Attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...)
    GLenum ty;
    GLint components;

    Attribute(const char* name, size_t offset, GLenum ty, GLint components);
};

/* 
<simias> in order to create the vertex attrib array I need to know:
the GL type of the field, the number of components (unary, pair or triple)
and the offset within the struct
*/

#endif
