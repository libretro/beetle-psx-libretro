
#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "../retrogl/buffer.h"
#include "../retrogl/shader.h"
#include "../retrogl/program.h"
#include "../retrogl/texture.h"
#include "../retrogl/framebuffer.h"
#include "../retrogl/error.h"

#include "libretro.h"
#include <glsm/glsmsym.h>

#include <vector>
#include <stdint.h>

extern retro_environment_t environ_cb;
extern retro_video_refresh_t video_cb;

const uint16_t VRAM_WIDTH_PIXELS = 1024;
const uint16_t VRAM_HEIGHT = 512;
const size_t VRAM_PIXELS = (size_t) VRAM_WIDTH_PIXELS * (size_t) VRAM_HEIGHT;

/// How many vertices we buffer before forcing a draw
static const unsigned int VERTEX_BUFFER_LEN = 2048;

struct DrawConfig {
    uint16_t display_top_left[2];
    uint16_t display_resolution[2];
    bool     display_24bpp;
    int16_t  draw_offset[2];
    uint16_t draw_area_top_left[2];
    uint16_t draw_area_dimensions[2];
    uint16_t vram[VRAM_PIXELS];
};

struct CommandVertex {
    /// Position in PlayStation VRAM coordinates
    int16_t position[3];
    /// RGB color, 8bits per component
    uint8_t color[3];
    /// Texture coordinates within the page
    uint16_t texture_coord[2];
    /// Texture page (base offset in VRAM used for texture lookup)
    uint16_t texture_page[2];
    /// Color Look-Up Table (palette) coordinates in VRAM
    uint16_t clut[2];
    /// Blending mode: 0: no texture, 1: raw-texture, 2: texture-blended
    uint8_t texture_blend_mode;
    /// Right shift from 16bits: 0 for 16bpp textures, 1 for 8bpp, 2
    /// for 4bpp
    uint8_t depth_shift;
    /// True if dithering is enabled for this primitive
    uint8_t dither;
    /// 0: primitive is opaque, 1: primitive is semi-transparent
    uint8_t semi_transparent;

    static std::vector<Attribute> attributes();
};

struct OutputVertex {
    /// Vertex position on the screen
    float position[2];
    /// Corresponding coordinate in the framebuffer
    uint16_t fb_coord[2];

    static std::vector<Attribute> attributes();
};

struct ImageLoadVertex {
    // Vertex position in VRAM
    uint16_t position[2];

    static std::vector<Attribute> attributes();
};

enum SemiTransparencyMode {
    /// Source / 2 + destination / 2
    Average = 0,
    /// Source + destination
    Add = 1,
    /// Destination - source
    SubtractSource = 2,
    /// Destination + source / 4
    AddQuarterSource = 3,
};

class GlRenderer {
public:
    /// Buffer used to handle PlayStation GPU draw commands
    DrawBuffer<CommandVertex>* command_buffer;
    /// Primitive type for the vertices in the command buffers
    /// (TRIANGLES or LINES)
    GLenum command_draw_mode;
    /// Temporary buffer holding vertices for semi-transparent draw
    /// commands.
    std::vector<CommandVertex> semi_transparent_vertices;
    /// Transparency mode for semi-transparent commands
    SemiTransparencyMode semi_transparency_mode;
    /// Polygon mode (for wireframe)
    GLenum command_polygon_mode;
    /// Buffer used to draw to the frontend's framebuffer
    DrawBuffer<OutputVertex>* output_buffer;
    /// Buffer used to copy textures from `fb_texture` to `fb_out`
    DrawBuffer<ImageLoadVertex>* image_load_buffer;
    /// Texture used to store the VRAM for texture mapping
    DrawConfig* config;
    /// Framebuffer used as a shader input for texturing draw commands
    Texture* fb_texture;
    /// Framebuffer used as an output when running draw commands
    Texture* fb_out;
    /// Depth buffer for fb_out
    Texture* fb_out_depth;
    /// Current resolution of the frontend's framebuffer
    uint32_t frontend_resolution[2];
    /// Current internal resolution upscaling factor
    uint32_t internal_upscaling;
    /// Current internal color depth
    uint8_t internal_color_depth;
    /// Counter for preserving primitive draw order in the z-buffer
    /// since we draw semi-transparent primitives out-of-order.
    int16_t primitive_ordering;

    /* pub fn from_config(config: DrawConfig) -> Result<GlRenderer, Error> */
    GlRenderer(DrawConfig* config);

    ~GlRenderer();

    template<typename T>
    static DrawBuffer<T>* build_buffer( const char* vertex_shader,
                                        const char* fragment_shader,
                                        size_t capacity,
                                        bool lifo  )
    {
        Shader* vs = new Shader(vertex_shader, GL_VERTEX_SHADER);
        Shader* fs = new Shader(fragment_shader, GL_FRAGMENT_SHADER);
        Program* program = new Program(vs, fs);

        return new DrawBuffer<T>(capacity, program, lifo);
    }

    void draw();
    void apply_scissor();
    void bind_libretro_framebuffer();
    void upload_textures(   uint16_t top_left[2], 
                            uint16_t dimensions[2],
                            uint16_t pixel_buffer[VRAM_PIXELS]);

    void upload_vram_window(uint16_t top_left[2], 
                            uint16_t dimensions[2],
                            uint16_t pixel_buffer[VRAM_PIXELS]);

    DrawConfig* draw_config();
    void prepare_render();
    bool refresh_variables();
    void finalize_frame();
    void maybe_force_draw(  size_t nvertices, GLenum draw_mode, 
                            bool semi_transparent, 
                            SemiTransparencyMode semi_transparency_mode);

    void set_draw_offset(int16_t x, int16_t y);
    void set_draw_area(uint16_t top_left[2], uint16_t dimensions[2]);

    void set_display_mode(  uint16_t top_left[2], 
                            uint16_t resolution[2],
                            bool depth_24bpp);

    void push_triangle( CommandVertex v[3],
                        SemiTransparencyMode semi_transparency_mode);

    void push_line( CommandVertex v[2],
                    SemiTransparencyMode semi_transparency_mode);

    void fill_rect( uint8_t color[3], 
                    uint16_t top_left[2], 
                    uint16_t dimensions[2]);

    void copy_rect( uint16_t source_top_left[2], 
                    uint16_t target_top_left[2],
                    uint16_t dimensions[2]);

};

std::vector<Attribute> attributes(CommandVertex* v);
std::vector<Attribute> attributes(OutputVertex* v);
std::vector<Attribute> attributes(ImageLoadVertex* v);

#endif
