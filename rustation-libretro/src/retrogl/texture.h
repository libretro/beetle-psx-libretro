#ifndef RETROGL_TEXTURE_H
#define RETROGL_TEXTURE_H

#include "error.h"

#include <glsm/glsmsym.h>

#include <stdint.h>

class Texture {
public:
    GLuint id;
    uint32_t width;
    uint32_t height;

    Texture(uint32_t width, uint32_t height, GLenum internal_format);
    ~Texture();
    void bind(GLenum texture_unit);
    
    /*
    pub fn set_sub_image<T>(&self,
                            top_left: (u16, u16),
                            resolution: (u16, u16),
                            format: GLenum,
                            ty: GLenum,
                            data: &[T]) -> Result<(), Error> {
    */
    /* This method was supposed to receive a generic parameter 'data'.
    In GlRenderer.cpp, I only see 'data' be an array of u16
    so I'll make it accept only that instead */
    void set_sub_image( uint16_t top_left[2],
                        uint16_t resolution[2],
                        GLenum format,
                        GLenum ty,
                        uint16_t* data);

    /* Same comment as above */
    void set_sub_image_window(  uint16_t top_left[2],
                                uint16_t resolution[2],
                                size_t row_len,
                                GLenum format,
                                GLenum ty,
                                uint16_t* data);

    void drop();
};

#endif
