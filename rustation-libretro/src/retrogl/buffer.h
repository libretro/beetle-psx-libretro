#ifndef RETROGL_BUFFER_H
#define RETROGL_BUFFER_H

#include <glsm/glsmsym.h>

#include <stdio.h>
#include <stdlib.h> // size_t
#include <stdint.h>
//#include <unistd.h>

#include <vector>
#include <deque>

#include "vertex.h"
#include "program.h"
#include "error.h"

template<typename T>
struct Storage {
  // Fence used to make sure we're not writing to the buffer while
  // it's being used.
  GLsync fence;
  // Offset in the main buffer
  unsigned offset;

  Storage()
    :fence(NULL), offset(0)
  {
  }

  Storage(unsigned offset)
    :fence(NULL), offset(offset)
  {
  }

  ~Storage() {
    if (this->fence) {
      glDeleteSync(this->fence);
    }
  }

  // Wait for the buffer to be ready for reuse
  void sync() {
    if (this->fence) {
      glWaitSync(this->fence, 0, GL_TIMEOUT_IGNORED);
      glDeleteSync(this->fence);
      this->fence = NULL;
      get_error();
    }
  }

  void create_fence() {
    void *fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    this->fence = reinterpret_cast<GLsync>(fence);
  }
};

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
    /// Use triple buffering
    Storage<T> buffers[3];
    /// Index one-past the last element stored in `active`
    unsigned active_next_index;
    /// Index of the first element of the current command in `active`
    unsigned active_command_index;
    /// Number of elements T that `active` and `backed` can hold
    size_t capacity;

    DrawBuffer(size_t capacity, Program* program)
    {
        VertexArrayObject* vao = new VertexArrayObject();

        GLuint id = 0;
        // Generate the buffer object
        glGenBuffers(1, &id);

        this->vao = vao;
        this->program = program;
        this->capacity = capacity;
        this->id = id;

	// Create and map the buffer
	this->bind();
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

	this->buffers[0] = Storage<T>(0);
	this->buffers[1] = Storage<T>(this->capacity);
	this->buffers[2] = Storage<T>(this->capacity * 2);

	this->active_next_index = 0;
	this->active_command_index = 0;

        get_error();
    }

    ~DrawBuffer()
    {
        this->bind();

	this->buffers[1].sync();
	this->buffers[2].sync();

	glUnmapBuffer(GL_ARRAY_BUFFER);

	glDeleteBuffers(1, &this->id);

        if (this->vao) {
            delete this->vao;
            this->vao = NULL;
        }

        if (this->program) {
            delete program;
            this->program = NULL;
        }
    }

    /* fn bind_attributes(&self)-> Result<(), Error> { */
    void bind_attributes()
    {
        this->vao->bind();

        // ARRAY_BUFFER is captured by VertexAttribPointer
        this->bind();

        std::vector<Attribute> attrs = T::attributes();

        GLint element_size = (GLint) sizeof( T );

        /*
        let index =
                    match self.program.find_attribute(attr.name) {
                        Ok(i) => i,
                        // Don't error out if the shader doesn't use this
                        // attribute, it could be caused by shader
                        // optimization if the attribute is unused for
                        // some reason.
                        Err(Error::InvalidValue) => continue,
                        Err(e) => return Err(e),
                    };

        */

        //speculative: attribs enabled on VAO=0 (disabled) get applied to the VAO when created initially
        //as a core, we don't control the state entirely at this point. frontend may have enabled attribs.
        //we need to make sure they're all disabled before then re-enabling the attribs we want
        //(solves crashes on some drivers/compilers due to accidentally enabled attribs)
        GLint nVertexAttribs;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nVertexAttribs);
        for (int i = 0; i < nVertexAttribs; i++) glDisableVertexAttribArray(i);

        for (std::vector<Attribute>::iterator it(attrs.begin()); it != attrs.end(); ++it) {
            Attribute& attr = *it;
            GLint index = this->program->find_attribute(attr.name.c_str());

            // Don't error out if the shader doesn't use this
            // attribute, it could be caused by shader
            // optimization if the attribute is unused for
            // some reason.
            if (index < 0) {
                continue;
            }
            glEnableVertexAttribArray((GLuint) index);

            // This captures the buffer so that we don't have to bind it
            // when we draw later on, we'll just have to bind the vao
            switch (attr.ty) {
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
                                        attr.gl_offset());
                break;
            case GL_FLOAT:
                glVertexAttribPointer(  index,
                                        attr.components,
                                        attr.ty,
                                        GL_FALSE,
                                        element_size,
                                        attr.gl_offset());
                break;
            case GL_DOUBLE:
                glVertexAttribLPointer( index,
                                        attr.components,
                                        attr.ty,
                                        element_size,
                                        attr.gl_offset());
                break;
            }
        }

        get_error();
    }

    unsigned next_index() {
      return this->active_next_index;
    }

    /// Swap the active and backed buffers
    void swap() {
      this->buffers[0].create_fence();
      this->buffers[1].sync();
      std::swap(this->buffers[0], this->buffers[1]);
      std::swap(this->buffers[1], this->buffers[2]);
      this->active_next_index = 0;
      this->active_command_index = 0;
    }

    void enable_attribute(const char* attr)
    {
        GLint index = this->program->find_attribute(attr);

        if (index < 0) {
          return;
        }

        this->vao->bind();

        glEnableVertexAttribArray(index);

        get_error();
    }

    void disable_attribute(const char* attr)
    {
        GLint index = this->program->find_attribute(attr);

        if (index < 0) {
          return;
        }

        this->vao->bind();

        glDisableVertexAttribArray(index);

        get_error();
    }

    bool empty()
    {
        return this->active_next_index == this->active_command_index;
    }

    /// Called when the current batch is completed (the draw calls
    /// have been done and we won't reference that data anymore)
    void finish()
    {
      this->active_command_index = this->active_next_index;
    }

    /// Bind the buffer to the current VAO
    void bind()
    {
        glBindBuffer(GL_ARRAY_BUFFER, this->id);
    }

    void push_slice(T slice[], size_t n)
    {
        if (n > this->remaining_capacity() ) {
            printf("DrawBuffer::push_slice() - Out of memory \n");
            return;
        }

	memcpy(this->map + this->buffers[0].offset + this->active_next_index,
	       slice,
	       n * sizeof(T));

	this->active_next_index += n;
    }

    void draw(GLenum mode)
    {
        if (this->empty()) {
	  return;
	}

        this->vao->bind();
        this->program->bind();

	unsigned start = this->buffers[0].offset + this->active_command_index;
	unsigned len = this->active_next_index - this->active_command_index;

	// Length in number of vertices
        glDrawArrays(mode, start, len);

        get_error();
    }

    void pre_bind() {
        this->vao->bind();
	this->program->bind();
    }

    void draw_indexed_no_bind(GLenum mode, GLushort *indices, GLsizei count)
    {
        if (this->empty()) {
	  return;
	}

	GLint base = this->buffers[0].offset;

        glDrawElementsBaseVertex(mode,
				 count,
				 GL_UNSIGNED_SHORT,
				 indices,
				 base);

        get_error();
    }

    size_t remaining_capacity()
    {
      return this->capacity - this->active_next_index;
    }
};

#endif
