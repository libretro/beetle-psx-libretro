#include "rsx_lib_gl.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h> /* exit() */
#include <stddef.h> /* offsetof() */

#include <stdexcept>

#include <glsm/glsmsym.h>

#include <boolean.h>

#include <glsm/glsmsym.h>
#include <vector>
#include <cstdio>
#include <stdint.h>

#include <map>
#include <string>
#include <algorithm>

#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"
#include "libretro.h"
#include "libretro_options.h"

#include "rsx/rsx_intf.h" //enums

#define DRAWBUFFER_IS_EMPTY(x)           ((x)->map_index == 0)
#define DRAWBUFFER_REMAINING_CAPACITY(x) ((x)->capacity - (x)->map_index)
#define DRAWBUFFER_NEXT_INDEX(x)         ((x)->map_start + (x)->map_index)

#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT       0x000
#endif

#include "shaders_gl/command_vertex.glsl.h"
#include "shaders_gl/command_fragment.glsl.h"
#define FILTER_XBR
#include "shaders_gl/command_fragment.glsl.h"
#include "shaders_gl/command_vertex.glsl.h"
#undef FILTER_XBR
#define FILTER_SABR
#include "shaders_gl/command_fragment.glsl.h"
#undef FILTER_SABR
#define FILTER_BILINEAR
#include "shaders_gl/command_fragment.glsl.h"
#undef FILTER_BILINEAR
#define FILTER_3POINT
#include "shaders_gl/command_fragment.glsl.h"
#undef FILTER_3POINT
#define FILTER_JINC2
#include "shaders_gl/command_fragment.glsl.h"
#undef FILTER_JINC2
#include "shaders_gl/output_vertex.glsl.h"
#include "shaders_gl/output_fragment.glsl.h"
#include "shaders_gl/image_load_vertex.glsl.h"
#include "shaders_gl/image_load_fragment.glsl.h"

#include "libretro.h"
#include "libretro_options.h"

#if 0 || defined(__APPLE__)
#define NEW_COPY_RECT
static const GLushort indices[6] = {0, 1, 2, 2, 1, 3};
#else
static const GLushort indices[6] = {0, 1, 2, 1, 2, 3};
#endif

#define VRAM_WIDTH_PIXELS 1024
#define VRAM_HEIGHT 512
#define VRAM_PIXELS (VRAM_WIDTH_PIXELS * VRAM_HEIGHT)

extern retro_log_printf_t log_cb;

/* How many vertices we buffer before forcing a draw. Since the
 * indexes are stored on 16bits we need to make sure that the length
 * multiplied by 3 (for triple buffering) doesn't overflow 0xffff. */
static const unsigned int VERTEX_BUFFER_LEN = 0x4000;

/* Maximum number of indices for a vertex buffer. Since quads have
 * two duplicated vertices it can be up to 3/2 the vertex buffer
 * length */
static const unsigned int INDEX_BUFFER_LEN = ((VERTEX_BUFFER_LEN * 3 + 1) / 2);

typedef std::map<std::string, GLint> UniformMap;

enum VideoClock {
   VideoClock_Ntsc,
   VideoClock_Pal
};

enum FilterMode {
   FILTER_MODE_NEAREST,
   FILTER_MODE_SABR,
   FILTER_MODE_XBR,
   FILTER_MODE_BILINEAR,
   FILTER_MODE_3POINT,
   FILTER_MODE_JINC2
};

/* State machine dealing with OpenGL context
 * destruction/reconstruction */
enum GlState
{
   /* OpenGL context is ready */
   GlState_Valid,
   /* OpenGL context has been destroyed (or is not created yet) */
   GlState_Invalid
};

enum SemiTransparencyMode {
   /* Source / 2 + destination / 2 */
   SemiTransparencyMode_Average = 0,
   /* Source + destination */
   SemiTransparencyMode_Add = 1,
   /* Destination - source */
   SemiTransparencyMode_SubtractSource = 2,
   /* Destination + source / 4 */
   SemiTransparencyMode_AddQuarterSource = 3,
};

struct Program
{
   GLuint id;
   /* Hash map of all the active uniforms in this program */
   UniformMap uniforms;
   char *info_log;
};

struct Shader
{
   GLuint id;
   char *info_log;
};

struct Attribute
{
   char name[32];
   size_t offset;
   /* Attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...) */
   GLenum type;
   GLint components;
};

struct CommandVertex {
   /* Position in PlayStation VRAM coordinates */
   float position[4];
   /* RGB color, 8bits per component */
   uint8_t color[3];
   /* Texture coordinates within the page */
   uint16_t texture_coord[2];
   /* Texture page (base offset in VRAM used for texture lookup) */
   uint16_t texture_page[2];
   /* Color Look-Up Table (palette) coordinates in VRAM */
   uint16_t clut[2];
   /* Blending mode: 0: no texture, 1: raw-texture, 2: texture-blended */
   uint8_t texture_blend_mode;
   /* Right shift from 16bits: 0 for 16bpp textures, 1 for 8bpp, 2
    * for 4bpp */
   uint8_t depth_shift;
   /* True if dithering is enabled for this primitive */
   uint8_t dither;
   /* 0: primitive is opaque, 1: primitive is semi-transparent */
   uint8_t semi_transparent;
   /* Texture limits of primtive */
   uint16_t texture_limits[4];
   /* Texture window mask/OR values */
   uint8_t texture_window[4];


   static std::vector<Attribute> attributes();
};

struct OutputVertex {
   /* Vertex position on the screen */
   float position[2];
   /* Corresponding coordinate in the framebuffer */
   uint16_t fb_coord[2];

   static std::vector<Attribute> attributes();
};

struct ImageLoadVertex {
   /* Vertex position in VRAM */
   uint16_t position[2];

   static std::vector<Attribute> attributes();
};

struct GlDisplayRect
{
   /* Analogous to DisplayRect in the Vulkan
    * renderer,but specified in native unscaled
    * glViewport coordinates. */
   int32_t x;
   int32_t y;
   uint32_t width;
   uint32_t height;
};

struct DrawConfig
{
   uint16_t display_top_left[2];
   uint16_t display_resolution[2];
   bool     display_24bpp;
   bool     display_off;
   int16_t  draw_offset[2];
   uint16_t draw_area_top_left[2];
   uint16_t draw_area_bot_right[2];
   uint16_t display_area_hrange[2];
   uint16_t display_area_vrange[2];
   bool     is_pal;
   bool     is_480i;
};

struct Texture
{
   GLuint id;
   uint32_t width;
   uint32_t height;
};

struct Framebuffer
{
   GLuint id;
   struct Texture _color_texture;
};

struct PrimitiveBatch {
   SemiTransparencyMode transparency_mode;
   /* GL_TRIANGLES or GL_LINES */
   GLenum draw_mode;
   bool opaque;
   bool set_mask;
   bool mask_test;
   /* First index */
   unsigned first;
   /* Count of indices */
   unsigned count;
};

template<typename T>
struct DrawBuffer
{
   /* OpenGL name for this buffer */
   GLuint id;
   /* Vertex Array Object containing the bindings for this
    * buffer. I'm assuming that each VAO will only use a single
    * buffer for simplicity. */
   GLuint vao;
   /* Program used to draw this buffer */
   Program* program;
   /* Currently mapped buffer range (write-only) */
   T *map;
   /* Number of elements T mapped at once in 'map' */
   size_t capacity;
   /* Index one-past the last element stored in 'map', relative to
    * the first element in 'map' */
   size_t map_index;
   /* Absolute offset of the 1st mapped element in the current
    * buffer relative to the beginning of the GL storage. */
   size_t map_start;
};

struct GlRenderer {
   /* Buffer used to handle PlayStation GPU draw commands */
   DrawBuffer<CommandVertex>* command_buffer;
   /* Buffer used to draw to the frontend's framebuffer */
   DrawBuffer<OutputVertex>* output_buffer;
   /* Buffer used to copy textures from 'fb_texture' to 'fb_out' */
   DrawBuffer<ImageLoadVertex>* image_load_buffer;

   GLushort vertex_indices[INDEX_BUFFER_LEN];
   /* Primitive type for the vertices in the command buffers
    * (TRIANGLES or LINES) */
   GLenum command_draw_mode;
   unsigned vertex_index_pos;
   std::vector<PrimitiveBatch> batches;
   /* Whether we're currently pushing opaque primitives or not */
   bool opaque;
   /* Current semi-transparency mode */
   SemiTransparencyMode semi_transparency_mode;
   /* Polygon mode (for wireframe) */
   GLenum command_polygon_mode;
   /* Texture used to store the VRAM for texture mapping */
   DrawConfig config;
   /* Framebuffer used as a shader input for texturing draw commands */
   Texture fb_texture;
   /* Framebuffer used as an output when running draw commands */
   Texture fb_out;
   /* Depth buffer for fb_out */
   Texture fb_out_depth;
   /* Current resolution of the frontend's framebuffer */
   uint32_t frontend_resolution[2];
   /* Current internal resolution upscaling factor */
   uint32_t internal_upscaling;
   /* Current internal color depth */
   uint8_t internal_color_depth;
   /* Counter for preserving primitive draw order in the z-buffer
    * since we draw semi-transparent primitives out-of-order. */
   int16_t primitive_ordering;
   /* Texture window mask/OR values */
   uint8_t tex_x_mask;
   uint8_t tex_x_or;
   uint8_t tex_y_mask;
   uint8_t tex_y_or;

   bool set_mask;
   bool mask_test;

   uint8_t filter_type;

   /* When true we display the entire VRAM buffer instead of just
    * the visible area */
   bool display_vram;

   /* Display Mode - GP1(08h) */
   enum width_modes curr_width_mode;

   /* When set we perform no horizontal padding */
   int crop_overscan;

   /* Experimental offset feature */
   int32_t image_offset_cycles;

   /* Image Crop option */
   unsigned image_crop;

   /* Scanline core options */
   int32_t initial_scanline;
   int32_t initial_scanline_pal;
   int32_t last_scanline;
   int32_t last_scanline_pal;
};

struct RetroGl
{
   GlRenderer *state_data;
   GlState state;
   VideoClock video_clock;
   bool inited;
};

static DrawConfig persistent_config = {
   {0, 0},         /* display_top_left */
   {MEDNAFEN_CORE_GEOMETRY_MAX_W, MEDNAFEN_CORE_GEOMETRY_MAX_H}, /* display_resolution */
   false,          /* display_24bpp */
   true,           /* display_off */
   {0, 0},         /* draw_area_top_left */
   {0, 0},         /* draw_area_dimensions */
   {0, 0},         /* draw_offset */
   {0x200, 0xC00}, /* display_area_hrange (hardware reset values)*/
   {0x10, 0x100},  /* display_area_vrange (hardware reset values)*/ 
   false,          /* is_pal */
   false,          /* is_480i */
};

static RetroGl static_renderer;

static bool has_software_fb = false;

extern "C" unsigned char widescreen_hack;
extern "C" unsigned char widescreen_hack_aspect_ratio_setting;
extern "C" bool content_is_pal;
extern "C" int aspect_ratio_setting;

#ifdef __cplusplus
extern "C"
{
#endif
   extern retro_environment_t environ_cb;
   extern retro_video_refresh_t video_cb;
#ifdef __cplusplus
}
#endif


#ifdef DEBUG
static void get_error(const char *msg)
{
   GLenum error = glGetError();
   switch (error)
   {
      case GL_NO_ERROR:
         log_cb(RETRO_LOG_INFO, "GL error flag: GL_NO_ERROR [%s]\n", msg);
         return;
      case GL_INVALID_ENUM:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_INVALID_ENUM [%s]\n", msg);
         break;
      case GL_INVALID_VALUE:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_INVALID_VALUE [%s]\n", msg);
         break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_INVALID_FRAMEBUFFER_OPERATION [%s]\n", msg);
         break;
      case GL_OUT_OF_MEMORY:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_OUT_OF_MEMORY [%s]\n", msg);
         break;
      case GL_STACK_UNDERFLOW:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_STACK_UNDERFLOW [%s]\n", msg);
         break;
      case GL_STACK_OVERFLOW:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_STACK_OVERFLOW [%s]\n", msg);
         break;
      case GL_INVALID_OPERATION:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_INVALID_OPERATION [%s]\n", msg);
         break;
      default:
         log_cb(RETRO_LOG_ERROR, "GL error flag: %d [%s]\n", (int) error, msg);
         break;
   }

   /* glGetError should always be called in a loop, until
    * it returns GL_NO_ERROR, if all error flags are to be reset. */
   while (error != GL_NO_ERROR)
      error = glGetError();
}
#endif

static bool Shader_init(
      struct Shader *shader,
      const char* source,
      GLenum shader_type)
{
   GLint status;
   GLint log_len = 0;
   GLuint id;

   shader->info_log = NULL;
   id               = glCreateShader(shader_type);

   if (id == 0)
   {
      log_cb(RETRO_LOG_ERROR, "An error occured creating the shader object\n");
      return false;
   }

   glShaderSource( id,
         1,
         &source,
         NULL);
   glCompileShader(id);

   status = (GLint) GL_FALSE;
   glGetShaderiv(id, GL_COMPILE_STATUS, &status);
   glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_len);

   if (log_len > 0)
   {
      GLsizei len;

      shader->info_log = (char*)malloc(log_len);
      len              = (GLsizei) log_len;

      glGetShaderInfoLog(id,
            len,
            &log_len,
            (char*)shader->info_log);

      if (log_len > 0)
         shader->info_log[log_len - 1] = '\0';
   }

   if (status == GL_FALSE)
   {
      log_cb(RETRO_LOG_ERROR, "Shader_init() - Shader compilation failed:\n%s\n", source);


      log_cb(RETRO_LOG_DEBUG, "Shader info log:\n%s\n", shader->info_log);

      return false;
   }

   shader->id = id;

   return true;
}

static void get_program_info_log(Program *pg, GLuint id)
{
   GLsizei len;
   GLint log_len = 0;

   glGetProgramiv(id, GL_INFO_LOG_LENGTH, &log_len);

   if (log_len <= 0)
      return;

   pg->info_log = (char*)malloc(log_len);
   len          = (GLsizei) log_len;

   glGetProgramInfoLog(id,
         len,
         &log_len,
         (char*)pg->info_log);

   if (log_len <= 0)
      return;

   pg->info_log[log_len - 1] = '\0';
}

static UniformMap load_program_uniforms(GLuint program)
{
   size_t u;
   UniformMap uniforms;
   /* Figure out how long a uniform name can be */
   GLint max_name_len = 0;
   GLint n_uniforms   = 0;

   glGetProgramiv( program,
         GL_ACTIVE_UNIFORMS,
         &n_uniforms );

   glGetProgramiv( program,
         GL_ACTIVE_UNIFORM_MAX_LENGTH,
         &max_name_len);

   for (u = 0; u < n_uniforms; ++u)
   {
      char name[256];
      size_t name_len = max_name_len;
      GLsizei len     = 0;
      GLint size      = 0;
      GLenum ty       = 0;

      glGetActiveUniform( program,
            (GLuint) u,
            (GLsizei) name_len,
            &len,
            &size,
            &ty,
            (char*) name);

      if (len <= 0)
      {
         log_cb(RETRO_LOG_WARN, "Ignoring uniform name with size %d\n", len);
         continue;
      }

      /* Retrieve the location of this uniform */
      GLint location = glGetUniformLocation(program, (const char*) name);

      if (location < 0)
      {
         log_cb(RETRO_LOG_WARN, "Uniform \"%s\" doesn't have a location", name);
         continue;
      }

      uniforms[name] = location;
   }

   return uniforms;
}


static bool Program_init(
      Program *program,
      Shader* vertex_shader,
      Shader* fragment_shader)
{
   GLint status;
   GLuint id;

   program->info_log = NULL;

   id                = glCreateProgram();

   if (id == 0)
   {
      log_cb(RETRO_LOG_ERROR, "Program_init() - glCreateProgram() returned 0\n");
      return false;
   }

   glAttachShader(id, vertex_shader->id);
   glAttachShader(id, fragment_shader->id);

   glLinkProgram(id);

   glDetachShader(id, vertex_shader->id);
   glDetachShader(id, fragment_shader->id);

   /* Check if the program linking was successful */
   status = GL_FALSE;
   glGetProgramiv(id, GL_LINK_STATUS, &status);
   get_program_info_log(program, id);

   if (status == GL_FALSE)
   {
      log_cb(RETRO_LOG_ERROR, "Program_init() - glLinkProgram() returned GL_FALSE\n");
      log_cb(RETRO_LOG_ERROR, "Program info log:\n%s\n", program->info_log);

      return false;
   }

   UniformMap uniforms = load_program_uniforms(id);

   program->id       = id;
   program->uniforms = uniforms;

   log_cb(RETRO_LOG_DEBUG, "Binding program for first time: %d\n", id);

   glUseProgram(id);

   log_cb(RETRO_LOG_DEBUG, "Unbinding program for first time: %d\n", id);

   glUseProgram(0);

   return true;
}

static void Program_free(Program *program)
{
   if (!program)
      return;

   if (glIsProgram(program->id))
      glDeleteProgram(program->id);
   if (program->info_log)
      free(program->info_log);
}

template<typename T>
static void DrawBuffer_enable_attribute(DrawBuffer<T> *drawbuffer, const char* attr)
{
   GLint index = glGetAttribLocation(drawbuffer->program->id, attr);

   if (index < 0)
      return;

   glBindVertexArray(drawbuffer->vao);
   glEnableVertexAttribArray(index);
}

template<typename T>
static void DrawBuffer_disable_attribute(DrawBuffer<T> *drawbuffer, const char* attr)
{
   GLint index = glGetAttribLocation(drawbuffer->program->id, attr);

   if (index < 0)
      return;

   glBindVertexArray(drawbuffer->vao);
   glDisableVertexAttribArray(index);
}

#ifdef DEBUG
#define DrawBuffer_push_slice(drawbuffer, slice, n, len) \
   assert(n <= DRAWBUFFER_REMAINING_CAPACITY(drawbuffer)); \
   assert(drawbuffer->map != NULL); \
   memcpy(drawbuffer->map + drawbuffer->map_index, slice, n * len); \
   drawbuffer->map_index += n;
#else
#define DrawBuffer_push_slice(drawbuffer, slice, n, len) \
   memcpy(drawbuffer->map + drawbuffer->map_index, slice, n * len); \
   drawbuffer->map_index += n;
#endif

template<typename T>
static void DrawBuffer_draw(DrawBuffer<T> *drawbuffer, GLenum mode)
{
   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);
   /* Unmap the active buffer */
   glUnmapBuffer(GL_ARRAY_BUFFER);

   drawbuffer->map = NULL;

   /* The VAO needs to be bound now or else glDrawArrays
    * errors out on some systems */
   glBindVertexArray(drawbuffer->vao);
   glUseProgram(drawbuffer->program->id);

   /* Length in number of vertices */
   glDrawArrays(mode, drawbuffer->map_start, drawbuffer->map_index);

   drawbuffer->map_start += drawbuffer->map_index;
   drawbuffer->map_index  = 0;

   DrawBuffer_map__no_bind(drawbuffer);
}

/* Map the buffer for write-only access */
template<typename T>
static void DrawBuffer_map__no_bind(DrawBuffer<T> *drawbuffer)
{
   GLintptr offset_bytes;
   void *m                = NULL;
   size_t element_size    = sizeof(T);
   GLsizeiptr buffer_size = drawbuffer->capacity * element_size;

   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);

   /* If we're already mapped something's wrong */
   assert(drawbuffer->map == NULL);

   /* We don't have enough room left to remap 'capacity',
    * start back from the beginning of the buffer. */
   if (drawbuffer->map_start > 2 * drawbuffer->capacity)
      drawbuffer->map_start = 0;

   offset_bytes = drawbuffer->map_start * element_size;

   m = glMapBufferRange(GL_ARRAY_BUFFER,
         offset_bytes,
         buffer_size,
         GL_MAP_WRITE_BIT |
         GL_MAP_INVALIDATE_RANGE_BIT);

   assert(m != NULL);

   drawbuffer->map = reinterpret_cast<T *>(m);
}

template<typename T>
static void DrawBuffer_free(DrawBuffer<T> *drawbuffer)
{
   if (!drawbuffer)
      return;

   /* Unmap the active buffer */
   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);
   glUnmapBuffer(GL_ARRAY_BUFFER);

   Program_free(drawbuffer->program);
   glDeleteBuffers(1, &drawbuffer->id);
   glDeleteVertexArrays(1, &drawbuffer->vao);

   delete drawbuffer->program;

   drawbuffer->map       = NULL;
   drawbuffer->id        = 0;
   drawbuffer->vao       = 0;
   drawbuffer->program   = NULL;
   drawbuffer->capacity  = 0;
   drawbuffer->map_index = 0;
   drawbuffer->map_start = 0;
}

template<typename T>
static void DrawBuffer_bind_attributes(DrawBuffer<T> *drawbuffer)
{
   unsigned i;
   GLint nVertexAttribs;

   glBindVertexArray(drawbuffer->vao);

   /* ARRAY_BUFFER is captured by VertexAttribPointer */
   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);

   std::vector<Attribute> attrs = T::attributes();
   GLint element_size = (GLint) sizeof( T );

   /* speculative: attribs enabled on VAO=0 (disabled) get applied to the VAO when created initially
    * as a core, we don't control the state entirely at this point. frontend may have enabled attribs.
    * we need to make sure they're all disabled before then re-enabling the attribs we want
    * (solves crashes on some drivers/compilers due to accidentally enabled attribs) */
   glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nVertexAttribs);

   for (i = 0; i < nVertexAttribs; i++)
      glDisableVertexAttribArray(i);

   for (std::vector<Attribute>::iterator it(attrs.begin()); it != attrs.end(); ++it)
   {
      Attribute& attr = *it;
      GLint index     = glGetAttribLocation(drawbuffer->program->id, attr.name);

      /* Don't error out if the shader doesn't use this
       * attribute, it could be caused by shader
       * optimization if the attribute is unused for
       * some reason. */
      if (index < 0)
         continue;

      glEnableVertexAttribArray((GLuint) index);

      /* This captures the buffer so that we don't have to bind it
       * when we draw later on, we'll just have to bind the vao */
      switch (attr.type)
      {
         case GL_BYTE:
         case GL_UNSIGNED_BYTE:
         case GL_SHORT:
         case GL_UNSIGNED_SHORT:
         case GL_INT:
         case GL_UNSIGNED_INT:
            glVertexAttribIPointer( index,
                  attr.components,
                  attr.type,
                  element_size,
                  (GLvoid*)attr.offset);
            break;
         case GL_FLOAT:
            glVertexAttribPointer(  index,
                  attr.components,
                  attr.type,
                  GL_FALSE,
                  element_size,
                  (GLvoid*)attr.offset);
            break;
         case GL_DOUBLE:
            glVertexAttribLPointer( index,
                  attr.components,
                  attr.type,
                  element_size,
                  (GLvoid*)attr.offset);
            break;
      }
   }
}

template<typename T>
static void DrawBuffer_new(DrawBuffer<T> *drawbuffer,
      const char *vertex_shader, const char *fragment_shader, size_t capacity)
{
   GLuint id = 0;
   size_t element_size = sizeof(T);
   Shader vs, fs;
   Program* program    = new Program;

   Shader_init(&vs, vertex_shader, GL_VERTEX_SHADER);
   Shader_init(&fs, fragment_shader, GL_FRAGMENT_SHADER);

   if (!Program_init(program, &vs, &fs))
   {
      delete program;
      return;
   }

   /* Program owns the two pointers, so we clean them up now */
   glDeleteShader(fs.id);
   glDeleteShader(vs.id);
   if (fs.info_log)
      free(fs.info_log);
   if (vs.info_log)
      free(vs.info_log);

   glGenVertexArrays(1, &id);

   drawbuffer->map       = NULL;
   drawbuffer->vao       = id;

   id                    = 0;

   /* Generate the buffer object */
   glGenBuffers(1, &id);

   drawbuffer->program  = program;
   drawbuffer->capacity = capacity;
   drawbuffer->id       = id;

   /* Create and map the buffer */
   glBindBuffer(GL_ARRAY_BUFFER, id);

   /* We allocate enough space for 3 times the buffer space and
    * we only remap one third of it at a time */
   GLsizeiptr storage_size = drawbuffer->capacity * element_size * 3;

   /* Since we store indexes in unsigned shorts we want to make
    * sure the entire buffer is indexable. */
   assert(drawbuffer->capacity * 3 <= 0xffff);

   glBufferData(GL_ARRAY_BUFFER, storage_size, NULL, GL_DYNAMIC_DRAW);

   DrawBuffer_bind_attributes<T>(drawbuffer);

   drawbuffer->map_index = 0;
   drawbuffer->map_start = 0;

   DrawBuffer_map__no_bind(drawbuffer);
}

static void Framebuffer_init(struct Framebuffer *fb,
      struct Texture* color_texture)
{
   GLuint id = 0;
   glGenFramebuffers(1, &id);

   fb->id                    = id;

   fb->_color_texture.id     = color_texture->id;
   fb->_color_texture.width  = color_texture->width;
   fb->_color_texture.height = color_texture->height;

   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->id);

   glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           color_texture->id,
                           0);

   GLenum col_attach_0 = GL_COLOR_ATTACHMENT0;

   glDrawBuffers(1, &col_attach_0);
   glViewport( 0,
               0,
               (GLsizei) color_texture->width,
               (GLsizei) color_texture->height);
}

static void Texture_init(
      struct Texture *tex,
      uint32_t width,
      uint32_t height,
      GLenum internal_format)
{
   GLuint id = 0;

   glGenTextures(1, &id);
   glBindTexture(GL_TEXTURE_2D, id);
   glTexStorage2D(GL_TEXTURE_2D,
                  1,
                  internal_format,
                  (GLsizei) width,
                  (GLsizei) height);

   tex->id     = id;
   tex->width  = width;
   tex->height = height;
}

static void Texture_set_sub_image_window(
      struct Texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      size_t row_len,
      GLenum format,
      GLenum ty,
      uint16_t* data)
{
   uint16_t x         = top_left[0];
   uint16_t y         = top_left[1];

   size_t index       = ((size_t) y) * row_len + ((size_t) x);

   /* TODO - Am I indexing data out of bounds? */
   uint16_t* sub_data = &( data[index] );

   glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) row_len);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glBindTexture(GL_TEXTURE_2D, tex->id);
   glTexSubImage2D(  GL_TEXTURE_2D,
                     0,
                     (GLint) top_left[0],
                     (GLint) top_left[1],
                     (GLsizei) resolution[0],
                     (GLsizei) resolution[1],
                     format,
                     ty,
                     (void*)sub_data);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

template<typename T>
static DrawBuffer<T>* DrawBuffer_build( const char* vertex_shader,
      const char* fragment_shader,
      size_t capacity)
{
   DrawBuffer<T> *t = new DrawBuffer<T>;
   DrawBuffer_new<T>(t, vertex_shader, fragment_shader, capacity);

   return t;
}

static void GlRenderer_draw(GlRenderer *renderer)
{
   if (!renderer || static_renderer.state == GlState_Invalid)
      return;

   Framebuffer _fb;
   int16_t x = renderer->config.draw_offset[0];
   int16_t y = renderer->config.draw_offset[1];

   if (renderer->command_buffer->program)
   {
      glUseProgram(renderer->command_buffer->program->id);
      glUniform2i(renderer->command_buffer->program->uniforms["offset"], (GLint)x, (GLint)y);
      /* We use texture unit 0 */
      glUniform1i(renderer->command_buffer->program->uniforms["fb_texture"], 0);
   }

   /* Bind the out framebuffer */
   Framebuffer_init(&_fb, &renderer->fb_out);

   glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
         GL_DEPTH_STENCIL_ATTACHMENT,
         renderer->fb_out_depth.id,
         0);

   glClear(GL_DEPTH_BUFFER_BIT);

   glStencilMask(1);
   glEnable(GL_STENCIL_TEST);

   /* Bind and unmap the command buffer */
   glBindBuffer(GL_ARRAY_BUFFER, renderer->command_buffer->id);
   glUnmapBuffer(GL_ARRAY_BUFFER);

   /* The VAO needs to be bound here or the glDrawElements calls
    * will error out on some systems */
   glBindVertexArray(renderer->command_buffer->vao);

   renderer->command_buffer->map = NULL;

   if (!renderer->batches.empty())
      renderer->batches.back().count = renderer->vertex_index_pos
         - renderer->batches.back().first;

   for (std::vector<PrimitiveBatch>::iterator it =
         renderer->batches.begin();
         it != renderer->batches.end();
         ++it)
   {
      /* Mask bits */
      if (it->set_mask)
         glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
      else
         glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);

      if (it->mask_test)
         glStencilFunc(GL_NOTEQUAL, 1, 1);
      else
         glStencilFunc(GL_ALWAYS, 1, 1);

      /* Blending */
      bool opaque = it->opaque;
      if (renderer->command_buffer->program)
         glUniform1ui(renderer->command_buffer->program->uniforms["draw_semi_transparent"], !opaque);
      if (opaque)
         glDisable(GL_BLEND);
      else
      {
         glEnable(GL_BLEND);

         GLenum blend_func = GL_FUNC_ADD;
         GLenum blend_src = GL_CONSTANT_ALPHA;
         GLenum blend_dst = GL_CONSTANT_ALPHA;

         switch (it->transparency_mode)
         {
            /* 0.5xB + 0.5 x F */
            case SemiTransparencyMode_Average:
               blend_func = GL_FUNC_ADD;
               /* Set to 0.5 with glBlendColor */
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
      }

      /* Drawing */
      if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      {
         /* This method doesn't call prepare_draw/finalize_draw itself, it
          * must be handled by the caller. This is because this command
          * can be called several times on the same buffer (i.e. multiple
          * draw calls between the prepare/finalize) */
         glDrawElements(it->draw_mode, it->count, GL_UNSIGNED_SHORT, &renderer->vertex_indices[it->first]);
      }
   }

   glDisable(GL_STENCIL_TEST);

   renderer->command_buffer->map_start += renderer->command_buffer->map_index;
   renderer->command_buffer->map_index  = 0;
   DrawBuffer_map__no_bind(renderer->command_buffer);

   renderer->primitive_ordering = 0;
   renderer->batches.clear();
   renderer->opaque = false;
   renderer->vertex_index_pos = 0;
   renderer->mask_test = false;
   renderer->set_mask = false;

   glDeleteFramebuffers(1, &_fb.id);
}

static void GlRenderer_upload_textures(
      GlRenderer *renderer,
      uint16_t top_left[2],
      uint16_t dimensions[2],
      uint16_t pixel_buffer[VRAM_PIXELS])
{
   if (!renderer)
      return;

   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glBindTexture(GL_TEXTURE_2D, renderer->fb_texture.id);
   glTexSubImage2D(  GL_TEXTURE_2D,
         0,
         (GLint) top_left[0],
         (GLint) top_left[1],
         (GLsizei) dimensions[0],
         (GLsizei) dimensions[1],
         GL_RGBA,
         GL_UNSIGNED_SHORT_1_5_5_5_REV,
         (void*)pixel_buffer);

   uint16_t x_start    = top_left[0];
   uint16_t x_end      = x_start + dimensions[0];
   uint16_t y_start    = top_left[1];
   uint16_t y_end      = y_start + dimensions[1];

   ImageLoadVertex slice[4] =
   {
      {   {x_start,   y_start }   },
      {   {x_end,     y_start }   },
      {   {x_start,   y_end   }   },
      {   {x_end,     y_end   }   }
   };

   if (renderer->image_load_buffer)
   {
      DrawBuffer_push_slice(renderer->image_load_buffer, &slice, 4,
            sizeof(ImageLoadVertex));

      if (renderer->image_load_buffer->program)
      {
         /* fb_texture is always at 1x */
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1i(renderer->image_load_buffer->program->uniforms["fb_texture"], 0);
         glUniform1ui(renderer->image_load_buffer->program->uniforms["internal_upscaling"], 1);

         glUseProgram(renderer->command_buffer->program->id);
         glUniform1i(renderer->command_buffer->program->uniforms["fb_texture"], 0);
      }
   }

   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

   /* Bind the output framebuffer */
   Framebuffer _fb;
   Framebuffer_init(&_fb, &renderer->fb_out);

   if (!DRAWBUFFER_IS_EMPTY(renderer->image_load_buffer))
      DrawBuffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

   glPolygonMode(GL_FRONT_AND_BACK, renderer->command_polygon_mode);
   glEnable(GL_SCISSOR_TEST);

#ifdef DEBUG
   get_error("GlRenderer_upload_textures");
#endif
   glDeleteFramebuffers(1, &_fb.id);
}

static void get_variables(uint8_t *upscaling, bool *display_vram)
{
   struct retro_variable var = {0};

   var.key = BEETLE_OPT(internal_resolution);
   if (upscaling)
   {
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         /* Same limitations as libretro.cpp */
         *upscaling = var.value[0] -'0';
         if (var.value[1] != 'x')
         {
            *upscaling  = (var.value[0] - '0') * 10;
            *upscaling += var.value[1] - '0';
         }
      }
   }

   if (display_vram)
   {
      var.key = BEETLE_OPT(display_vram);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "enabled"))
            *display_vram = true;
         else
            *display_vram = false;
      }
   }
}

static bool GlRenderer_new(GlRenderer *renderer, DrawConfig config)
{
   DrawBuffer<CommandVertex>* command_buffer;
   uint8_t upscaling         = 1;
   bool display_vram         = false;
   struct retro_variable var = {0};
   uint16_t top_left[2]      = {0, 0};
   uint16_t dimensions[2]    = {
      (uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};

   if (!renderer)
      return false;

   var.key = BEETLE_OPT(renderer_software_fb);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         has_software_fb = true;
      else
         has_software_fb = false;
   }
   else
      has_software_fb = true;

   get_variables(&upscaling, &display_vram);

   var.key = BEETLE_OPT(crop_overscan);
   int crop_overscan = true;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;
   }

   int32_t image_offset_cycles = 0;
   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }

   unsigned image_crop = 0;
   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   int32_t initial_scanline = 0;
   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline = atoi(var.value);
   }

   int32_t last_scanline = 239;
   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline = atoi(var.value);
   }

   int32_t initial_scanline_pal = 0;
   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline_pal = atoi(var.value);
   }

   int32_t last_scanline_pal = 287;
   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline_pal = atoi(var.value);
   }

   var.key = BEETLE_OPT(filter);
   uint8_t filter = FILTER_MODE_NEAREST;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "nearest"))
         filter = FILTER_MODE_NEAREST;
      else if (!strcmp(var.value, "SABR"))
         filter = FILTER_MODE_SABR;
      else if (!strcmp(var.value, "xBR"))
         filter = FILTER_MODE_XBR;
      else if (!strcmp(var.value, "bilinear"))
         filter = FILTER_MODE_BILINEAR;
      else if (!strcmp(var.value, "3-point"))
         filter = FILTER_MODE_3POINT;
      else if (!strcmp(var.value, "JINC2"))
         filter = FILTER_MODE_JINC2;

      renderer->filter_type = filter;
   }

   var.key = BEETLE_OPT(depth);
   uint8_t depth = 16;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "32bpp"))
         depth = 32;
   }

   var.key = BEETLE_OPT(dither_mode);
   dither_mode dither_mode = DITHER_NATIVE;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "internal resolution"))
         dither_mode = DITHER_UPSCALED;
      else if (!strcmp(var.value, "disabled"))
         dither_mode  = DITHER_OFF;
   }

   var.key = BEETLE_OPT(wireframe);
   bool wireframe = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         wireframe = true;
   }

   log_cb(RETRO_LOG_DEBUG, "Building OpenGL state (%dx internal res., %dbpp)\n", upscaling, depth);

   switch(renderer->filter_type)
   {
      case FILTER_MODE_SABR:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex_xbr,
               command_fragment_sabr,
               VERTEX_BUFFER_LEN);
         break;
      case FILTER_MODE_XBR:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex_xbr,
               command_fragment_xbr,
               VERTEX_BUFFER_LEN);
         break;
      case FILTER_MODE_BILINEAR:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex,
               command_fragment_bilinear,
               VERTEX_BUFFER_LEN);
         break;
      case FILTER_MODE_3POINT:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex,
               command_fragment_3point,
               VERTEX_BUFFER_LEN);
         break;
      case FILTER_MODE_JINC2:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex,
               command_fragment_jinc2,
               VERTEX_BUFFER_LEN);
         break;
      case FILTER_MODE_NEAREST:
      default:
         command_buffer = DrawBuffer_build<CommandVertex>(
               command_vertex,
               command_fragment,
               VERTEX_BUFFER_LEN);
   }

   DrawBuffer<OutputVertex>* output_buffer =
      DrawBuffer_build<OutputVertex>(
            output_vertex,
            output_fragment,
            4);

   DrawBuffer<ImageLoadVertex>* image_load_buffer =
      DrawBuffer_build<ImageLoadVertex>(
            image_load_vertex,
            image_load_fragment,
            4);

   uint32_t native_width  = (uint32_t) VRAM_WIDTH_PIXELS;
   uint32_t native_height = (uint32_t) VRAM_HEIGHT;

   /* Texture holding the raw VRAM texture contents. We can't
    * meaningfully upscale it since most games use paletted
    * textures. */
   Texture_init(&renderer->fb_texture, native_width, native_height, GL_RGB5_A1);

   if (dither_mode == DITHER_OFF)
   {
      /* Dithering is superfluous when we increase the internal
      * color depth, but users asked for it */
      DrawBuffer_disable_attribute(command_buffer, "dither");
   }
   else
   {
      DrawBuffer_enable_attribute(command_buffer, "dither");
   }

   GLenum command_draw_mode = wireframe ? GL_LINE : GL_FILL;

   if (command_buffer->program)
   {
      uint32_t dither_scaling = dither_mode == DITHER_UPSCALED ? 1 : upscaling;

      glUseProgram(command_buffer->program->id);
      glUniform1ui(command_buffer->program->uniforms["dither_scaling"], dither_scaling);
   }

   GLenum texture_storage = GL_RGB5_A1;
   switch (depth)
   {
      case 16:
         texture_storage = GL_RGB5_A1;
         break;
      case 32:
         texture_storage = GL_RGBA8;
         break;
      default:
         log_cb(RETRO_LOG_ERROR, "Unsupported depth %d\n", depth);
         exit(EXIT_FAILURE);
   }

   Texture_init(
         &renderer->fb_out,
         native_width  * upscaling,
         native_height * upscaling,
         texture_storage);

   Texture_init(
         &renderer->fb_out_depth,
         renderer->fb_out.width,
         renderer->fb_out.height,
         GL_DEPTH24_STENCIL8);

   renderer->filter_type = filter;
   renderer->command_buffer = command_buffer;
   renderer->vertex_index_pos = 0;
   renderer->command_draw_mode = GL_TRIANGLES;
   renderer->semi_transparency_mode =  SemiTransparencyMode_Average;
   renderer->command_polygon_mode = command_draw_mode;
   renderer->output_buffer = output_buffer;
   renderer->image_load_buffer = image_load_buffer;
   renderer->config = config;
   renderer->frontend_resolution[0] = 0;
   renderer->frontend_resolution[1] = 0;
   renderer->internal_upscaling = upscaling;
   renderer->internal_color_depth = depth;
   renderer->crop_overscan = crop_overscan;
   renderer->image_offset_cycles = image_offset_cycles;
   renderer->image_crop = image_crop;
   renderer->curr_width_mode = WIDTH_MODE_320;
   renderer->initial_scanline = initial_scanline;
   renderer->last_scanline = last_scanline;
   renderer->initial_scanline_pal = initial_scanline_pal;
   renderer->last_scanline_pal = last_scanline_pal;
   renderer->primitive_ordering = 0;
   renderer->tex_x_mask = 0;
   renderer->tex_x_or = 0;
   renderer->tex_y_mask = 0;
   renderer->tex_y_or = 0;
   renderer->display_vram = display_vram;
   renderer->set_mask  = false;
   renderer->mask_test = false;

   if (renderer)
      GlRenderer_upload_textures(renderer, top_left, dimensions, GPU_get_vram());

   return true;
}

static void GlRenderer_free(GlRenderer *renderer)
{
   if (!renderer)
      return;

   if (renderer->command_buffer)
   {
      DrawBuffer_free(renderer->command_buffer);
      delete renderer->command_buffer;
   }
   renderer->command_buffer = NULL;

   if (renderer->output_buffer)
   {
      DrawBuffer_free(renderer->output_buffer);
      delete renderer->output_buffer;
   }
   renderer->output_buffer = NULL;

   if (renderer->image_load_buffer)
   {
      DrawBuffer_free(renderer->image_load_buffer);
      delete renderer->image_load_buffer;
   }
   renderer->image_load_buffer = NULL;

   glDeleteTextures(1, &renderer->fb_texture.id);
   renderer->fb_texture.id     = 0;
   renderer->fb_texture.width  = 0;
   renderer->fb_texture.height = 0;

   glDeleteTextures(1, &renderer->fb_out.id);
   renderer->fb_out.id     = 0;
   renderer->fb_out.width  = 0;
   renderer->fb_out.height = 0;

   glDeleteTextures(1, &renderer->fb_out_depth.id);
   renderer->fb_out_depth.id     = 0;
   renderer->fb_out_depth.width  = 0;
   renderer->fb_out_depth.height = 0;

   unsigned i;
   for (i = 0; i < INDEX_BUFFER_LEN; i++)
      renderer->vertex_indices[i] = 0;
}

static inline void apply_scissor(GlRenderer *renderer)
{
   uint16_t _x = renderer->config.draw_area_top_left[0];
   uint16_t _y = renderer->config.draw_area_top_left[1];
   int _w      = renderer->config.draw_area_bot_right[0] - _x;
   int _h      = renderer->config.draw_area_bot_right[1] - _y;

   if (_w < 0)
      _w = 0;

   if (_h < 0)
      _h = 0;

   GLsizei upscale = (GLsizei)renderer->internal_upscaling;

   /* We need to scale those to match the internal resolution if
    * upscaling is enabled */
   GLsizei x = (GLsizei) _x * upscale;
   GLsizei y = (GLsizei) _y * upscale;
   GLsizei w = (GLsizei) _w * upscale;
   GLsizei h = (GLsizei) _h * upscale;

   glScissor(x, y, w, h);
}

static GlDisplayRect compute_gl_display_rect(GlRenderer *renderer)
{
   /* Current function logic mostly backported from Vulkan renderer */

   int32_t clock_div;
   switch (renderer->curr_width_mode)
   {
      case WIDTH_MODE_256:
         clock_div = 10;
         break;

      case WIDTH_MODE_320:
         clock_div = 8;
         break;

      case WIDTH_MODE_512:
         clock_div = 5;
         break;

      case WIDTH_MODE_640:
         clock_div = 4;
         break;

      /* The unusual case: 368px mode. Width is 364 px for
       * typical 368 mode games but often times something
       * different, which necessitates checking width mode 
       * rather than calculated pixel width */
      case WIDTH_MODE_368:
         clock_div = 7;
         break;

      default: //should never be here -- if we're here, something is terribly wrong
         break;
   }

   uint32_t width;
   int32_t x;
   if (renderer->crop_overscan)
   {
      width = (uint32_t) ((2560/clock_div) - renderer->image_crop);
      int32_t offset_cycles = renderer->image_offset_cycles;
      int32_t h_start = (int32_t) renderer->config.display_area_hrange[0];
      /* Restore old center behaviour is render_state.horiz_start is intentionally very high.
       * 938 fixes Gunbird (1008) and Mobile Light Force (EU release of Gunbird),
       * but this value should be lowerer in the future if necessary. */
      if ((renderer->config.display_area_hrange[0] < 938) && (renderer->crop_overscan == 2))
          x = floor((offset_cycles / (double) clock_div) - (renderer->image_crop / 2));
      else
          x = floor(((h_start - 608 + offset_cycles) / (double) clock_div) - (renderer->image_crop / 2));
   }
   else
   {
      width = (uint32_t) (2800/clock_div);
      int32_t offset_cycles = renderer->image_offset_cycles;
      int32_t h_start = (int32_t) renderer->config.display_area_hrange[0];
      x = floor((h_start - 488 + offset_cycles) / (double) clock_div);
   }

   uint32_t height;
   int32_t y;
   if (renderer->crop_overscan == 2)
   {
        if (renderer->config.is_pal)
        {
            int h = (renderer->config.display_area_vrange[1] - renderer->config.display_area_vrange[0]) - (287 - renderer->last_scanline_pal) - renderer->initial_scanline_pal;
            height = (h < 0 ? 0 : (uint32_t) h);
            y = renderer->last_scanline_pal - 287;
        }
        else
        {
            int h = (renderer->config.display_area_vrange[1] - renderer->config.display_area_vrange[0]) - (239 - renderer->last_scanline) - renderer->initial_scanline;
            height = (h < 0 ? 0 : (uint32_t) h);
            y = renderer->last_scanline - 239;
        }
   }
   if (renderer->crop_overscan != 2 || height > (renderer->config.is_pal? 288 : 240))
   {
        if (renderer->config.is_pal)
        {
            int h = renderer->last_scanline_pal - renderer->initial_scanline_pal + 1;
            height = (h < 0 ? 0 : (uint32_t) h);
            y = (308 - renderer->config.display_area_vrange[1]) + (renderer->last_scanline_pal - 287);
        }
        else
        {
            int h = renderer->last_scanline - renderer->initial_scanline + 1;
            height = (h < 0 ? 0 : (uint32_t) h);
            y = (256 - renderer->config.display_area_vrange[1]) + (renderer->last_scanline - 239);
        }
   }
   height *= (renderer->config.is_480i ? 2 : 1);
   y *= (renderer->config.is_480i ? 2 : 1);

   return {x, y, width, height};
}

static void bind_libretro_framebuffer(GlRenderer *renderer)
{
   if (!renderer)
      return;

   GLuint fbo;
   uint32_t w, h;
   uint32_t upscale   = renderer->internal_upscaling;
   uint32_t f_w       = renderer->frontend_resolution[0];
   uint32_t f_h       = renderer->frontend_resolution[1];
   uint32_t _w        = renderer->config.display_resolution[0];
   uint32_t _h        = renderer->config.display_resolution[1];

   /* vp_w and vp_h currently contingent on rsx_intf_set_display_mode behavior... */
   uint32_t vp_w = renderer->config.display_resolution[0];
   uint32_t vp_h = renderer->config.display_resolution[1];

   int32_t x, y;
   int32_t _x = 0;
   int32_t _y = 0;

   if (renderer->display_vram)
   {
      _x           = 0;
      _y           = 0;
      _w           = VRAM_WIDTH_PIXELS;
      _h           = VRAM_HEIGHT;

      //override vram fb dimensions for viewport
      vp_w = _w;
      vp_h = _h;
   }
   else
   {
      GlDisplayRect disp_rect = compute_gl_display_rect(renderer);
      _x = disp_rect.x;
      _y = disp_rect.y;
      _w = disp_rect.width;
      _h = disp_rect.height;
   }

   x       = _x * (int32_t) upscale;
   y       = _y * (int32_t) upscale;
   w       = (uint32_t) _w * upscale;
   h       = (uint32_t) _h * upscale;
   vp_w   *= upscale;
   vp_h   *= upscale;

   if (w != f_w || h != f_h)
   {
      /* We need to change the frontend's resolution */
      struct retro_game_geometry geometry;
      geometry.base_width  = MEDNAFEN_CORE_GEOMETRY_BASE_W;
      geometry.base_height = MEDNAFEN_CORE_GEOMETRY_BASE_H;

      /* Max parameters are ignored by this call */
      geometry.max_width  = MEDNAFEN_CORE_GEOMETRY_MAX_W * upscale;
      geometry.max_height = MEDNAFEN_CORE_GEOMETRY_MAX_H * upscale;

      geometry.aspect_ratio = rsx_common_get_aspect_ratio(content_is_pal, renderer->crop_overscan,
                                                          content_is_pal ? renderer->initial_scanline_pal :
                                                                           renderer->initial_scanline,
                                                          content_is_pal ? renderer->last_scanline_pal :
                                                                           renderer->last_scanline,
                                                          aspect_ratio_setting, renderer->display_vram, widescreen_hack,
                                                          widescreen_hack_aspect_ratio_setting);

      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);

      renderer->frontend_resolution[0] = w;
      renderer->frontend_resolution[1] = h;
   }

   /* Bind the output framebuffer provided by the frontend */
   fbo = glsm_get_current_framebuffer();
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
   glViewport((GLsizei) x, (GLsizei) y, (GLsizei) vp_w, (GLsizei) vp_h);
}

static bool retro_refresh_variables(GlRenderer *renderer)
{
   uint8_t filter            = FILTER_MODE_NEAREST;
   uint8_t upscaling         = 1;
   bool display_vram         = false;
   struct retro_variable var = {0};

   var.key = BEETLE_OPT(renderer_software_fb);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         has_software_fb = true;
      else
         has_software_fb = false;
   }
   else
      /* If 'BEETLE_OPT(renderer_software_fb)' option is not found, then
       * we are running in software mode */
      has_software_fb = true;

   get_variables(&upscaling, &display_vram);

   var.key = BEETLE_OPT(crop_overscan);
   int crop_overscan = 1;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;
   }

   int32_t image_offset_cycles;
   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }
   
   unsigned image_crop;
   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   int32_t initial_scanline = 0;
   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline = atoi(var.value);
   }

   int32_t last_scanline = 239;
   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline = atoi(var.value);
   }

   int32_t initial_scanline_pal = 0;
   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline_pal = atoi(var.value);
   }

   int32_t last_scanline_pal = 287;
   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline_pal = atoi(var.value);
   }

   var.key = BEETLE_OPT(filter);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "nearest"))
         filter = FILTER_MODE_NEAREST;
      else if (!strcmp(var.value, "SABR"))
         filter = FILTER_MODE_SABR;
      else if (!strcmp(var.value, "xBR"))
         filter = FILTER_MODE_XBR;
      else if (!strcmp(var.value, "bilinear"))
         filter = FILTER_MODE_BILINEAR;
      else if (!strcmp(var.value, "3-point"))
         filter = FILTER_MODE_3POINT;
      else if (!strcmp(var.value, "JINC2"))
         filter = FILTER_MODE_JINC2;
   }

   var.key = BEETLE_OPT(depth);
   uint8_t depth = 16;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "32bpp"))
         depth = 32;
   }

   var.key = BEETLE_OPT(dither_mode);
   dither_mode dither_mode = DITHER_NATIVE;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "1x(native)"))
      {
         dither_mode = DITHER_NATIVE;
         DrawBuffer_enable_attribute(renderer->command_buffer, "dither");
      }
      else if (!strcmp(var.value, "internal resolution"))
      {
         dither_mode = DITHER_UPSCALED;
         DrawBuffer_enable_attribute(renderer->command_buffer, "dither");
      }
      else if (!strcmp(var.value, "disabled"))
      {
         dither_mode  = DITHER_OFF;
         DrawBuffer_disable_attribute(renderer->command_buffer, "dither");
      }
   }

   var.key = BEETLE_OPT(wireframe);
   bool wireframe = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         wireframe = true;
   }

   bool rebuild_fb_out =
      upscaling != renderer->internal_upscaling ||
      depth != renderer->internal_color_depth;

   if (rebuild_fb_out)
   {
      if (dither_mode == DITHER_OFF)
         DrawBuffer_disable_attribute(renderer->command_buffer, "dither");
      else
         DrawBuffer_enable_attribute(renderer->command_buffer, "dither");

      uint32_t native_width  = (uint32_t) VRAM_WIDTH_PIXELS;
      uint32_t native_height = (uint32_t) VRAM_HEIGHT;

      uint32_t w = native_width  * upscaling;
      uint32_t h = native_height * upscaling;

      GLenum texture_storage = GL_RGB5_A1;
      switch (depth)
      {
         case 16:
            texture_storage = GL_RGB5_A1;
            break;
         case 32:
            texture_storage = GL_RGBA8;
            break;
         default:
            log_cb(RETRO_LOG_ERROR, "Unsupported depth %d\n", depth);
            exit(EXIT_FAILURE);
      }

      glDeleteTextures(1, &renderer->fb_out.id);
      renderer->fb_out.id     = 0;
      renderer->fb_out.width  = 0;
      renderer->fb_out.height = 0;
      Texture_init(&renderer->fb_out, w, h, texture_storage);

      /* This is a bit wasteful since it'll re-upload the data
       * to 'fb_texture' even though we haven't touched it but
       * this code is not very performance-critical anyway. */

      uint16_t top_left[2]   = {0, 0};
      uint16_t dimensions[2] = {(uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};

      if (renderer)
         GlRenderer_upload_textures(renderer, top_left, dimensions, GPU_get_vram());

      glDeleteTextures(1, &renderer->fb_out_depth.id);
      renderer->fb_out_depth.id     = 0;
      renderer->fb_out_depth.width  = 0;
      renderer->fb_out_depth.height = 0;
      Texture_init(&renderer->fb_out_depth, w, h, GL_DEPTH24_STENCIL8);
   }

   if (renderer->command_buffer->program)
   {
      uint32_t dither_scaling = dither_mode == DITHER_UPSCALED ? 1 : upscaling;

      glUseProgram(renderer->command_buffer->program->id);
      glUniform1ui(renderer->command_buffer->program->uniforms["dither_scaling"], dither_scaling);
   }

   renderer->command_polygon_mode = wireframe ? GL_LINE : GL_FILL;

   glLineWidth((GLfloat) upscaling);

   /* If the scaling factor has changed the frontend should be
   *  reconfigured. We can't do that here because it could
   *  destroy the OpenGL context which would destroy 'this' */
   bool reconfigure_frontend =
      renderer->internal_upscaling != upscaling ||
      renderer->display_vram != display_vram ||
      renderer->filter_type != filter;

   renderer->internal_upscaling     = upscaling;
   renderer->display_vram           = display_vram;
   renderer->internal_color_depth   = depth;
   renderer->filter_type            = filter;
   renderer->crop_overscan          = crop_overscan;
   renderer->image_offset_cycles    = image_offset_cycles;
   renderer->image_crop             = image_crop;
   renderer->initial_scanline       = initial_scanline;
   renderer->last_scanline          = last_scanline;
   renderer->initial_scanline_pal   = initial_scanline_pal;
   renderer->last_scanline_pal      = last_scanline_pal;

   return reconfigure_frontend;
}

static void vertex_preprocessing(
      GlRenderer *renderer,
      CommandVertex *v,
      unsigned count,
      GLenum mode,
      SemiTransparencyMode stm,
      bool mask_test,
      bool set_mask)
{
   if (!renderer)
      return;

   bool is_semi_transparent = v[0].semi_transparent == 1;
   bool is_textured         = v[0].texture_blend_mode != 0;
   /* Textured semi-transparent polys can contain opaque texels (when
    * bit 15 of the color is set to 0). Therefore they're drawn twice,
    * once for the opaque texels and once for the semi-transparent
    * ones. Only untextured semi-transparent triangles don't need to be
    * drawn as opaque. */
   bool is_opaque = !is_semi_transparent || is_textured;

   bool buffer_full         = DRAWBUFFER_REMAINING_CAPACITY(renderer->command_buffer) < count;

   if (buffer_full)
   {
      if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
         GlRenderer_draw(renderer);
   }

   int16_t z = renderer->primitive_ordering;
   renderer->primitive_ordering += 1;

   for (unsigned i = 0; i < count; i++)
   {
      v[i].position[2] = z;
      v[i].texture_window[0] = renderer->tex_x_mask;
      v[i].texture_window[1] = renderer->tex_x_or;
      v[i].texture_window[2] = renderer->tex_y_mask;
      v[i].texture_window[3] = renderer->tex_y_or;
   }

   if (renderer->batches.empty()
       || mode != renderer->command_draw_mode
       || is_opaque != renderer->opaque
       || (is_semi_transparent &&
           stm != renderer->semi_transparency_mode)
       || renderer->set_mask != set_mask
       || renderer->mask_test != mask_test)
   {
      if (!renderer->batches.empty())
      {
         PrimitiveBatch& last_batch = renderer->batches.back();
         last_batch.count = renderer->vertex_index_pos - last_batch.first;
      }
      PrimitiveBatch batch;
      batch.opaque = is_opaque;
      batch.draw_mode = mode;
      batch.transparency_mode = stm;
      batch.set_mask = set_mask;
      batch.mask_test = mask_test;
      batch.first = renderer->vertex_index_pos;
      batch.count = 0;
      renderer->batches.push_back(batch);

      renderer->semi_transparency_mode = stm;
      renderer->command_draw_mode = mode;
      renderer->opaque = is_opaque;
      renderer->set_mask = set_mask;
      renderer->mask_test = mask_test;
   }
}

static void vertex_add_blended_pass(
      GlRenderer *renderer, int vertex_index)
{
   if (!renderer->batches.empty())
   {
      PrimitiveBatch& last_batch = renderer->batches.back();
      last_batch.count = renderer->vertex_index_pos - last_batch.first;

      PrimitiveBatch batch;
      batch.opaque = false;
      batch.draw_mode = last_batch.draw_mode;
      batch.transparency_mode = last_batch.transparency_mode;
      batch.set_mask = true;
      batch.mask_test = last_batch.mask_test;
      batch.first = vertex_index;
      batch.count = 0;
      renderer->batches.push_back(batch);

      renderer->opaque = false;
      renderer->set_mask = true;
   }
}

static void push_primitive(
      GlRenderer *renderer,
      CommandVertex *v,
      unsigned count,
      GLenum mode,
      SemiTransparencyMode stm,
      bool mask_test,
      bool set_mask)
{
   if (!renderer)
      return;

   bool is_semi_transparent = v[0].semi_transparent   == 1;
   bool is_textured         = v[0].texture_blend_mode != 0;

   vertex_preprocessing(renderer, v, count, mode, stm, mask_test, set_mask);

   unsigned index     = DRAWBUFFER_NEXT_INDEX(renderer->command_buffer);
   unsigned index_pos = renderer->vertex_index_pos;

   for (unsigned i = 0; i < count; i++)
      renderer->vertex_indices[renderer->vertex_index_pos++] = index + i;

   /* Add transparent pass if needed */
   if (is_semi_transparent && is_textured)
      vertex_add_blended_pass(renderer, index_pos);

   DrawBuffer_push_slice(renderer->command_buffer, v, count,
         sizeof(CommandVertex)
         );
}

std::vector<Attribute> CommandVertex::attributes()
{
   std::vector<Attribute> result;
   Attribute attr;

   strcpy(attr.name, "position");
   attr.offset     = offsetof(CommandVertex, position);
   attr.type       = GL_FLOAT;
   attr.components = 4;

   result.push_back(attr);

   strcpy(attr.name, "color");
   attr.offset     = offsetof(CommandVertex, color);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 3;

   result.push_back(attr);

   strcpy(attr.name, "texture_coord");
   attr.offset     = offsetof(CommandVertex, texture_coord);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 2;

   result.push_back(attr);

   strcpy(attr.name, "texture_page");
   attr.offset     = offsetof(CommandVertex, texture_page);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 2;

   result.push_back(attr);

   strcpy(attr.name, "clut");
   attr.offset     = offsetof(CommandVertex, clut);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 2;

   result.push_back(attr);

   strcpy(attr.name, "texture_blend_mode");
   attr.offset     = offsetof(CommandVertex, texture_blend_mode);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 1;

   result.push_back(attr);

   strcpy(attr.name, "depth_shift");
   attr.offset     = offsetof(CommandVertex, depth_shift);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 1;

   result.push_back(attr);

   strcpy(attr.name, "dither");
   attr.offset     = offsetof(CommandVertex, dither);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 1;

   result.push_back(attr);

   strcpy(attr.name, "semi_transparent");
   attr.offset     = offsetof(CommandVertex, semi_transparent);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 1;

   result.push_back(attr);

   strcpy(attr.name, "texture_window");
   attr.offset     = offsetof(CommandVertex, texture_window);
   attr.type       = GL_UNSIGNED_BYTE;
   attr.components = 4;

   result.push_back(attr);

   strcpy(attr.name, "texture_limits");
   attr.offset     = offsetof(CommandVertex, texture_limits);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 4;

   result.push_back(attr);

   return result;
}

std::vector<Attribute> OutputVertex::attributes()
{
   std::vector<Attribute> result;
   Attribute attr;

   strcpy(attr.name, "position");
   attr.offset     = offsetof(OutputVertex, position);
   attr.type       = GL_FLOAT;
   attr.components = 2;

   result.push_back(attr);

   strcpy(attr.name, "fb_coord");
   attr.offset     = offsetof(OutputVertex, fb_coord);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 2;

   result.push_back(attr);

   return result;
}

std::vector<Attribute> ImageLoadVertex::attributes()
{
   std::vector<Attribute> result;
   Attribute attr;

   strcpy(attr.name, "position");
   attr.offset     = offsetof(ImageLoadVertex, position);
   attr.type       = GL_UNSIGNED_SHORT;
   attr.components = 2;

   result.push_back(attr);

   return result;
}

static void cleanup_gl_state(void)
{
   /* Cleanup OpenGL context before returning to the frontend */
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
}

extern void GPU_RestoreStateP1(bool val);
extern void GPU_RestoreStateP2(bool val);
extern void GPU_RestoreStateP3(void);

static void gl_context_reset(void)
{
   log_cb(RETRO_LOG_DEBUG, "gl_context_reset called.\n");
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

   if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
      return;

   static_renderer.state_data = new GlRenderer();

   if (GlRenderer_new(static_renderer.state_data, persistent_config))
   {
      static_renderer.inited = true;
      static_renderer.state  = GlState_Valid;

      GPU_RestoreStateP1(true);
      GPU_RestoreStateP2(true);
      GPU_RestoreStateP3();
   }
   else
   {
      log_cb(RETRO_LOG_WARN, "[gl_context_reset] GlRenderer_new failed. State will be invalid.\n");
   }
}

static void gl_context_destroy(void)
{
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_DESTROY, NULL);

   log_cb(RETRO_LOG_DEBUG, "gl_context_destroy called.\n");

   if (static_renderer.state_data)
   {
      GlRenderer_free(static_renderer.state_data);
      delete static_renderer.state_data;
   }

   static_renderer.state_data = NULL;
   static_renderer.state      = GlState_Invalid;
   static_renderer.inited     = false;
}

static bool gl_context_framebuffer_lock(void* data)
{
   return false;
}

extern "C" bool currently_interlaced;

static struct retro_system_av_info get_av_info(VideoClock std)
{
   struct retro_system_av_info info;
   unsigned int max_width                   = 0;
   unsigned int max_height                  = 0;
   uint8_t upscaling                        = 1;
   bool widescreen_hack                     = false;
   int widescreen_hack_aspect_ratio_setting = 1;
   bool display_vram                        = false;
   int crop_overscan                        = 0;
   int initial_scanline_ntsc                = 0;
   int last_scanline_ntsc                   = 239;
   int initial_scanline_pal                 = 0;
   int last_scanline_pal                    = 287;

   /* This function currently queries core options rather than
      checking GlRenderer state; possible to refactor? */

   get_variables(&upscaling, &display_vram);

   struct retro_variable var = {0};

   var.key = BEETLE_OPT(widescreen_hack);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         widescreen_hack = true;
   }

   var.key = BEETLE_OPT(widescreen_hack_aspect_ratio);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "16:10"))
         widescreen_hack_aspect_ratio_setting = 0;
      else if (!strcmp(var.value, "16:9"))
         widescreen_hack_aspect_ratio_setting = 1;
      else if (!strcmp(var.value, "18:9"))
         widescreen_hack_aspect_ratio_setting = 2;
      else if (!strcmp(var.value, "19:9"))
         widescreen_hack_aspect_ratio_setting = 3;
      else if (!strcmp(var.value, "20:9"))
         widescreen_hack_aspect_ratio_setting = 4;
      else if (!strcmp(var.value, "21:9"))
         widescreen_hack_aspect_ratio_setting = 5;
      else if (!strcmp(var.value, "32:9"))
         widescreen_hack_aspect_ratio_setting = 6;
   }

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline_ntsc = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline_ntsc = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline_pal = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline_pal = atoi(var.value);
   }

   if (display_vram)
   {
      max_width  = VRAM_WIDTH_PIXELS;
      max_height = VRAM_HEIGHT;
   }
   else
   {
      max_width  = MEDNAFEN_CORE_GEOMETRY_MAX_W;
      max_height = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   }

   memset(&info, 0, sizeof(info));

   /* The base resolution will be overriden using
    * ENVIRONMENT_SET_GEOMETRY before rendering a frame so
    * this base value is not really important */
   info.geometry.base_width     = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info.geometry.base_height    = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info.geometry.max_width      = max_width  * upscaling;
   info.geometry.max_height     = max_height * upscaling;
   info.geometry.aspect_ratio = rsx_common_get_aspect_ratio(std, crop_overscan,
                                                            std ? initial_scanline_pal : initial_scanline_ntsc,
                                                            std ? last_scanline_pal : last_scanline_ntsc,
                                                            aspect_ratio_setting, display_vram, widescreen_hack,
                                                            widescreen_hack_aspect_ratio_setting);

   info.timing.fps = rsx_common_get_timing_fps();
   info.timing.sample_rate = SOUND_FREQUENCY;

   return info;
}

void rsx_gl_get_system_av_info(struct retro_system_av_info *info)
{
   /* TODO/FIXME - This definition seems very backwards and duplicating work */

   /* This will possibly trigger the frontend to reconfigure itself */
   if (static_renderer.inited)
      rsx_gl_refresh_variables();

   struct retro_system_av_info result = get_av_info(static_renderer.video_clock);
   memcpy(info, &result, sizeof(result));
}

bool rsx_gl_open(bool is_pal)
{
   glsm_ctx_params_t params = {0};
   retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888;
   VideoClock clock = is_pal ? VideoClock_Pal : VideoClock_Ntsc;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f))
      return false;

   /* glsm related setup */
   params.context_reset         = gl_context_reset;
   params.context_destroy       = gl_context_destroy;
   params.framebuffer_lock      = gl_context_framebuffer_lock;
   params.environ_cb            = environ_cb;
   params.stencil               = false;
   params.imm_vbo_draw          = NULL;
   params.imm_vbo_disable       = NULL;
   params.context_type          = RETRO_HW_CONTEXT_OPENGL_CORE;
   params.major                 = 3;
   params.minor                 = 3;

   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      return false;

   /* No context until 'context_reset' is called */
   static_renderer.video_clock  = clock;

   return true;
}

void rsx_gl_close(void)
{
   static_renderer.state       = GlState_Invalid;
   static_renderer.video_clock = VideoClock_Ntsc;
}

void rsx_gl_refresh_variables(void)
{
   GlRenderer* renderer = NULL;
   if (!static_renderer.inited)
      return;

   switch (static_renderer.state)
   {
      case GlState_Valid:
         renderer = static_renderer.state_data;
         break;
      case GlState_Invalid:
         /* Nothing to be done if we don't have a GL context */
         return;
   }

   bool reconfigure_frontend = retro_refresh_variables(renderer);

   if (reconfigure_frontend)
   {
      /* The resolution has changed, we must tell the frontend
       * to change its format */
      struct retro_variable var = {0};

      struct retro_system_av_info av_info = get_av_info(static_renderer.video_clock);

      /* This call can potentially (but not necessarily) call
       * 'context_destroy' and 'context_reset' to reinitialize */
      bool ok = environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);

      if (!ok)
      {
         log_cb(RETRO_LOG_WARN, "Couldn't change frontend resolution\n");
         log_cb(RETRO_LOG_DEBUG, "Try resetting to enable the new configuration\n");
      }
   }
}

void rsx_gl_prepare_frame(void)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
   {
      log_cb(RETRO_LOG_ERROR, "[rsx_gl_prepare_frame] Renderer state marked as valid but state data is null.\n");
      return;
   }

   /* In case we're upscaling we need to increase the line width
    * proportionally */
   glLineWidth((GLfloat)renderer->internal_upscaling);
   glPolygonMode(GL_FRONT_AND_BACK, renderer->command_polygon_mode);
   glEnable(GL_SCISSOR_TEST);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   /* Used for PSX GPU command blending */
   glBlendColor(0.25, 0.25, 0.25, 0.5);

   apply_scissor(renderer);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, renderer->fb_texture.id);
}

static void compute_vram_framebuffer_dimensions(GlRenderer *renderer)
{
   /* Compute native PSX framebuffer dimensions for current frame
      and store results in renderer->config.display_resolution */

   if (!renderer)
      return;

   uint16_t clock_div;
   switch (renderer->curr_width_mode)
   {
      case WIDTH_MODE_256:
         clock_div = 10;
         break;

      case WIDTH_MODE_320:
         clock_div = 8;
         break;

      case WIDTH_MODE_512:
         clock_div = 5;
         break;

      case WIDTH_MODE_640:
         clock_div = 4;
         break;

      case WIDTH_MODE_368:
         clock_div = 7;
         break;

      default: // Should not be here, if we ever get here then log and crash?
         break;
   }

   // First we get the horizontal range in number of pixel clock period
   uint16_t fb_width = (renderer->config.display_area_hrange[1] - renderer->config.display_area_hrange[0]);

   // Then we apply the divider
   fb_width /= clock_div;

   // Then the rounding formula straight outta No$
   fb_width = (fb_width + 2) & ~3;

   uint16_t fb_height = (renderer->config.display_area_vrange[1] - renderer->config.display_area_vrange[0]);
   fb_height *= renderer->config.is_480i ? 2 : 1;

   renderer->config.display_resolution[0] = fb_width;
   renderer->config.display_resolution[1] = fb_height;
}

void rsx_gl_finalize_frame(const void *fb, unsigned width,
                           unsigned height, unsigned pitch)
{
   /* Setup 2 triangles that cover the entire framebuffer
      then copy the displayed portion of the screen from fb_out */

   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Draw pending commands */
   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   /* Calculate native PSX framebuffer dimensions to update renderer
      state before calling bind_libretro_framebuffer */
   compute_vram_framebuffer_dimensions(renderer);

   /* We can now render to the frontend's buffer */
   bind_libretro_framebuffer(renderer);

   glDisable(GL_SCISSOR_TEST);
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_BLEND);

   /* Clear the screen no matter what: prevents possible leftover 
      pixels from previous frame when loading save state for any 
      games not using standard framebuffer heights */
   glClearColor(0.0, 0.0, 0.0, 0.0);
   glClear(GL_COLOR_BUFFER_BIT);

   if (!renderer->config.display_off || renderer->display_vram)
   {
      /* Bind 'fb_out' to texture unit 1 */
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, renderer->fb_out.id);

      /* First we draw the visible part of fb_out */
      uint16_t fb_x_start = renderer->config.display_top_left[0];
      uint16_t fb_y_start = renderer->config.display_top_left[1];
      uint16_t fb_width   = renderer->config.display_resolution[0];
      uint16_t fb_height  = renderer->config.display_resolution[1];

      GLint depth_24bpp   = (GLint) renderer->config.display_24bpp;

      if (renderer->display_vram)
      {
         /* Display the entire VRAM as a 16bpp buffer */
         fb_x_start = 0;
         fb_y_start = 0;
         fb_width = VRAM_WIDTH_PIXELS;
         fb_height = VRAM_HEIGHT;

         depth_24bpp = 0;
      }

      if (renderer->output_buffer)
      {
         OutputVertex slice[4] =
         {
            { {-1.0, -1.0}, {0,         fb_height}   },
            { { 1.0, -1.0}, {fb_width , fb_height}   },
            { {-1.0,  1.0}, {0,         0} },
            { { 1.0,  1.0}, {fb_width,  0} }
         };

         if (renderer->output_buffer)
         {
            DrawBuffer_push_slice(renderer->output_buffer, &slice, 4,
                  sizeof(OutputVertex));

            if (renderer->output_buffer->program)
            {
               glUseProgram(renderer->output_buffer->program->id);
               glUniform1i(renderer->output_buffer->program->uniforms["fb"], 1);
               glUniform2ui(renderer->output_buffer->program->uniforms["offset"], fb_x_start, fb_y_start);

               glUniform1i(renderer->output_buffer->program->uniforms["depth_24bpp"], depth_24bpp);

               glUniform1ui(renderer->output_buffer->program->uniforms["internal_upscaling"], renderer->internal_upscaling);
            }

            if (!DRAWBUFFER_IS_EMPTY(renderer->output_buffer))
               DrawBuffer_draw(renderer->output_buffer, GL_TRIANGLE_STRIP);
         }
      }
   }

   /* TODO - Hack: copy fb_out back into fb_texture at the end of every
    * frame to make offscreen rendering kinda sorta work. Very messy
    * and slow. */
   {
      Framebuffer _fb;
      ImageLoadVertex slice[4] =
      {
         {   {   0,   0   }   },
         {   {1023,   0   }   },
         {   {   0, 511   }   },
         {   {1023, 511   }   },
      };

      if (renderer->image_load_buffer)
      {
         DrawBuffer_push_slice(renderer->image_load_buffer, &slice, 4,
               sizeof(ImageLoadVertex));

         if (renderer->image_load_buffer->program)
         {
            glUseProgram(renderer->image_load_buffer->program->id);
            glUniform1i(renderer->image_load_buffer->program->uniforms["fb_texture"], 1);
         }
      }

      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_BLEND);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

      Framebuffer_init(&_fb, &renderer->fb_texture);

      if (renderer->image_load_buffer->program)
      {
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1ui(renderer->image_load_buffer->program->uniforms["internal_upscaling"], renderer->internal_upscaling);
      }

      if (!DRAWBUFFER_IS_EMPTY(renderer->image_load_buffer))
         DrawBuffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

      glDeleteFramebuffers(1, &_fb.id);
   }

   cleanup_gl_state();

   /* When using a hardware renderer we set the data pointer to
    * -1 to notify the frontend that the frame has been rendered
    * in the framebuffer. */
   video_cb(   RETRO_HW_FRAME_BUFFER_VALID,
         renderer->frontend_resolution[0],
         renderer->frontend_resolution[1], 0);
}

void rsx_gl_set_tex_window(uint8_t tww, uint8_t twh, uint8_t twx, uint8_t twy)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->tex_x_mask = ~(tww << 3);
   renderer->tex_x_or   = (twx & tww) << 3;
   renderer->tex_y_mask = ~(twh << 3);
   renderer->tex_y_or   = (twy & twh) << 3;
}

void rsx_gl_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
   /* TODO/FIXME */
   return;
}

void rsx_gl_set_draw_offset(int16_t x, int16_t y)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Finish drawing anything with the current offset */
   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   renderer->config.draw_offset[0] = x;
   renderer->config.draw_offset[1] = y;
}

void rsx_gl_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{

   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Finish drawing anything in the current area */
   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   renderer->config.draw_area_top_left[0] = x0;
   renderer->config.draw_area_top_left[1] = y0;
   /* Draw area coordinates are inclusive */
   renderer->config.draw_area_bot_right[0] = x1 + 1;
   renderer->config.draw_area_bot_right[1] = y1 + 1;

   apply_scissor(renderer);
}

void rsx_gl_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_top_left[0] = xstart;
   renderer->config.display_top_left[1] = ystart;
}

void rsx_gl_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_area_hrange[0] = x1;
   renderer->config.display_area_hrange[1] = x2;
}

void rsx_gl_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_area_vrange[0] = y1;
   renderer->config.display_area_vrange[1] = y2;
}

void rsx_gl_set_display_mode(bool depth_24bpp,
                             bool is_pal,
                             bool is_480i,
                             int width_mode)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_24bpp         = depth_24bpp;

   renderer->config.is_pal  = is_pal;
   renderer->config.is_480i = is_480i;

   renderer->curr_width_mode = (enum width_modes) width_mode;
}

/* Draw commands */

void rsx_gl_push_triangle(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer         = static_renderer.state_data;
   if (!renderer)
      return;

   SemiTransparencyMode semi_transparency_mode    = SemiTransparencyMode_Add;
   bool semi_transparent        = false;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Average;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_SubtractSource;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
         break;
      default:
         break;
   }

   CommandVertex v[3] =
   {
      {
         {p0x, p0y, 0.95, p0w},   /* position */
         {
            (uint8_t) c0,
            (uint8_t) (c0 >> 8),
            (uint8_t) (c0 >> 16)
         }, /* color */
         {t0x, t0y},   /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      },
      {
         {p1x, p1y, 0.95, p1w }, /* position */
         {
            (uint8_t) c1,
            (uint8_t) (c1 >> 8),
            (uint8_t) (c1 >> 16)
         }, /* color */
         {t1x, t1y}, /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      },
      {
         {p2x, p2y, 0.95, p2w }, /* position */
         {
            (uint8_t) c2,
            (uint8_t) (c2 >> 8),
            (uint8_t) (c2 >> 16)
         }, /* color */
         {t2x, t2y}, /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      }
   };

   push_primitive(renderer, v, 3, GL_TRIANGLES,
         semi_transparency_mode, mask_test, set_mask);
}

void rsx_gl_push_quad(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      float p3x, float p3y, float p3w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint32_t c3,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t t3x, uint16_t t3y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode_Add;
   bool semi_transparent     = false;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Average;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_SubtractSource;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
         break;
      default:
         break;
   }

   CommandVertex v[4] =
   {
      {
         {p0x, p0y, 0.95, p0w},   /* position */
         {
            (uint8_t) c0,
            (uint8_t) (c0 >> 8),
            (uint8_t) (c0 >> 16)
         }, /* color */
         {t0x, t0y},   /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      },
      {
         {p1x, p1y, 0.95, p1w }, /* position */
         {
            (uint8_t) c1,
            (uint8_t) (c1 >> 8),
            (uint8_t) (c1 >> 16)
         }, /* color */
         {t1x, t1y}, /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      },
      {
         {p2x, p2y, 0.95, p2w }, /* position */
         {
            (uint8_t) c2,
            (uint8_t) (c2 >> 8),
            (uint8_t) (c2 >> 16)
         }, /* color */
         {t2x, t2y}, /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         {min_u, min_v, max_u, max_v},
      },
      {
         {p3x, p3y, 0.95, p3w }, /* position */
         {
            (uint8_t) c3,
            (uint8_t) (c3 >> 8),
            (uint8_t) (c3 >> 16)
         }, /* color */
         {t3x, t3y}, /* texture_coord */
         {texpage_x, texpage_y},
         {clut_x, clut_y},
         texture_blend_mode,
         depth_shift,
         (uint8_t) dither,
         semi_transparent,
         { min_u, min_v, max_u, max_v },
      },
   };

   bool is_semi_transparent = v[0].semi_transparent == 1;
   bool is_textured         = v[0].texture_blend_mode != 0;

   vertex_preprocessing(renderer, v, 4,
         GL_TRIANGLES, semi_transparency_mode, mask_test, set_mask);

   unsigned index     = DRAWBUFFER_NEXT_INDEX(renderer->command_buffer);
   unsigned index_pos = renderer->vertex_index_pos;

   for (unsigned i = 0; i < 6; i++)
      renderer->vertex_indices[renderer->vertex_index_pos++] = index + indices[i];

   /* Add transparent pass if needed */
   if (is_semi_transparent && is_textured)
      vertex_add_blended_pass(renderer, index_pos);

   DrawBuffer_push_slice(renderer->command_buffer, v, 4,
         sizeof(CommandVertex));
}

void rsx_gl_push_line(
      int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0, uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode_Add;
   bool semi_transparent = false;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Average;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_Add;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_SubtractSource;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
         break;
      default:
         break;
   }

   CommandVertex v[2] = {
      {
         {(float)p0x, (float)p0y, 0., 1.0}, /* position */
         {
            (uint8_t) c0,
            (uint8_t) (c0 >> 8),
            (uint8_t) (c0 >> 16)
         }, /* color */
         {0, 0}, /* texture_coord */
         {0, 0}, /* texture_page */
         {0, 0}, /* clut */
         0,      /* texture_blend_mode */
         0,      /* depth_shift */
         (uint8_t) dither,
         semi_transparent,
      },
      {
         {(float)p1x, (float)p1y, 0., 1.0}, /* position */
         {
            (uint8_t) c1,
            (uint8_t) (c1 >> 8),
            (uint8_t) (c1 >> 16)
         }, /* color */
         {0, 0}, /* texture_coord */
         {0, 0}, /* texture_page */
         {0, 0}, /* clut */
         0,      /* texture_blend_mode */
         0,      /* depth_shift */
         (uint8_t) dither,
         semi_transparent,
      }
   };

   push_primitive(renderer, v, 2,
         GL_LINES, semi_transparency_mode, mask_test, set_mask);
}

void rsx_gl_load_image(
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram,
      uint32_t mask_eval_and, uint32_t mask_set_or)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->set_mask     = mask_set_or != 0;
   renderer->mask_test    = mask_eval_and != 0;

   Framebuffer _fb;
   uint16_t top_left[2];
   uint16_t dimensions[2];

   top_left[0]            = x;
   top_left[1]            = y;
   dimensions[0]          = w;
   dimensions[1]          = h;

   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   Texture_set_sub_image_window(
         &renderer->fb_texture,
         top_left,
         dimensions,
         (size_t) VRAM_WIDTH_PIXELS,
         GL_RGBA,
         GL_UNSIGNED_SHORT_1_5_5_5_REV,
         vram);

   uint16_t x_start    = top_left[0];
   uint16_t x_end      = x_start + dimensions[0];
   uint16_t y_start    = top_left[1];
   uint16_t y_end      = y_start + dimensions[1];

   ImageLoadVertex slice[4] =
   {
      {   {x_start,   y_start }   },
      {   {x_end,     y_start }   },
      {   {x_start,   y_end   }   },
      {   {x_end,     y_end   }   }
   };

   if (renderer->image_load_buffer)
   {
      DrawBuffer_push_slice(renderer->image_load_buffer, slice, 4,
            sizeof(ImageLoadVertex));

      if (renderer->image_load_buffer->program)
      {
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1i(renderer->image_load_buffer->program->uniforms["fb_texture"], 0);
         /* fb_texture is always at 1x */
         glUniform1ui(renderer->image_load_buffer->program->uniforms["internal_upscaling"], 1);
      }
   }

   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

   /* Bind the output framebuffer */
   Framebuffer_init(&_fb, &renderer->fb_out);

   if (!DRAWBUFFER_IS_EMPTY(renderer->image_load_buffer))
      DrawBuffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

   glPolygonMode(GL_FRONT_AND_BACK, renderer->command_polygon_mode);
   glEnable(GL_SCISSOR_TEST);

#ifdef DEBUG
   get_error("rsx_gl_load_image");
#endif

   glDeleteFramebuffers(1, &_fb.id);
}


void rsx_gl_fill_rect(
      uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{

   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   uint8_t col[3]         = {(uint8_t) color, (uint8_t) (color >> 8), (uint8_t) (color >> 16)};

   /* Draw pending commands */
   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   /* Fill rect ignores the draw area. Save the previous value
    * and reconfigure the scissor box to the fill rectangle
    * instead. */
   uint16_t draw_area_top_left[2] = {
      renderer->config.draw_area_top_left[0],
      renderer->config.draw_area_top_left[1]
   };
   uint16_t draw_area_bot_right[2] = {
      renderer->config.draw_area_bot_right[0],
      renderer->config.draw_area_bot_right[1]
   };

   renderer->config.draw_area_top_left[0]  = top_left[0];
   renderer->config.draw_area_top_left[1]  = top_left[1];
   renderer->config.draw_area_bot_right[0] = top_left[0] + dimensions[0];
   renderer->config.draw_area_bot_right[1] = top_left[1] + dimensions[1];

   apply_scissor(renderer);

   /* This scope is intentional, just like in the Rust version */
   {
      /* Bind the out framebuffer */
      Framebuffer _fb;
      Framebuffer_init(&_fb, &renderer->fb_out);

      glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            renderer->fb_out_depth.id,
            0);

      glClearColor(   (float) col[0] / 255.0,
            (float) col[1] / 255.0,
            (float) col[2] / 255.0,
            /* TODO - XXX Not entirely sure what happens to
               the mask bit in fill_rect commands */
            0.0);
      glStencilMask(1);
      glClearStencil(0);
      glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

      glDeleteFramebuffers(1, &_fb.id);
   }

   /* Reconfigure the draw area */
   renderer->config.draw_area_top_left[0]    = draw_area_top_left[0];
   renderer->config.draw_area_top_left[1]    = draw_area_top_left[1];
   renderer->config.draw_area_bot_right[0]   = draw_area_bot_right[0];
   renderer->config.draw_area_bot_right[1]   = draw_area_bot_right[1];

   apply_scissor(renderer);
}

void rsx_gl_copy_rect(
      uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h,
      uint32_t mask_eval_and, uint32_t mask_set_or) /* TODO use mask for copy. See software renderer */
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   if (src_x == dst_x && src_y == dst_y)
     return;

   renderer->set_mask          = mask_set_or != 0;
   renderer->mask_test         = mask_eval_and != 0;

   uint16_t source_top_left[2] = {src_x, src_y};
   uint16_t target_top_left[2] = {dst_x, dst_y};
   uint16_t dimensions[2]      = {w, h};

   /* Draw pending commands */
   if (!DRAWBUFFER_IS_EMPTY(renderer->command_buffer))
      GlRenderer_draw(renderer);

   uint32_t upscale = renderer->internal_upscaling;

   GLint new_src_x = (GLint) source_top_left[0] * (GLint) upscale;
   GLint new_src_y = (GLint) source_top_left[1] * (GLint) upscale;
   GLint new_dst_x = (GLint) target_top_left[0] * (GLint) upscale;
   GLint new_dst_y = (GLint) target_top_left[1] * (GLint) upscale;

   GLsizei new_w = (GLsizei) dimensions[0] * (GLsizei) upscale;
   GLsizei new_h = (GLsizei) dimensions[1] * (GLsizei) upscale;

#ifdef NEW_COPY_RECT
   /* TODO/FIXME - buggy code!
    *
    * Dead or Alive/Tekken 3 (high-res interlaced game) has screen
    * flickering issues with this code! */

   /* The diagonal is duplicated. I originally used "1, 2, 1, 2" to
    *  duplicate the diagonal but I believe it was incorrect because of
    *  the OpenGL filling convention. At least it's what TinyTiger told
    *  me... */

   GLuint fb;

   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
   glFramebufferTexture(GL_READ_FRAMEBUFFER,
         GL_COLOR_ATTACHMENT0,
         renderer->fb_out.id,
         0);

   glReadBuffer(GL_COLOR_ATTACHMENT0);

   /* TODO - Can I bind the same texture to the framebuffer and
    * GL_TEXTURE_2D? Something tells me this is undefined
    * behaviour. I could use glReadPixels and glWritePixels instead
    * or something like that. */
   glBindTexture(GL_TEXTURE_2D, renderer->fb_out.id);
   glCopyTexSubImage2D( GL_TEXTURE_2D, 0, new_dst_x, new_dst_y,
                        new_src_x, new_src_y, new_w, new_h);

   glDeleteFramebuffers(1, &fb);
#else

   /* The diagonal is duplicated */

   /* XXX CopyImageSubData gives undefined results if the source
    * and target area overlap, this should be handled
    * explicitely */
   /* TODO - OpenGL 4.3 and GLES 3.2 requirement! FIXME! */
   glCopyImageSubData(  renderer->fb_out.id, GL_TEXTURE_2D, 0, new_src_x, new_src_y, 0,
                        renderer->fb_out.id, GL_TEXTURE_2D, 0, new_dst_x, new_dst_y, 0,
                        new_w, new_h, 1 );
#endif

#ifdef DEBUG
   get_error("rsx_gl_copy_rect");
#endif
}

void rsx_gl_toggle_display(bool status)
{
   if (static_renderer.state == GlState_Invalid)
      return;

   GlRenderer *renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_off = status;
}

bool rsx_gl_has_software_renderer(void)
{
   if (!static_renderer.inited)
      return false;
   return has_software_fb;
}
