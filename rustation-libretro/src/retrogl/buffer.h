#ifndef RETROGL_BUFFER_H
#define RETROGL_BUFFER_H

#include <glsm/glsmsym.h>

#include <stdio.h>
#include <stdlib.h> // size_t
#include <stdint.h>

#include <vector>

#include "vertex.h"
#include "program.h"
#include "error.h"

#define draw_indexed__raw(mode, indices, count) glDrawElements(mode, count, GL_UNSIGNED_SHORT, indices)

#define DRAWBUFFER_IS_EMPTY(x)           ((x)->active_next_index == (x)->active_command_index)
#define DRAWBUFFER_REMAINING_CAPACITY(x) ((x)->capacity - (x)->active_next_index)
#define DRAWBUFFER_NEXT_INDEX(x)         ((x)->active_next_index)

/* Bind the buffer to the current VAO */
#define DRAWBUFFER_BIND(x)               (glBindBuffer(GL_ARRAY_BUFFER, (x)->id))

#define DRAWBUFFER_PREBIND(x) \
   VertexArrayObject_bind((x)->vao); \
   program_bind((x)->program)

/* Called when the current batch is completed (the draw calls
 * have been done and we won't reference that data anymore) */
#define DRAWBUFFER_FINISH(x)             ((x)->active_command_index = (x)->active_next_index)


template<typename T>
class DrawBuffer
{
public:
    /// OpenGL name for this buffer
    GLuint id;
    /// Vertex Array Object containing the bindings for this
    /// buffer. I'm assuming that each VAO will only use a single
    /// buffer for simplicity.
    VertexArrayObject* vao;
    /// Program used to draw this buffer
    Program* program;
    /// Persistently mapped buffer (using ARB_buffer_storage)
    T *map;
    GLsync buffer_fence;

    /// Index one-past the last element stored in `active`
    unsigned active_next_index;
    /// Index of the first element of the current command in `active`
    unsigned active_command_index;
    /// Number of elements T that `active` and `backed` can hold
    size_t capacity;

    DrawBuffer(size_t capacity, Program* program)
    {
       VertexArrayObject* vao = (VertexArrayObject*)calloc(1, sizeof(*vao));
       
       VertexArrayObject_init(vao);

       GLuint id = 0;
       // Generate the buffer object
       glGenBuffers(1, &id);

       this->vao = vao;
       this->program = program;
       this->capacity = capacity;
       this->id = id;

       // Create and map the buffer
       DRAWBUFFER_BIND(this);

       size_t element_size = sizeof(T);
       // We double buffer so we allocate a storage twise as big
       GLsizeiptr storage_size = (GLsizeiptr) (this->capacity * element_size * 3);

       glBufferStorage(GL_ARRAY_BUFFER,
             storage_size,
             NULL,
             GL_MAP_WRITE_BIT |
             GL_MAP_PERSISTENT_BIT |
             GL_MAP_COHERENT_BIT);

       this->bind_attributes();

       void *m = glMapBufferRange(GL_ARRAY_BUFFER,
             0,
             storage_size,
             GL_MAP_WRITE_BIT |
             GL_MAP_PERSISTENT_BIT |
             GL_MAP_FLUSH_EXPLICIT_BIT |
             GL_MAP_COHERENT_BIT);

       this->map = reinterpret_cast<T*>(m);

       this->active_next_index = 0;
       this->active_command_index = 0;
    }

    ~DrawBuffer()
    {
       DRAWBUFFER_BIND(this);

       glUnmapBuffer(GL_ARRAY_BUFFER);

       glDeleteBuffers(1, &this->id);

       if (this->buffer_fence)
       {
          glDeleteSync(this->buffer_fence);
       }

       if (this->vao)
       {
          VertexArrayObject_free(this->vao);
          free(this->vao);
          this->vao = NULL;
       }

       if (this->program)
       {
          Program_free(program);
          free(program);
          this->program = NULL;
       }
    }


    void bind_attributes()
    {
       VertexArrayObject_bind(this->vao);

       // ARRAY_BUFFER is captured by VertexAttribPointer
       DRAWBUFFER_BIND(this);

       std::vector<Attribute> attrs = T::attributes();

       GLint element_size = (GLint) sizeof( T );

       //speculative: attribs enabled on VAO=0 (disabled) get applied to the VAO when created initially
       //as a core, we don't control the state entirely at this point. frontend may have enabled attribs.
       //we need to make sure they're all disabled before then re-enabling the attribs we want
       //(solves crashes on some drivers/compilers due to accidentally enabled attribs)
       GLint nVertexAttribs;
       glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nVertexAttribs);

       for (int i = 0; i < nVertexAttribs; i++)
          glDisableVertexAttribArray(i);

       for (std::vector<Attribute>::iterator it(attrs.begin()); it != attrs.end(); ++it)
       {
          Attribute& attr = *it;
          GLint index     = Program_find_attribute(this->program, attr.name);

          // Don't error out if the shader doesn't use this
          // attribute, it could be caused by shader
          // optimization if the attribute is unused for
          // some reason.
          if (index < 0)
             continue;

          glEnableVertexAttribArray((GLuint) index);

          // This captures the buffer so that we don't have to bind it
          // when we draw later on, we'll just have to bind the vao
          switch (attr.ty)
          {
             case GL_BYTE:
             case GL_UNSIGNED_BYTE:
             case GL_SHORT:
             case GL_UNSIGNED_SHORT:
             case GL_INT:
             case GL_UNSIGNED_INT:
                glVertexAttribIPointer( index,
                      attr.components,
                      attr.ty,
                      element_size,
                      (GLvoid*)attr.offset);
                break;
             case GL_FLOAT:
                glVertexAttribPointer(  index,
                      attr.components,
                      attr.ty,
                      GL_FALSE,
                      element_size,
                      (GLvoid*)attr.offset);
                break;
             case GL_DOUBLE:
                glVertexAttribLPointer( index,
                      attr.components,
                      attr.ty,
                      element_size,
                      (GLvoid*)attr.offset);
                break;
          }
       }
    }


    /// Swap the active and backed buffers
    void swap()
    {
       this->buffer_fence = reinterpret_cast<GLsync>(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));

       // Wait for the buffer to be ready for reuse
       if (this->buffer_fence)
       {
          glWaitSync(this->buffer_fence, 0, GL_TIMEOUT_IGNORED);
          glDeleteSync(this->buffer_fence);
          this->buffer_fence = NULL;
       }

       this->active_next_index = 0;
       this->active_command_index = 0;
    }

    void enable_attribute(const char* attr)
    {
       GLint index = Program_find_attribute(this->program, attr);

       if (index < 0)
          return;

       VertexArrayObject_bind(this->vao);

       glEnableVertexAttribArray(index);
    }

    void disable_attribute(const char* attr)
    {
       GLint index = Program_find_attribute(this->program, attr);

       if (index < 0)
          return;

       VertexArrayObject_bind(this->vao);

       glDisableVertexAttribArray(index);
    }

    void push_slice(T slice[], size_t n)
    {
       if (n > DRAWBUFFER_REMAINING_CAPACITY(this) )
       {
          printf("DrawBuffer::push_slice() - Out of memory \n");
          return;
       }

       memcpy(this->map + this->active_next_index,
             slice,
             n * sizeof(T));

       this->active_next_index += n;
    }

    void draw(GLenum mode)
    {
       VertexArrayObject_bind(this->vao);
       program_bind(this->program);

       unsigned start = this->active_command_index;
       unsigned len = this->active_next_index - this->active_command_index;

       // Length in number of vertices
       glDrawArrays(mode, start, len);
    }

};

#endif
