#include "GlRenderer.h"

#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"

#include "shaders/command_vertex.glsl.h"
#include "shaders/command_fragment.glsl.h"
#include "shaders/output_vertex.glsl.h"
#include "shaders/output_fragment.glsl.h"
#include "shaders/image_load_vertex.glsl.h"
#include "shaders/image_load_fragment.glsl.h"

#include <stdio.h>   // printf()
#include <stdlib.h> // size_t, EXIT_FAILURE
#include <stddef.h> // offsetof()
#include <string.h>

// Main GPU instance, used to access the VRAM
extern PS_GPU *GPU;
static bool has_software_fb = false;

GlRenderer::GlRenderer(DrawConfig* config)
{

    struct retro_variable var = {0};

    var.key = "beetle_psx_internal_resolution";
    uint8_t upscaling = 1;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        /* Same limitations as libretro.cpp */
        upscaling = var.value[0] -'0';
    }

    var.key = "beetle_psx_filter";
    uint8_t filter = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "nearest"))
          filter = 0;
       else if (!strcmp(var.value, "3point N64"))
          filter = 1;
       else if (!strcmp(var.value, "bilinear"))
          filter = 2;

       this->filter_type = filter;
    }

    var.key = "beetle_psx_internal_color_depth";
    uint8_t depth = 16;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "32bpp"))
          depth = 32;
       else
          depth = 16;
    }


    var.key = "beetle_psx_scale_dither";
    bool scale_dither = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "enabled"))
          scale_dither = true;
       else
          scale_dither = false;
    }

    var.key = "beetle_psx_wireframe";
    bool wireframe = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "enabled"))
          wireframe = true;
       else
          wireframe = false;
    }

    var.key = "beetle_psx_display_vram";
    bool display_vram = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "enabled"))
	display_vram = true;
      else
	display_vram = false;
    }

    printf("Building OpenGL state (%dx internal res., %dbpp)\n", upscaling, depth);

    DrawBuffer<CommandVertex>* opaque_command_buffer =
        GlRenderer::build_buffer<CommandVertex>(
            command_vertex,
            command_fragment,
            VERTEX_BUFFER_LEN,
            true);

    DrawBuffer<OutputVertex>* output_buffer =
        GlRenderer::build_buffer<OutputVertex>(
            output_vertex,
            output_fragment,
            4,
            false);

    DrawBuffer<ImageLoadVertex>* image_load_buffer =
        GlRenderer::build_buffer<ImageLoadVertex>(
            image_load_vertex,
            image_load_fragment,
            4,
            false);

    uint32_t native_width  = (uint32_t) VRAM_WIDTH_PIXELS;
    uint32_t native_height = (uint32_t) VRAM_HEIGHT;

    // Texture holding the raw VRAM texture contents. We can't
    // meaningfully upscale it since most games use paletted
    // textures.
    Texture* fb_texture = new Texture(native_width, native_height, GL_RGB5_A1);

    if (depth > 16) {
        // Dithering is superfluous when we increase the internal
        // color depth
        opaque_command_buffer->disable_attribute("dither");
    }

    uint32_t dither_scaling = scale_dither ? upscaling : 1;
    GLenum command_draw_mode = wireframe ? GL_LINE : GL_FILL;

    opaque_command_buffer->program->uniform1ui("dither_scaling", dither_scaling);
    opaque_command_buffer->program->uniform1ui("texture_flt", this->filter_type);
    //opaque_command_buffer->program->uniform1ui("mask_setor", this->mask_set_or);
    //opaque_command_buffer->program->uniform1ui("mask_evaland", this->mask_eval_and);

    GLenum texture_storage = GL_RGB5_A1;
    switch (depth) {
    case 16:
        texture_storage = GL_RGB5_A1;
        break;
    case 32:
        texture_storage = GL_RGBA8;
        break;
    default:
        printf("Unsupported depth %d\n", depth);
        exit(EXIT_FAILURE);
    }

    Texture* fb_out = new Texture( native_width * upscaling,
                                   native_height * upscaling,
                                   texture_storage);

    Texture* fb_out_depth = new Texture( fb_out->width,
                                         fb_out->height,
                                         GL_DEPTH_COMPONENT32F);


    // let mut state = GlRenderer {
    this->filter_type    = filter;
    this->command_buffer = opaque_command_buffer;
    this->command_draw_mode = GL_TRIANGLES;
    this->semi_transparent_vertices.reserve((size_t) VERTEX_BUFFER_LEN);
    this->semi_transparency_mode =  SemiTransparencyMode_Average;
    this->command_polygon_mode = command_draw_mode;
    this->output_buffer = output_buffer;
    this->image_load_buffer = image_load_buffer;
    this->config = config;
    this->fb_texture = fb_texture;
    this->fb_out = fb_out;
    this->fb_out_depth = fb_out_depth;
    this->frontend_resolution[0] = 0;
    this->frontend_resolution[1] = 0;
    this->internal_upscaling = upscaling;
    this->internal_color_depth = depth;
    this->primitive_ordering = 0;
    this->tex_x_mask = 0;
    this->tex_x_or = 0;
    this->tex_y_mask = 0;
    this->tex_y_or = 0;
    this->display_vram = display_vram;
    this->mask_set_or  = 0;
    this->mask_eval_and = 0;
    // }

    this->display_off = true;

    //opaque_command_buffer->program->uniform1ui("mask_setor", this->mask_set_or);
    //opaque_command_buffer->program->uniform1ui("mask_evaland", this->mask_eval_and);

    //// NOTE: r5 - I have no idea what a borrow checker is.
    // Yet an other copy of this 1MB array to make the borrow
    // checker happy...
    uint16_t top_left[2] = {0, 0};
    uint16_t dimensions[2] = {(uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};
    this->upload_textures(top_left, dimensions, GPU->vram);
}

GlRenderer::~GlRenderer()
{
    if (this->command_buffer) {
        delete this->command_buffer;
        this->command_buffer = NULL;
    }

    if (this->output_buffer)
    {
        delete this->output_buffer;
        this->output_buffer = NULL;
    }

    if (this->image_load_buffer) {
        delete this->image_load_buffer;
        this->image_load_buffer = NULL;
    }

    if (this->config) {
        delete this->config;
        this->config = NULL;
    }

    if (this->fb_texture) {
        delete this->fb_texture;
        this->fb_texture = NULL;
    }

    if (this->fb_out) {
        delete this->fb_out;
        this->fb_out = NULL;
    }

    if (this->fb_out_depth) {
        delete this->fb_out_depth;
        this->fb_out_depth = NULL;
    }
}

/*
template<typename T>
static DrawBuffer<T>* GlRenderer::build_buffer( const char** vertex_shader,
                                                const char** fragment_shader,
                                                size_t capacity,
                                                bool lifo  )
{
    Shader* vs = new Shader(vertex_shader, GL_VERTEX_SHADER);
    Shader* fs = new Shader(fragment_shader, GL_FRAGMENT_SHADER);
    Program* program = new Program(vs, fs);

    return new DrawBuffer<T>(capacity, program, lifo);
}
*/

void GlRenderer::draw()
{
    if (this->command_buffer->empty() && this->semi_transparent_vertices.empty())
        return; // Nothing to be done

    int16_t x = this->config->draw_offset[0];
    int16_t y = this->config->draw_offset[1];

    this->command_buffer->program->uniform2i("offset", (GLint)x, (GLint)y);

    // We use texture unit 0
    this->command_buffer->program->uniform1i("fb_texture", 0);
    this->command_buffer->program->uniform1ui("texture_flt", this->filter_type);
    //this->command_buffer->program->uniform1ui("mask_setor", this->mask_set_or);
    //this->command_buffer->program->uniform1ui("mask_evaland", this->mask_eval_and);

    // Bind the out framebuffer
    Framebuffer _fb = Framebuffer(this->fb_out, this->fb_out_depth);

    glClear(GL_DEPTH_BUFFER_BIT);

    // First we draw the opaque vertices
    if (!this->command_buffer->empty()) {
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        glDisable(GL_BLEND);

        this->command_buffer->program->uniform1ui("draw_semi_transparent", 0);
        this->command_buffer->draw(this->command_draw_mode);
        this->command_buffer->clear();
    }

    // Then the semi-transparent vertices
    if (!this->semi_transparent_vertices.empty()) {

        // Emulation of the various PSX blending mode using a
        // combination of constant alpha/color (to emulate
        // constant 1/4 and 1/2 factors) and blending equation.
        GLenum blend_func = GL_FUNC_ADD;
        GLenum blend_src = GL_CONSTANT_ALPHA;
        GLenum blend_dst = GL_CONSTANT_ALPHA;

        switch (this->semi_transparency_mode) {
        /* 0.5xB + 0.5 x F */
        case SemiTransparencyMode_Average:
            blend_func = GL_FUNC_ADD;
            // Set to 0.5 with glBlendColor
            blend_src = GL_CONSTANT_ALPHA;
            blend_dst = GL_CONSTANT_ALPHA;
            break;
        /* 1.0xB + 1.0 x F */
        case SemiTransparencyMode_Add:
            blend_func = GL_FUNC_ADD;
            blend_src = GL_ONE;
            blend_dst = GL_ONE;
            break;
        /* 1.0xB - 1.0 x F */
        case SemiTransparencyMode_SubtractSource:
            blend_func = GL_FUNC_REVERSE_SUBTRACT;
            blend_src = GL_ONE;
            blend_dst = GL_ONE;
            break;
        case SemiTransparencyMode_AddQuarterSource:
            blend_func = GL_FUNC_ADD;
            blend_src = GL_CONSTANT_COLOR;
            blend_dst = GL_ONE;
            break;
        }

        glBlendFuncSeparate(blend_src, blend_dst, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(blend_func, GL_FUNC_ADD);
        glEnable(GL_BLEND);

        this->command_buffer->program->uniform1ui("draw_semi_transparent", 1);

        this->command_buffer->push_slice(&this->semi_transparent_vertices[0], this->semi_transparent_vertices.size());

        this->command_buffer->draw(this->command_draw_mode);

        this->command_buffer->clear();

        this->semi_transparent_vertices.clear();
    }

    this->primitive_ordering = 0;
}

void GlRenderer::apply_scissor()
{
    uint16_t _x = this->config->draw_area_top_left[0];
    uint16_t _y = this->config->draw_area_top_left[1];
    uint16_t _w = this->config->draw_area_bot_right[0] - _x;
    uint16_t _h = this->config->draw_area_bot_right[1] - _y;

    GLsizei upscale = (GLsizei) this->internal_upscaling;

    // We need to scale those to match the internal resolution if
    // upscaling is enabled
    GLsizei x = (GLsizei) _x * upscale;
    GLsizei y = (GLsizei) _y * upscale;
    GLsizei w = (GLsizei) _w * upscale;
    GLsizei h = (GLsizei) _h * upscale;

    glScissor(x, y, w, h);

}

void GlRenderer::bind_libretro_framebuffer()
{
    uint32_t f_w = this->frontend_resolution[0];
    uint32_t f_h = this->frontend_resolution[1];
    uint16_t _w;
    uint16_t _h;
    float    aspect_ratio;

    if (this->display_vram) {
      _w = VRAM_WIDTH_PIXELS;
      _h = VRAM_HEIGHT;
      // Is this accurate?
      aspect_ratio = 2.0 / 1.0;
    } else {
      _w = this->config->display_resolution[0];
      _h = this->config->display_resolution[1];
      aspect_ratio = 4.0 / 3.0;
    }

    uint32_t upscale = this->internal_upscaling;

    // XXX scale w and h when implementing increased internal
    // resolution
    uint32_t w = (uint32_t) _w * upscale;
    uint32_t h = (uint32_t) _h * upscale;

    if (w != f_w || h != f_h) {
        // We need to change the frontend's resolution
        struct retro_game_geometry geometry;
        geometry.base_width  = w;
        geometry.base_height = h;
        // Max parameters are ignored by this call
        geometry.max_width  = 0;
        geometry.max_height = 0;

        geometry.aspect_ratio = aspect_ratio;

        printf("Target framebuffer size: %dx%d\n", w, h);

        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);

        this->frontend_resolution[0] = w;
        this->frontend_resolution[1] = h;
    }

    // Bind the output framebuffer provided by the frontend
    /* TODO/FIXME - I think glsm_ctl(BIND) is the way to go here. Check with the libretro devs */
    GLuint fbo = glsm_get_current_framebuffer();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLsizei) w, (GLsizei) h);
}

void GlRenderer::upload_textures(   uint16_t top_left[2],
                                    uint16_t dimensions[2],
                                    uint16_t pixel_buffer[VRAM_PIXELS])
{
    this->draw();

    this->fb_texture->set_sub_image(top_left,
                                    dimensions,
                                    GL_RGBA,
                                    GL_UNSIGNED_SHORT_1_5_5_5_REV,
                                    pixel_buffer);
    this->image_load_buffer->clear();

    uint16_t x_start    = top_left[0];
    uint16_t x_end      = x_start + dimensions[0];
    uint16_t y_start    = top_left[1];
    uint16_t y_end      = y_start + dimensions[1];

    const size_t slice_len = 4;
    ImageLoadVertex slice[slice_len] =
    {
        {   {x_start,   y_start }   },
        {   {x_end,     y_start }   },
        {   {x_start,   y_end   }   },
        {   {x_end,     y_end   }   }
    };

    this->image_load_buffer->push_slice(slice, slice_len);

    this->image_load_buffer->program->uniform1i("fb_texture", 0);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Bind the output framebuffer
    // let _fb = Framebuffer::new(&self.fb_out);
    Framebuffer _fb = Framebuffer(this->fb_out);

    this->image_load_buffer->draw(GL_TRIANGLE_STRIP);
    glPolygonMode(GL_FRONT_AND_BACK, this->command_polygon_mode);
    glEnable(GL_SCISSOR_TEST);

    get_error();
}

void GlRenderer::upload_vram_window(uint16_t top_left[2],
                                    uint16_t dimensions[2],
                                    uint16_t pixel_buffer[VRAM_PIXELS])
{
    this->draw();

    this->fb_texture->set_sub_image_window( top_left,
                                            dimensions,
                                            (size_t) VRAM_WIDTH_PIXELS,
                                            GL_RGBA,
                                            GL_UNSIGNED_SHORT_1_5_5_5_REV,
                                            pixel_buffer);

    this->image_load_buffer->clear();

    uint16_t x_start    = top_left[0];
    uint16_t x_end      = x_start + dimensions[0];
    uint16_t y_start    = top_left[1];
    uint16_t y_end      = y_start + dimensions[1];

    const size_t slice_len = 4;
    ImageLoadVertex slice[slice_len] =
        {
            {   {x_start,   y_start }   },
            {   {x_end,     y_start }   },
            {   {x_start,   y_end   }   },
            {   {x_end,     y_end   }   }
        };
    this->image_load_buffer->push_slice(slice, slice_len);

    this->image_load_buffer->program->uniform1i("fb_texture", 0);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Bind the output framebuffer
    Framebuffer _fb = Framebuffer(this->fb_out);

    this->image_load_buffer->draw(GL_TRIANGLE_STRIP);
    glPolygonMode(GL_FRONT_AND_BACK, this->command_polygon_mode);
    glEnable(GL_SCISSOR_TEST);

    get_error();
}

DrawConfig* GlRenderer::draw_config()
{
    return this->config;
}

void GlRenderer::prepare_render()
{
    // In case we're upscaling we need to increase the line width
    // proportionally
    glLineWidth((GLfloat)this->internal_upscaling);
    glPolygonMode(GL_FRONT_AND_BACK, this->command_polygon_mode);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Used for PSX GPU command blending
    glBlendColor(0.25, 0.25, 0.25, 0.5);

    this->apply_scissor();

    // Bind `fb_texture` to texture unit 0
    this->fb_texture->bind(GL_TEXTURE0);
}

bool GlRenderer::has_software_renderer()
{
   return has_software_fb;
}

bool GlRenderer::refresh_variables()
{
    struct retro_variable var = {0};

    var.key = "beetle_psx_renderer_software_fb";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
       if (!strcmp(var.value, "enabled"))
          has_software_fb = true;
       else
          has_software_fb = false;
    }

    var.key = "beetle_psx_internal_resolution";
    uint8_t upscaling = 1;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        /* Same limitations as libretro.cpp */
        upscaling = var.value[0] -'0';
    }

    var.key = "beetle_psx_filter";
    uint8_t filter = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "nearest"))
          filter = 0;
       else if (!strcmp(var.value, "3point N64"))
          filter = 1;
       else if (!strcmp(var.value, "bilinear"))
          filter = 2;

       this->filter_type = filter;
    }

    var.key = "beetle_psx_internal_color_depth";
    uint8_t depth = 16;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        depth = !strcmp(var.value, "32bpp") ? 32 : 16;
    }


    var.key = "beetle_psx_scale_dither";
    bool scale_dither = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "enabled"))
          scale_dither = true;
       else
          scale_dither = false;
    }

    var.key = "beetle_psx_wireframe";
    bool wireframe = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
       if (!strcmp(var.value, "enabled"))
          wireframe = true;
       else
          wireframe = false;
    }

    var.key = "beetle_psx_display_vram";
    bool display_vram = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "enabled")) {
	display_vram = true;
      } else
	display_vram = false;
    }

    bool rebuild_fb_out =
      upscaling != this->internal_upscaling ||
      depth != this->internal_color_depth;

    if (rebuild_fb_out) {
        if (depth > 16) {
            this->command_buffer->disable_attribute("dither");
        } else {
            this->command_buffer->enable_attribute("dither");
        }

        uint32_t native_width = (uint32_t) VRAM_WIDTH_PIXELS;
        uint32_t native_height = (uint32_t) VRAM_HEIGHT;

        uint32_t w = native_width * upscaling;
        uint32_t h = native_height * upscaling;

        GLenum texture_storage = GL_RGB5_A1;
        switch (depth) {
        case 16:
            texture_storage = GL_RGB5_A1;
            break;
        case 32:
            texture_storage = GL_RGBA8;
            break;
        default:
            printf("Unsupported depth %d\n", depth);
            exit(EXIT_FAILURE);
        }

        Texture* fb_out = new Texture(w, h, texture_storage);

        if (this->fb_out) {
            delete this->fb_out;
            this->fb_out = NULL;
        }

        this->fb_out = fb_out;

        // This is a bit wasteful since it'll re-upload the data
        // to `fb_texture` even though we haven't touched it but
        // this code is not very performance-critical anyway.

        uint16_t top_left[2] = {0, 0};
        uint16_t dimensions[2] = {(uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};

        this->upload_textures(top_left, dimensions,
			      GPU->vram);


        if (this->fb_out_depth) {
            delete this->fb_out_depth;
            this->fb_out_depth = NULL;
        }

        this->fb_out_depth = new Texture(w, h, GL_DEPTH_COMPONENT32F);
    }

    uint32_t dither_scaling = scale_dither ? upscaling : 1;
    this->command_buffer->program->uniform1ui("dither_scaling", (GLuint) dither_scaling);
    this->command_buffer->program->uniform1ui("texture_flt", this->filter_type);
    //this->command_buffer->program->uniform1ui("mask_setor", this->mask_set_or);
    //this->command_buffer->program->uniform1ui("mask_evaland", this->mask_eval_and);

    this->command_polygon_mode = wireframe ? GL_LINE : GL_FILL;

    glLineWidth((GLfloat) upscaling);

    // If the scaling factor has changed the frontend should be
    // reconfigured. We can't do that here because it could
    // destroy the OpenGL context which would destroy `self`
    //// r5 - replace 'self' by 'this'
    bool reconfigure_frontend =
      this->internal_upscaling != upscaling ||
      this->display_vram != display_vram;

    this->internal_upscaling = upscaling;
    this->display_vram = display_vram;
    this->internal_color_depth = depth;

    return reconfigure_frontend;
}
/* Setup 2 triangles that cover the entire framebuffer
then copy the displayed portion of the screen from fb_out */
void GlRenderer::finalize_frame()
{
    // Draw pending commands
    this->draw();

    // We can now render to teh frontend's buffer
    this->bind_libretro_framebuffer();

    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    /* If the display is off, just clear the screen */
    if (this->display_off) {
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    else {
        // Bind 'fb_out' to texture unit 1
        this->fb_out->bind(GL_TEXTURE1);

        // First we draw the visible part of fb_out
        uint16_t fb_x_start = this->config->display_top_left[0];
        uint16_t fb_y_start = this->config->display_top_left[1];
        uint16_t fb_width = this->config->display_resolution[0];
        uint16_t fb_height = this->config->display_resolution[1];

	GLint depth_24bpp = (GLint) this->config->display_24bpp;

	if (this->display_vram) {
	  // Display the entire VRAM as a 16bpp buffer
	  fb_x_start = 0;
	  fb_y_start = 0;
	  fb_width = VRAM_WIDTH_PIXELS;
	  fb_height = VRAM_HEIGHT;

	  depth_24bpp = 0;
	}

        uint16_t fb_x_end = fb_x_start + fb_width;
        uint16_t fb_y_end = fb_y_start + fb_height;

        this->output_buffer->clear();

        OutputVertex slice[4] =
        {
            { {-1.0, -1.0}, {fb_x_start,    fb_y_end}   },
            { { 1.0, -1.0}, {fb_x_end,      fb_y_end}   },
            { {-1.0,  1.0}, {fb_x_start,    fb_y_start} },
            { { 1.0,  1.0}, {fb_x_end,      fb_y_start} }
        };
        this->output_buffer->push_slice(slice, 4);

        this->output_buffer->program->uniform1i("fb", 1);
        this->output_buffer->program->uniform1i("depth_24bpp", depth_24bpp);
        this->output_buffer->program->uniform1ui( "internal_upscaling",
                                                    this->internal_upscaling);
        this->output_buffer->draw(GL_TRIANGLE_STRIP);
    }

    // Cleanup OpenGL context before returning to the frontend
    /* All of these GL calls are also done in glsm_ctl(UNBIND) */
    glDisable(GL_BLEND);
    glBlendColor(0.0, 0.0, 0.0, 0.0);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glLineWidth(1.0);
    glClearColor(0.0, 0.0, 0.0, 0.0);

    // When using a hardware renderer we set the data pointer to
    // -1 to notify the frontend that the frame has been rendered
    // in the framebuffer.
    video_cb(   RETRO_HW_FRAME_BUFFER_VALID, this->frontend_resolution[0],
                this->frontend_resolution[1], 0);
}
/// Check if a new primitive's attributes are somehow incompatible
/// with the ones currently buffered, in which case we must force
/// a draw to flush the buffers.
void GlRenderer::maybe_force_draw(  size_t nvertices,
                                    GLenum draw_mode,
                                    bool semi_transparent,
                                    SemiTransparencyMode semi_transparency_mode)
{
    /* std::vector grows as much as we want. 'semi_transparent_vertices' is meant
    to have a capacity of VERTEX_BUFFER_LEN. We'll use that constant in the
    subtraction below. */
    size_t semi_transparent_remaining_capacity =
        (size_t) VERTEX_BUFFER_LEN
        - this->semi_transparent_vertices.size();

    bool force_draw =
        // Check if we have enough room left in the buffer
        this->command_buffer->remaining_capacity() < nvertices ||
        semi_transparent_remaining_capacity < nvertices ||
        // Check if we're changing the draw mode (line <=> triangle)
        this->command_draw_mode != draw_mode ||
        // Check if we're changing the semi-transparency mode
        (semi_transparent &&
        !this->semi_transparent_vertices.empty() &&
        this->semi_transparency_mode != semi_transparency_mode);

    if (force_draw) {
        this->draw();

        // Update the state machine for the next primitive
        this->command_draw_mode = draw_mode;
    }

    if (semi_transparent) {
      this->semi_transparency_mode = semi_transparency_mode;
    }
}

void GlRenderer::set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
    // Finish drawing anything with the current offset
    this->draw();
    this->mask_set_or   = mask_set_or;
    this->mask_eval_and = mask_eval_and;
}

void GlRenderer::set_draw_offset(int16_t x, int16_t y)
{
    // Finish drawing anything with the current offset
    this->draw();
    this->config->draw_offset[0] = x;
    this->config->draw_offset[1] = y;
}

void GlRenderer::set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx,
      uint8_t twy)
{
    this->tex_x_mask = ~(tww << 3);
    this->tex_x_or = (twx & tww) << 3;
    this->tex_y_mask = ~(twh << 3);
    this->tex_y_or = (twy & twh) << 3;
}

void GlRenderer::set_draw_area(uint16_t top_left[2],
			       uint16_t bot_right[2])
{
    // Finish drawing anything in the current area
    this->draw();

    this->config->draw_area_top_left[0] = top_left[0];
    this->config->draw_area_top_left[1] = top_left[1];
    this->config->draw_area_bot_right[0] = bot_right[0];
    this->config->draw_area_bot_right[1] = bot_right[1];

    this->apply_scissor();
}

void GlRenderer::set_display_mode(  uint16_t top_left[2],
                                    uint16_t resolution[2],
                                    bool depth_24bpp)
{
    this->config->display_top_left[0] = top_left[0];
    this->config->display_top_left[1] = top_left[1];

    this->config->display_resolution[0] = resolution[0];
    this->config->display_resolution[1] = resolution[1];
    this->config->display_24bpp = depth_24bpp;
}

void GlRenderer::push_triangle( CommandVertex v[3],
                                SemiTransparencyMode semi_transparency_mode)
{
    this->maybe_force_draw( 3, GL_TRIANGLES,
                            v[0].semi_transparent == 1,
                            semi_transparency_mode);

    int16_t z = this->primitive_ordering;
    this->primitive_ordering += 1;

    size_t slice_len = 3;
    size_t i;

    for (i = 0; i < slice_len; ++i) {
        v[i].position[2] = z;
	v[i].texture_window[0] = tex_x_mask;
	v[i].texture_window[1] = tex_x_or;
	v[i].texture_window[2] = tex_y_mask;
	v[i].texture_window[3] = tex_y_or;
    }

    bool needs_opaque_draw =
        !(v[0].semi_transparent == 1) ||
        // Textured semi-transparent polys can contain opaque
        // texels (when bit 15 of the color is set to
        // 0). Therefore they're drawn twice, once for the opaque
        // texels and once for the semi-transparent ones
        v[0].texture_blend_mode != 0;

    if (needs_opaque_draw) {
        this->command_buffer->push_slice(v, slice_len);
    }

    if (v[0].semi_transparent == 1) {
        /*  self.semi_transparent_vertices.extend_from_slice(&v); */
        size_t i;
        for (i = 0; i < slice_len; ++i) {
            this->semi_transparent_vertices.push_back(v[i]);
        }
    }
}

void GlRenderer::push_sprite( CommandVertex v[6],
                                SemiTransparencyMode semi_transparency_mode)
{
    this->maybe_force_draw( 6, GL_TRIANGLES,
                            v[0].semi_transparent == 1,
                            semi_transparency_mode);

    int16_t z = this->primitive_ordering;
    this->primitive_ordering += 1;

    size_t slice_len = 6;
    size_t i;

    for (i = 0; i < slice_len; ++i) {
        v[i].position[2] = z;
	v[i].texture_window[0] = tex_x_mask;
	v[i].texture_window[1] = tex_x_or;
	v[i].texture_window[2] = tex_y_mask;
	v[i].texture_window[3] = tex_y_or;
    }

    bool needs_opaque_draw =
        !(v[0].semi_transparent == 1) ||
        // Textured semi-transparent polys can contain opaque
        // texels (when bit 15 of the color is set to
        // 0). Therefore they're drawn twice, once for the opaque
        // texels and once for the semi-transparent ones
        v[0].texture_blend_mode != 0;

    if (needs_opaque_draw) {
        this->command_buffer->push_slice(v, slice_len);
    }

    if (v[0].semi_transparent == 1) {
        /*  self.semi_transparent_vertices.extend_from_slice(&v); */
        size_t i;
        for (i = 0; i < slice_len; ++i) {
            this->semi_transparent_vertices.push_back(v[i]);
        }
    }
}

void GlRenderer::push_line( CommandVertex v[2],
                            SemiTransparencyMode semi_transparency_mode)
{
    this->maybe_force_draw( 2, GL_LINES,
                            v[0].semi_transparent == 1,
                            semi_transparency_mode);

    int16_t z = this->primitive_ordering;
    this->primitive_ordering += 1;

    size_t slice_len = 2;
    size_t i;
    for (i = 0; i < slice_len; ++i) {
        v[i].position[2] = z;
    }

    if (v[0].semi_transparent == 1) {
        size_t i;
        for (i = 0; i < slice_len; ++i) {
            this->semi_transparent_vertices.push_back(v[i]);
        }
    } else {
        this->command_buffer->push_slice(v, slice_len);
    }
}

void GlRenderer::fill_rect( uint8_t color[3],
                            uint16_t top_left[2],
                            uint16_t dimensions[2])
{
    // Draw pending commands
    this->draw();

    // Fill rect ignores the draw area. Save the previous value
    // and reconfigure the scissor box to the fill rectangle
    // instead.
    uint16_t draw_area_top_left[2] = {
        this->config->draw_area_top_left[0],
        this->config->draw_area_top_left[1]
    };
    uint16_t draw_area_bot_right[2] = {
        this->config->draw_area_bot_right[0],
        this->config->draw_area_bot_right[1]
    };

    this->config->draw_area_top_left[0] = top_left[0];
    this->config->draw_area_top_left[1] = top_left[1];
    this->config->draw_area_bot_right[0] = top_left[0] + dimensions[0];
    this->config->draw_area_bot_right[1] = top_left[1] + dimensions[1];

    this->apply_scissor();

    /* This scope is intentional, just like in the Rust version */
    {
        // Bind the out framebuffer
        Framebuffer _fb = Framebuffer(this->fb_out);

        glClearColor(   (float) color[0] / 255.0,
                        (float) color[1] / 255.0,
                        (float) color[2] / 255.0,
                        // XXX Not entirely sure what happens to
                        // the mask bit in fill_rect commands
                        0.0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Reconfigure the draw area
    this->config->draw_area_top_left[0]    = draw_area_top_left[0];
    this->config->draw_area_top_left[1]    = draw_area_top_left[1];
    this->config->draw_area_bot_right[0]   = draw_area_bot_right[0];
    this->config->draw_area_bot_right[1]   = draw_area_bot_right[1];

    this->apply_scissor();
}

void GlRenderer::copy_rect( uint16_t source_top_left[2],
                            uint16_t target_top_left[2],
                            uint16_t dimensions[2])
{
    // Draw pending commands
    this->draw();

    uint32_t upscale = this->internal_upscaling;

    GLint src_x = (GLint) source_top_left[0] * (GLint) upscale;
    GLint src_y = (GLint) source_top_left[1] * (GLint) upscale;
    GLint dst_x = (GLint) target_top_left[0] * (GLint) upscale;
    GLint dst_y = (GLint) target_top_left[1] * (GLint) upscale;

    GLsizei w = (GLsizei) dimensions[0] * (GLsizei) upscale;
    GLsizei h = (GLsizei) dimensions[1] * (GLsizei) upscale;

    // XXX CopyImageSubData gives undefined results if the source
    // and target area overlap, this should be handled
    // explicitely
    /* TODO - OpenGL 4.3 and GLES 3.2 requirement! FIXME! */
    glCopyImageSubData( this->fb_out->id, GL_TEXTURE_2D, 0, src_x, src_y, 0,
                        this->fb_out->id, GL_TEXTURE_2D, 0, dst_x, dst_y, 0,
                        w, h, 1 );

    get_error();
}

std::vector<Attribute> CommandVertex::attributes()
{
    std::vector<Attribute> result;

    result.push_back( Attribute("position",             offsetof(CommandVertex, position),              GL_SHORT,           3) );
    result.push_back( Attribute("color",                offsetof(CommandVertex, color),                 GL_UNSIGNED_BYTE,   3) );
    result.push_back( Attribute("texture_coord",        offsetof(CommandVertex, texture_coord),         GL_UNSIGNED_SHORT,  2) );
    result.push_back( Attribute("texture_page",         offsetof(CommandVertex, texture_page),          GL_UNSIGNED_SHORT,  2) );
    result.push_back( Attribute("clut",                 offsetof(CommandVertex, clut),                  GL_UNSIGNED_SHORT,  2) );
    result.push_back( Attribute("texture_blend_mode",   offsetof(CommandVertex, texture_blend_mode),    GL_UNSIGNED_BYTE,   1) );
    result.push_back( Attribute("depth_shift",          offsetof(CommandVertex, depth_shift),           GL_UNSIGNED_BYTE,   1) );
    result.push_back( Attribute("dither",               offsetof(CommandVertex, dither),                GL_UNSIGNED_BYTE,   1) );
    result.push_back( Attribute("semi_transparent",     offsetof(CommandVertex, semi_transparent),      GL_UNSIGNED_BYTE,   1) );
    result.push_back( Attribute("texture_window",       offsetof(CommandVertex, texture_window),        GL_UNSIGNED_BYTE,   4) );

    return result;
}

std::vector<Attribute> OutputVertex::attributes()
{
    std::vector<Attribute> result;

    result.push_back( Attribute("position", offsetof(OutputVertex, position), GL_FLOAT,             2) );
    result.push_back( Attribute("fb_coord", offsetof(OutputVertex, fb_coord), GL_UNSIGNED_SHORT,    2) );

    return result;
}

std::vector<Attribute> ImageLoadVertex::attributes()
{
    std::vector<Attribute> result;

    result.push_back( Attribute("position", offsetof(ImageLoadVertex, position), GL_UNSIGNED_SHORT,    2) );

    return result;
}
