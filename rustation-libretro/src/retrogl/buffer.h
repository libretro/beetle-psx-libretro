#ifndef RETROGL_BUFFER_H
#define RETROGL_BUFFER_H

#include <glsm/glsmsym.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <vector>
#include <deque>
#include <cassert>

#include "vertex.h"
#include "program.h"
#include "error.h"

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
    /// Currently mapped buffer range (write-only)
    T *map;
    /// Number of elements T mapped at once in `map`
    size_t capacity;
    /// Index one-past the last element stored in `map`, relative to
    /// the first element in `map`
    size_t map_index;
    /// Absolute offset of the 1st mapped element in the current
    /// buffer relative to the beginning of the GL storage.
    size_t map_start;

    DrawBuffer(size_t capacity, Program* program)
        :map(NULL)
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
	// We allocate enough space for 3 times the buffer space and
	// we only remap one third of it at a time
        GLsizeiptr storage_size = this->capacity * element_size * 3;

        // Since we store indexes in unsigned shorts we want to make
        // sure the entire buffer is indexable.
        assert(this->capacity * 3 <= 0xffff);

        glBufferData(GL_ARRAY_BUFFER, storage_size, NULL, GL_DYNAMIC_DRAW);

        this->bind_attributes();

        this->map_index = 0;
        this->map_start = 0;

        this->map__no_bind();

        get_error();
    }

    ~DrawBuffer()
    {
        this->bind();

	this->unmap__no_bind();

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

    void bind_attributes()
    {
        this->vao->bind();

        // ARRAY_BUFFER is captured by VertexAttribPointer
        this->bind();

        std::vector<Attribute> attrs = T::attributes();

        GLint element_size = (GLint) sizeof( T );

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

    // Map the buffer for write-only access
    void map__no_bind()
    {
        size_t element_size = sizeof(T);
        GLsizeiptr buffer_size = this->capacity * element_size;
        GLintptr offset_bytes;
        void *m;

        this->bind();

        // If we're already mapped something's wrong
        assert(this->map == NULL);

        if (this->map_start > 2 * this->capacity) {
            // We don't have enough room left to remap `capacity`,
            // start back from the beginning of the buffer.
            this->map_start = 0;
        }

        offset_bytes = this->map_start * element_size;

        m = glMapBufferRange(GL_ARRAY_BUFFER,
                             offset_bytes,
                             buffer_size,
                             GL_MAP_WRITE_BIT |
                             GL_MAP_INVALIDATE_RANGE_BIT);

        get_error();

        // Just in case...
        assert(m != NULL);

        this->map = reinterpret_cast<T *>(m);
    }

    // Unmap the active buffer
    void unmap__no_bind()
    {
        assert(this->map != NULL);

        this->bind();

        glUnmapBuffer(GL_ARRAY_BUFFER);

        this->map = NULL;
    }

    /// Returns the index of the next item to be inserted in
    /// `map`. Can be used to build an index buffer for indexed draws.
    size_t next_index()
    {
        return this->map_start + this->map_index;
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

    /// Return true if `map` is empty
    bool empty()
    {
        return this->map_index == 0;
    }

    /// Bind the buffer to the current VAO
    void bind()
    {
        glBindBuffer(GL_ARRAY_BUFFER, this->id);
    }

    /// Push new vertices in the storage. If `n` is greater than
    /// `remaining_capacity` this function crashes, it's up to the
    /// caller to make sure there's enough room left.
    void push_slice(T slice[], size_t n)
    {
        assert(n <= this->remaining_capacity());
        assert(this->map != NULL);

	memcpy(this->map + this->map_index,
	       slice,
	       n * sizeof(T));

	this->map_index += n;
    }

    /// Prepares the buffer for a draw command. We have to bind the
    /// VAO, the program and unmap the buffer.
    void prepare_draw()
    {
        this->vao->bind();
        this->program->bind();
        // I don't need to bind this to draw (it's captured by the
        // VAO) but I need it to map/unmap the storage.
        this->bind();

        this->unmap__no_bind();
    }

    /// Finalize the current buffer data and remap a fresh slice of
    /// the storage.
    void finalize_draw__no_bind()
    {
        this->map_start += this->map_index;
        this->map_index = 0;

        this->map__no_bind();
    }

    void draw(GLenum mode)
    {
        if (this->empty()) {
	  return;
	}

        this->prepare_draw();

	// Length in number of vertices
        glDrawArrays(mode, this->map_start, this->map_index);

        this->finalize_draw__no_bind();

        get_error();
    }

    /// This method doesn't call prepare_draw/finalize_draw itself, it
    /// must be handled by the caller. This is because this command
    /// can be called several times on the same buffer (i.e. multiple
    /// draw calls between the prepare/finalize)
    void draw_indexed__raw(GLenum mode, GLushort *indices, GLsizei count)
    {
        this->bind();

        if (this->empty()) {
	  return;
	}

        glDrawElements(mode, count, GL_UNSIGNED_SHORT, indices);

        get_error();
    }

    size_t remaining_capacity()
    {
        return this->capacity - this->map_index;
    }
};

#endif
