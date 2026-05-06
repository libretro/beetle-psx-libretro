#include "rsx_lib_gl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* exit() */
#include <stddef.h> /* offsetof() */
#include <assert.h>

#include <glsym/glsym.h>

#include <boolean.h>

#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"
#include "libretro.h"
#include "libretro_options.h"

#include "rsx/rsx_intf.h" /* enums */
#include "beetle_psx_globals.h"

#define gl_draw_buffer_is_empty(x)           ((x)->map_index == 0)
#define gl_draw_buffer_remaining_capacity(x) ((x)->capacity - (x)->map_index)
#define gl_draw_buffer_next_index(x)         ((x)->map_start + (x)->map_index)

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

/* Quad triangulation order.
 *
 * A PSX quad is rendered as two triangles sharing one diagonal.
 * The choice of which diagonal-vertex pair to duplicate produced
 * visible seams along the shared edge on Apple desktop GL and
 * some GLES3 drivers when "{0,1,2,1,2,3}" was used; "{0,1,2,2,1,3}"
 * was reported to fix it.  Backface culling is disabled for the
 * PSX renderer so the winding direction does not affect
 * visibility, only rasteriser fill convention along the diagonal.
 *
 * This is unrelated to the runtime gl_caps detection introduced
 * below; it stays a build-time choice because the difference is
 * driver-rasteriser-specific and not safe to consolidate without
 * cross-platform visual testing. */
#if defined(__APPLE__) || defined(HAVE_OPENGLES3)
static const GLushort indices[6] = {0, 1, 2, 2, 1, 3};
#else
static const GLushort indices[6] = {0, 1, 2, 1, 2, 3};
#endif

/* === GL capability bookkeeping ============================
 *
 * Populated once at context_reset by gl_caps_init().  Call sites
 * that want a feature beyond the GL/GLES baseline of their
 * compile profile consult this struct rather than relying on
 * compile-time #ifdefs - the build-time GLES2/GLES3/desktop
 * split decides only which header bundle is available; what the
 * driver actually exposes at runtime is a separate question.
 *
 * Version-cap reminders for future contributors:
 *
 *   - Apple desktop OpenGL is permanently capped at GL 4.1 Core.
 *     macOS shipped 4.1 in 10.9 (2013) and never advanced before
 *     deprecating the API in 10.14.  Anything from 4.2+ -
 *     including glCopyImageSubData (4.3), compute shaders (4.3),
 *     and the core glDebugMessageCallback (4.3) - will never be
 *     available on Apple desktop builds.
 *
 *   - Apple iOS / iPadOS are capped at GLES 3.0.  GLES 3.1 / 3.2
 *     features are not available there.
 *
 *   - Linux/Windows desktop drivers commonly expose 4.6 today,
 *     but the floor for a libretro frontend can be as low as 2.1
 *     (legacy compat).
 *
 *   - Android GLES drivers are commonly 3.0/3.1/3.2; very old
 *     devices may report only GLES 2.0.
 *
 * Function pointers are resolved via the libretro frontend's
 * get_proc_address callback (hw_render.get_proc_address, set
 * by the frontend during SET_HW_RENDER).  This works regardless
 * of whether the function is declared in the static GL/GLES
 * header for the current build profile, so call sites never
 * reference symbols their headers might lack.
 *
 * Pattern at every call site:
 *
 *   if (gl_caps.fp_glCopyImageSubData)
 *      gl_caps.fp_glCopyImageSubData(...);
 *   else if (gl_caps.fp_glBlitFramebuffer)
 *      gl_caps.fp_glBlitFramebuffer(...);
 *   else
 *      <portable readback fallback>
 */

/* Function-pointer typedef calling convention.  Different
 * profiles spell this differently:
 *   - Desktop GL  defines APIENTRY (via <GL/gl.h>) and via the
 *     glsym layer also gets APIENTRYP.
 *   - GLES        defines GL_APIENTRY (via <GLES{2,3}/gl{2,3}.h>)
 *     and GL_APIENTRYP, but does not necessarily provide the
 *     non-prefixed APIENTRY/APIENTRYP.
 *
 * Pick whichever the build's GL header bundle actually provides;
 * it's just a calling-convention attribute so an empty
 * definition is correct on profiles that have no special
 * convention. */
#if defined(APIENTRYP)
#  define BEETLE_GL_APIENTRYP APIENTRYP
#elif defined(GL_APIENTRYP)
#  define BEETLE_GL_APIENTRYP GL_APIENTRYP
#elif defined(APIENTRY)
#  define BEETLE_GL_APIENTRYP APIENTRY *
#elif defined(GL_APIENTRY)
#  define BEETLE_GL_APIENTRYP GL_APIENTRY *
#else
#  define BEETLE_GL_APIENTRYP *
#endif

typedef enum gl_api_family
{
   GL_API_UNKNOWN = 0,
   GL_API_DESKTOP,
   GL_API_GLES
} gl_api_family;

typedef enum gl_profile
{
   GL_PROFILE_UNKNOWN = 0,
   GL_PROFILE_LEGACY, /* desktop pre-3.0, or 3.0+ compat profile */
   GL_PROFILE_CORE,   /* desktop 3.2+ core profile */
   GL_PROFILE_ES      /* GLES (no profile distinction) */
} gl_profile;

typedef void (BEETLE_GL_APIENTRYP PFN_BEETLE_GL_BLITFRAMEBUFFER)(
      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
      GLbitfield mask, GLenum filter);

typedef void (BEETLE_GL_APIENTRYP PFN_BEETLE_GL_COPYIMAGESUBDATA)(
      GLuint srcName, GLenum srcTarget, GLint srcLevel,
      GLint srcX, GLint srcY, GLint srcZ,
      GLuint dstName, GLenum dstTarget, GLint dstLevel,
      GLint dstX, GLint dstY, GLint dstZ,
      GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);

typedef struct gl_caps
{
   /* Identity */
   gl_api_family api;
   gl_profile    profile;
   int           version_major;
   int           version_minor;
   /* Composite version for easy ordered comparisons:
    *   GL  3.3 -> 0x0303,  GL  4.5 -> 0x0405
    *   GLES 3.0 -> 0x0300, GLES 3.2 -> 0x0302 */
   int           version_packed;

   /* Driver identification (logging only).  Pointers into
    * GL-owned static memory; valid for the lifetime of the
    * context. */
   const char *vendor;
   const char *renderer;
   const char *version_string;

   /* Resolved entry points.  NULL when not available. */
   PFN_BEETLE_GL_BLITFRAMEBUFFER  fp_glBlitFramebuffer;
   PFN_BEETLE_GL_COPYIMAGESUBDATA fp_glCopyImageSubData;

   /* Set to 1 when the detected version is below the floor
    * beetle's GL renderer needs to function (GL/GLES 3.0).
    * gl_context_reset checks this and refuses to activate the
    * renderer when set. */
   int unsupported;
} gl_caps_t;

static gl_caps_t gl_caps;

/* === GLES 2.x compile-time bridges ============================
 *
 * Beetle-PSX targets GL 3.3 Core / GLES 3.x.  The GLES 2.x build
 * (HAVE_OPENGLES + HAVE_OPENGLES2) does not currently produce a
 * working renderer and there are pre-existing compile errors in
 * the file unrelated to glsm folding.  These bridges narrow the
 * scope of "what's missing for GLES 2.x" so a future contributor
 * who wants to make GLES 2.x actually work can replace them with
 * proper runtime branches at the call sites that still need them
 * (per the gl_caps pattern).
 *
 * Two flavours:
 *
 *   - Macro-aliases to OES/EXT extension entry points: for
 *     functions GLES 2.x does not have in core but commonly has
 *     via a vendor extension.  rglgen resolves the OES/EXT
 *     symbol for us at context_reset time; if the running driver
 *     does not expose the extension the function pointer is NULL
 *     and any call crashes loudly (preferable to silent no-ops
 *     that paint over real bugs).
 *
 *   - Empty / harmless stubs: for functions that have no GLES 2.x
 *     equivalent at all (typed integer uniforms, polygon mode,
 *     etc.).  These should never be reached at runtime because
 *     gl_caps_init refuses to mark the renderer valid below
 *     GL/GLES 3.0 (see "version_packed >= 0x0300" gate); they
 *     exist only so the compile graph stays connected.
 *
 * Once a real GLES 2.x rendering path is implemented these
 * bridges should disappear, replaced by gl_caps-style runtime
 * branches at each call site. */
#if defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES2)

/* Aliases to extension entry points.  rglgen resolves the
 * OES/EXT-suffixed symbol; the call goes through that runtime
 * pointer when reachable. */
#define glBindVertexArray         glBindVertexArrayOES
#define glDeleteVertexArrays      glDeleteVertexArraysOES
#define glDrawBuffers             glDrawBuffersEXT
#define glFramebufferTexture      glFramebufferTextureOES
#define glGenVertexArrays         glGenVertexArraysOES
#define glMapBufferRange          glMapBufferRangeEXT
#define glTexStorage2D            glTexStorage2DEXT
#define glUnmapBuffer             glUnmapBufferOES

/* Stubs for functions with no GLES 2.x equivalent.  Unreachable
 * at runtime because gl_caps_init refuses the renderer below
 * GL/GLES 3.0; present only to keep the file compile-clean. */
static INLINE void glUniform1ui(GLint location, GLuint v0)
{
   (void)location; (void)v0;
}
static INLINE void glUniform2ui(GLint location, GLuint v0, GLuint v1)
{
   (void)location; (void)v0; (void)v1;
}
static INLINE void glVertexAttribIPointer(
      GLuint index, GLint size, GLenum type,
      GLsizei stride, const void *pointer)
{
   (void)index; (void)size; (void)type;
   (void)stride; (void)pointer;
}
static INLINE void glVertexAttribLPointer(
      GLuint index, GLint size, GLenum type,
      GLsizei stride, const void *pointer)
{
   (void)index; (void)size; (void)type;
   (void)stride; (void)pointer;
}

#endif /* HAVE_OPENGLES && HAVE_OPENGLES2 */

#define VRAM_WIDTH_PIXELS 1024
#define VRAM_HEIGHT 512
#define VRAM_PIXELS (VRAM_WIDTH_PIXELS * VRAM_HEIGHT)

extern retro_log_printf_t log_cb;

/* === GL hardware-render plumbing ============================
 *
 * Beetle-PSX previously used libretro-common's glsm shim for
 * registering its GL context with the frontend (via
 * RETRO_ENVIRONMENT_SET_HW_RENDER), looking up the
 * frontend-provided default framebuffer object, and resolving
 * GL function symbols.  glsm also exposed a ~150-function
 * gl* -> rgl* macro layer that maintained an extensive
 * gl_state mirror for save / restore between shader passes.
 *
 * Beetle never used the save / restore part: it never called
 * GLSM_CTL_STATE_BIND or GLSM_CTL_STATE_UNBIND, and never used
 * the glsm_state_setup VAO (it allocates its own VAOs per
 * gl_draw_buffer).  All the rgl* wrappers were therefore
 * passthrough-plus-bookkeeping-nobody-read, and all the gl_state
 * fields were write-only.
 *
 * The remaining glsm functionality - SET_HW_RENDER plumbing,
 * default-framebuffer lookup, and GL-symbol resolution - is a
 * few-dozen lines that lives directly here now, so the GL state
 * surface beetle actually exercises is visible at this file's
 * level. */
static struct retro_hw_render_callback hw_render;

static GLuint beetle_gl_get_current_framebuffer(void)
{
   /* libretro frontend hands us the default FBO id each frame.
    * Used by bind_libretro_framebuffer() when finalising a frame. */
   if (hw_render.get_current_framebuffer)
      return (GLuint)hw_render.get_current_framebuffer();
   return 0;
}

/* How many vertices we buffer before forcing a draw. Since the
 * indexes are stored on 16bits we need to make sure that the length
 * multiplied by 3 (for triple buffering) doesn't overflow 0xffff. */
#define VERTEX_BUFFER_LEN 0x4000

/* Maximum number of indices for a vertex buffer. Since quads have
 * two duplicated vertices it can be up to 3/2 the vertex buffer
 * length */
#define INDEX_BUFFER_LEN  ((VERTEX_BUFFER_LEN * 3 + 1) / 2)

/* Maximum uniform name length (matches the buffer size used in
 * load_program_uniforms when querying glGetActiveUniform). */
#define UNIFORM_NAME_MAX 64

/* Maximum uniforms per program.  Beetle's shaders have at most
 * ~7 active uniforms; bumping to 16 leaves headroom without
 * dynamic allocation. */
#define UNIFORM_MAX_ENTRIES 16

struct gl_uniform_entry
{
   char name[UNIFORM_NAME_MAX];
   GLint location;
};
typedef struct gl_uniform_entry gl_uniform_entry;


struct gl_uniform_map
{
   struct gl_uniform_entry items[UNIFORM_MAX_ENTRIES];
   size_t count;
};

/* Linear-scan replacement for std::map<std::string,GLint>::operator[].
 * Beetle's per-program uniform count is small (3-7) so a straight
 * memcmp loop is fine and avoids the std::map allocation overhead. */
static GLint gl_uniform_map_get(const struct gl_uniform_map *m, const char *name)
{
   size_t i;
   for (i = 0; i < m->count; i++)
   {
      if (strcmp(m->items[i].name, name) == 0)
         return m->items[i].location;
   }
   /* Sentinel matches glGetUniformLocation's "no such uniform"
    * convention - calls into glUniform* with location -1 are
    * silently ignored by GL. */
   return -1;
}

static bool gl_uniform_map_set(struct gl_uniform_map *m, const char *name, GLint location)
{
   size_t name_len;
   if (m->count >= UNIFORM_MAX_ENTRIES)
   {
      log_cb(RETRO_LOG_WARN,
            "[gl_uniform_map] capacity exceeded, dropping uniform \"%s\"\n",
            name);
      return false;
   }
   name_len = strlen(name);
   if (name_len >= UNIFORM_NAME_MAX)
   {
      log_cb(RETRO_LOG_WARN,
            "[gl_uniform_map] name \"%s\" exceeds %d-byte limit, dropping\n",
            name, UNIFORM_NAME_MAX);
      return false;
   }
   memcpy(m->items[m->count].name, name, name_len + 1);
   m->items[m->count].location = location;
   m->count++;
   return true;
}

typedef struct gl_uniform_map gl_uniform_map;

enum gl_video_clock {
   VIDEO_CLOCK_NTSC,
   VIDEO_CLOCK_PAL
};
typedef enum gl_video_clock gl_video_clock;


enum gl_filter_mode {
   FILTER_MODE_NEAREST,
   FILTER_MODE_SABR,
   FILTER_MODE_XBR,
   FILTER_MODE_BILINEAR,
   FILTER_MODE_3POINT,
   FILTER_MODE_JINC2
};
typedef enum gl_filter_mode gl_filter_mode;


/* State machine dealing with OpenGL context
 * destruction/reconstruction */
enum gl_state
{
   /* OpenGL context is ready */
   GL_STATE_VALID,
   /* OpenGL context has been destroyed (or is not created yet) */
   GL_STATE_INVALID
};
typedef enum gl_state gl_state;


enum gl_semi_transparency_mode {
   /* Source / 2 + destination / 2 */
   SEMI_TRANSPARENCY_MODE_AVERAGE = 0,
   /* Source + destination */
   SEMI_TRANSPARENCY_MODE_ADD = 1,
   /* Destination - source */
   SEMI_TRANSPARENCY_MODE_SUBTRACT_SOURCE = 2,
   /* Destination + source / 4 */
   SEMI_TRANSPARENCY_MODE_ADD_QUARTER_SOURCE = 3,
};
typedef enum gl_semi_transparency_mode gl_semi_transparency_mode;


struct gl_program
{
   GLuint id;
   /* Hash map of all the active uniforms in this program */
   gl_uniform_map uniforms;
   char *info_log;
};
typedef struct gl_program gl_program;


struct gl_shader
{
   GLuint id;
   char *info_log;
};
typedef struct gl_shader gl_shader;


struct gl_attribute
{
   char name[32];
   size_t offset;
   /* gl_attribute type (BYTE, UNSIGNED_SHORT, FLOAT etc...) */
   GLenum type;
   GLint components;
};
typedef struct gl_attribute gl_attribute;


struct gl_command_vertex {
   /* Position in PlayStation VRAM coordinates */
   float position[4];
   /* RGB color, 8bits per component */
   uint8_t color[3];
   /* gl_texture coordinates within the page */
   uint16_t texture_coord[2];
   /* gl_texture page (base offset in VRAM used for texture lookup) */
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
   /* gl_texture limits of primtive */
   uint16_t texture_limits[4];
   /* gl_texture window mask/OR values */
   uint8_t texture_window[4];
};
typedef struct gl_command_vertex gl_command_vertex;


struct gl_output_vertex {
   /* Vertex position on the screen */
   float position[2];
   /* Corresponding coordinate in the framebuffer */
   uint16_t fb_coord[2];
};
typedef struct gl_output_vertex gl_output_vertex;


struct gl_image_load_vertex {
   /* Vertex position in VRAM */
   uint16_t position[2];
};
typedef struct gl_image_load_vertex gl_image_load_vertex;


/* Forward declarations for vertex-attribute accessor functions.
 * The arrays and bodies live further down (after push_primitive)
 * but gl_renderer_new calls them when constructing each
 * gl_draw_buffer, well before that point. */
static const struct gl_attribute *gl_command_vertex_attributes(size_t *count);
static const struct gl_attribute *gl_output_vertex_attributes(size_t *count);
static const struct gl_attribute *gl_image_load_vertex_attributes(size_t *count);

struct gl_display_rect
{
   /* Analogous to DisplayRect in the Vulkan
    * renderer,but specified in native unscaled
    * glViewport coordinates. */
   int32_t x;
   int32_t y;
   uint32_t width;
   uint32_t height;
};
typedef struct gl_display_rect gl_display_rect;


struct gl_draw_config
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
typedef struct gl_draw_config gl_draw_config;


struct gl_texture
{
   GLuint id;
   uint32_t width;
   uint32_t height;
};
typedef struct gl_texture gl_texture;


struct gl_framebuffer
{
   GLuint id;
   struct gl_texture _color_texture;
};
typedef struct gl_framebuffer gl_framebuffer;


struct gl_primitive_batch {
   gl_semi_transparency_mode transparency_mode;
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
typedef struct gl_primitive_batch gl_primitive_batch;


/* Replaces std::vector<gl_primitive_batch>.  Heap-backed dynamic array
 * with explicit init/free; capacity grows by doubling.  Used for
 * the per-frame batch list which can grow to thousands of entries
 * on heavy frames, so fixed-size storage isn't viable. */
struct gl_primitive_batch_vec {
   struct gl_primitive_batch *items;
   size_t count;
   size_t capacity;
};
typedef struct gl_primitive_batch_vec gl_primitive_batch_vec;


static void gl_primitive_batch_vec_init(struct gl_primitive_batch_vec *v)
{
   v->items    = NULL;
   v->count    = 0;
   v->capacity = 0;
}

static void gl_primitive_batch_vec_free(struct gl_primitive_batch_vec *v)
{
   if (!v)
      return;
   if (v->items)
   {
      free(v->items);
      v->items = NULL;
   }
   v->count    = 0;
   v->capacity = 0;
}

static void gl_primitive_batch_vec_clear(struct gl_primitive_batch_vec *v)
{
   /* Keep the allocated buffer; just reset count.  Preserves
    * std::vector::clear() semantics: subsequent push_back stays
    * in the same backing storage until a grow is needed. */
   v->count = 0;
}

static bool gl_primitive_batch_vec_push(struct gl_primitive_batch_vec *v,
      const struct gl_primitive_batch *b)
{
   if (v->count >= v->capacity)
   {
      size_t new_cap = v->capacity ? v->capacity * 2 : 64;
      struct gl_primitive_batch *new_items =
         (struct gl_primitive_batch *)realloc(v->items,
               new_cap * sizeof(struct gl_primitive_batch));
      if (!new_items)
         return false;
      v->items    = new_items;
      v->capacity = new_cap;
   }
   v->items[v->count++] = *b;
   return true;
}

/* gl_draw_buffer holds a typed-but-not-templated vertex buffer.  T was
 * the vertex type in the original C++ code; the template only
 * affected how 'map' was typed and how 'element_size' was computed
 * (sizeof(T)).  Both are stored explicitly now so a single
 * non-templated struct works for all three vertex types. */
struct gl_draw_buffer
{
   /* OpenGL name for this buffer */
   GLuint id;
   /* Vertex Array Object containing the bindings for this
    * buffer. I'm assuming that each VAO will only use a single
    * buffer for simplicity. */
   GLuint vao;
   /* gl_program used to draw this buffer */
   gl_program *program;
   /* Currently mapped buffer range (write-only).  void* because
    * the actual element type is one of gl_command_vertex /
    * gl_output_vertex / gl_image_load_vertex; element_size below tells
    * us the stride. */
   void *map;
   /* sizeof(vertex_type), stored at construction */
   size_t element_size;
   /* Number of elements mapped at once in 'map' */
   size_t capacity;
   /* Index one-past the last element stored in 'map', relative
    * to the first element in 'map' */
   size_t map_index;
   /* Absolute offset of the 1st mapped element in the current
    * buffer relative to the beginning of the GL storage. */
   size_t map_start;
};
typedef struct gl_draw_buffer gl_draw_buffer;


struct gl_renderer {
   /* Buffer used to handle PlayStation GPU draw commands */
   gl_draw_buffer *command_buffer;
   /* Buffer used to draw to the frontend's framebuffer */
   gl_draw_buffer *output_buffer;
   /* Buffer used to copy textures from 'fb_texture' to 'fb_out' */
   gl_draw_buffer *image_load_buffer;

   GLushort vertex_indices[INDEX_BUFFER_LEN];
   /* GPU buffer for vertex_indices (required for core profile) */
   GLuint index_buffer;
   /* Primitive type for the vertices in the command buffers
    * (TRIANGLES or LINES) */
   GLenum command_draw_mode;
   unsigned vertex_index_pos;
   gl_primitive_batch_vec batches;
   /* Whether we're currently pushing opaque primitives or not */
   bool opaque;
   /* Current semi-transparency mode */
   gl_semi_transparency_mode semi_transparency_mode;
   /* gl_texture used to store the VRAM for texture mapping */
   gl_draw_config config;
   /* gl_framebuffer used as a shader input for texturing draw commands */
   gl_texture fb_texture;
   /* gl_framebuffer used as an output when running draw commands */
   gl_texture fb_out;
   /* Depth buffer for fb_out */
   gl_texture fb_out_depth;
   /* Current resolution of the frontend's framebuffer */
   uint32_t frontend_resolution[2];
   /* Current internal resolution upscaling factor */
   uint32_t internal_upscaling;
   /* Current internal color depth */
   uint8_t internal_color_depth;
   /* Counter for preserving primitive draw order in the z-buffer
    * since we draw semi-transparent primitives out-of-order. */
   int16_t primitive_ordering;
   /* gl_texture window mask/OR values */
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
typedef struct gl_renderer gl_renderer;


struct retro_gl
{
   gl_renderer *state_data;
   gl_state state;
   gl_video_clock video_clock;
   bool inited;
};
typedef struct retro_gl retro_gl;


static gl_draw_config persistent_config = {
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

static retro_gl static_renderer;

static bool has_software_fb = false;

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
#ifndef __APPLE__
      case GL_STACK_UNDERFLOW:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_STACK_UNDERFLOW [%s]\n", msg);
         break;
      case GL_STACK_OVERFLOW:
         log_cb(RETRO_LOG_ERROR, "GL error flag: GL_STACK_OVERFLOW [%s]\n", msg);
         break;
#endif
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

static bool gl_shader_init(
      struct gl_shader *shader,
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
      log_cb(RETRO_LOG_ERROR, "gl_shader_init() - gl_shader compilation failed:\n%s\n", source);


      log_cb(RETRO_LOG_DEBUG, "gl_shader info log:\n%s\n", shader->info_log);

      return false;
   }

   shader->id = id;

   return true;
}

static void get_program_info_log(gl_program *pg, GLuint id)
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

static gl_uniform_map load_program_uniforms(GLuint program)
{
   size_t u;
   gl_uniform_map uniforms;
   /* Figure out how long a uniform name can be */
   GLint max_name_len = 0;
   GLint n_uniforms   = 0;

   memset(&uniforms, 0, sizeof(uniforms));

   glGetProgramiv( program,
         GL_ACTIVE_UNIFORMS,
         &n_uniforms );

   glGetProgramiv( program,
         GL_ACTIVE_UNIFORM_MAX_LENGTH,
         &max_name_len);

   for (u = 0; u < (size_t)n_uniforms; ++u)
   {
      char name[256];
      size_t name_len = max_name_len;
      GLsizei len     = 0;
      GLint size      = 0;
      GLenum ty       = 0;
      GLint location;

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
      location = glGetUniformLocation(program, (const char*) name);

      if (location < 0)
      {
         log_cb(RETRO_LOG_WARN, "Uniform \"%s\" doesn't have a location", name);
         continue;
      }

      gl_uniform_map_set(&uniforms, name, location);
   }

   return uniforms;
}


static bool gl_program_init(
      gl_program *program,
      gl_shader* vertex_shader,
      gl_shader* fragment_shader)
{
   GLint status;
   GLuint id;
   gl_uniform_map uniforms;

   program->info_log = NULL;

   id                = glCreateProgram();

   if (id == 0)
   {
      log_cb(RETRO_LOG_ERROR, "gl_program_init() - glCreateProgram() returned 0\n");
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
      log_cb(RETRO_LOG_ERROR, "gl_program_init() - glLinkProgram() returned GL_FALSE\n");
      log_cb(RETRO_LOG_ERROR, "gl_program info log:\n%s\n", program->info_log);

      return false;
   }

   uniforms = load_program_uniforms(id);

   program->id       = id;
   program->uniforms = uniforms;

   log_cb(RETRO_LOG_DEBUG, "Binding program for first time: %d\n", id);

   glUseProgram(id);

   log_cb(RETRO_LOG_DEBUG, "Unbinding program for first time: %d\n", id);

   glUseProgram(0);

   return true;
}

static void gl_program_free(gl_program *program)
{
   if (!program)
      return;

   if (glIsProgram(program->id))
      glDeleteProgram(program->id);
   if (program->info_log)
      free(program->info_log);
}

/* Forward declaration: gl_draw_buffer_draw and gl_draw_buffer_new both
 * call gl_draw_buffer_map_no_bind, but it appears below them. */
static void gl_draw_buffer_map_no_bind(gl_draw_buffer *drawbuffer);

static void gl_draw_buffer_enable_attribute(gl_draw_buffer *drawbuffer, const char *attr)
{
   GLint index = glGetAttribLocation(drawbuffer->program->id, attr);

   if (index < 0)
      return;

   glBindVertexArray(drawbuffer->vao);
#ifdef EMSCRIPTEN
   glVertexAttribI4ui(index, 1, 1, 1, 1);
#else
   glEnableVertexAttribArray(index);
#endif
}

static void gl_draw_buffer_disable_attribute(gl_draw_buffer *drawbuffer, const char *attr)
{
   GLint index = glGetAttribLocation(drawbuffer->program->id, attr);

   if (index < 0)
      return;

   glBindVertexArray(drawbuffer->vao);
#ifdef EMSCRIPTEN
   glVertexAttribI4ui(index, 0, 0, 0, 0);
#else
   glDisableVertexAttribArray(index);
#endif
}

/* gl_draw_buffer_push_slice copies n elements of size 'len' bytes
 * each from 'slice' into the mapped buffer.  The map is typed
 * void* (was T* in the templated version) so we cast to char*
 * for byte arithmetic. */
#ifdef DEBUG
#define gl_draw_buffer_push_slice(drawbuffer, slice, n, len) \
   assert((n) <= gl_draw_buffer_remaining_capacity(drawbuffer)); \
   assert((drawbuffer)->map != NULL); \
   memcpy((char *)(drawbuffer)->map + (drawbuffer)->map_index * (len), (slice), (n) * (len)); \
   (drawbuffer)->map_index += (n);
#else
#define gl_draw_buffer_push_slice(drawbuffer, slice, n, len) \
   memcpy((char *)(drawbuffer)->map + (drawbuffer)->map_index * (len), (slice), (n) * (len)); \
   (drawbuffer)->map_index += (n);
#endif

static void gl_draw_buffer_draw(gl_draw_buffer *drawbuffer, GLenum mode)
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

   gl_draw_buffer_map_no_bind(drawbuffer);
}

/* Map the buffer for write-only access */
static void gl_draw_buffer_map_no_bind(gl_draw_buffer *drawbuffer)
{
   GLintptr offset_bytes;
   void *m                = NULL;
   size_t element_size    = drawbuffer->element_size;
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

   drawbuffer->map = m;
}

static void gl_draw_buffer_free(gl_draw_buffer *drawbuffer)
{
   if (!drawbuffer)
      return;

   if (drawbuffer->id != 0)
   {
      /* Unmap if currently mapped */
      glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);
      if (drawbuffer->map)
         glUnmapBuffer(GL_ARRAY_BUFFER);
   }

   if (drawbuffer->program)
   {
      gl_program_free(drawbuffer->program);
      free(drawbuffer->program);
      drawbuffer->program = NULL;
   }

   if (drawbuffer->id != 0)
   {
      glDeleteBuffers(1, &drawbuffer->id);
      drawbuffer->id = 0;
   }

   if (drawbuffer->vao != 0)
   {
      glDeleteVertexArrays(1, &drawbuffer->vao);
      drawbuffer->vao = 0;
   }

   drawbuffer->map       = NULL;
   drawbuffer->capacity  = 0;
   drawbuffer->map_index = 0;
   drawbuffer->map_start = 0;
}

static void gl_draw_buffer_bind_attributes(gl_draw_buffer *drawbuffer,
      const gl_attribute *attrs, size_t n_attrs)
{
   unsigned i;
   GLint nVertexAttribs;
   GLint element_size;
   size_t a;

   glBindVertexArray(drawbuffer->vao);

   /* ARRAY_BUFFER is captured by VertexAttribPointer */
   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);

   element_size = (GLint) drawbuffer->element_size;

   /* speculative: attribs enabled on VAO=0 (disabled) get applied
    * to the VAO when created initially.  As a core, we don't
    * control the state entirely at this point: frontend may have
    * enabled attribs.  We need to make sure they're all disabled
    * before then re-enabling the attribs we want (solves crashes
    * on some drivers/compilers due to accidentally enabled
    * attribs). */
   glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nVertexAttribs);

   for (i = 0; i < (unsigned)nVertexAttribs; i++)
      glDisableVertexAttribArray(i);

   for (a = 0; a < n_attrs; a++)
   {
      const gl_attribute *attr = &attrs[a];
      GLint index = glGetAttribLocation(drawbuffer->program->id, attr->name);

      /* Don't error out if the shader doesn't use this attribute,
       * it could be caused by shader optimization if the attribute
       * is unused for some reason. */
      if (index < 0)
         continue;

      glEnableVertexAttribArray((GLuint) index);

      /* This captures the buffer so that we don't have to bind it
       * when we draw later on, we'll just have to bind the vao */
      switch (attr->type)
      {
         case GL_BYTE:
         case GL_UNSIGNED_BYTE:
         case GL_SHORT:
         case GL_UNSIGNED_SHORT:
         case GL_INT:
         case GL_UNSIGNED_INT:
            glVertexAttribIPointer( index,
                  attr->components,
                  attr->type,
                  element_size,
                  (GLvoid*)attr->offset);
            break;
         case GL_FLOAT:
            glVertexAttribPointer(  index,
                  attr->components,
                  attr->type,
                  GL_FALSE,
                  element_size,
                  (GLvoid*)attr->offset);
            break;
#ifndef HAVE_OPENGLES3
         case GL_DOUBLE:
            glVertexAttribLPointer( index,
                  attr->components,
                  attr->type,
                  element_size,
                  (GLvoid*)attr->offset);
            break;
#endif
      }
   }
}

/* Initialise a gl_draw_buffer in-place.  Returns true on success.  On
 * failure, releases all resources acquired up to the failure point
 * (no shader leak, no gl_program leak, no GL object leak) and leaves
 * 'drawbuffer' zeroed.  This is stricter than the original C++
 * code, which silently left a half-initialised gl_draw_buffer behind
 * if gl_program_init failed and leaked the two compiled shader
 * objects + their info logs. */
static bool gl_draw_buffer_new(gl_draw_buffer *drawbuffer,
      const char *vertex_shader_src,
      const char *fragment_shader_src,
      size_t capacity,
      size_t element_size,
      const gl_attribute *attrs,
      size_t n_attrs)
{
   gl_shader vs, fs;
   gl_program *program;
   GLsizeiptr storage_size;
   bool vs_ok = false;
   bool fs_ok = false;
   bool program_ok = false;

   memset(drawbuffer, 0, sizeof(*drawbuffer));
   memset(&vs, 0, sizeof(vs));
   memset(&fs, 0, sizeof(fs));

   program = (gl_program *)calloc(1, sizeof(*program));
   if (!program)
   {
      log_cb(RETRO_LOG_ERROR,
            "[gl_draw_buffer_new] OOM allocating gl_program\n");
      return false;
   }

   if (!gl_shader_init(&vs, vertex_shader_src, GL_VERTEX_SHADER))
      goto fail;
   vs_ok = true;
   if (!gl_shader_init(&fs, fragment_shader_src, GL_FRAGMENT_SHADER))
      goto fail;
   fs_ok = true;
   if (!gl_program_init(program, &vs, &fs))
      goto fail;
   program_ok = true;

   /* gl_program now owns the linked binary; the shader objects can
    * be detached and the info-log strings released. */
   glDeleteShader(vs.id);
   glDeleteShader(fs.id);
   if (vs.info_log) { free(vs.info_log); vs.info_log = NULL; }
   if (fs.info_log) { free(fs.info_log); fs.info_log = NULL; }
   vs_ok = false;
   fs_ok = false;

   /* From here on every failure must drop the linked program too. */
   glGenVertexArrays(1, &drawbuffer->vao);
   if (drawbuffer->vao == 0)
      goto fail;

   glGenBuffers(1, &drawbuffer->id);
   if (drawbuffer->id == 0)
      goto fail;

   drawbuffer->program      = program;
   drawbuffer->capacity     = capacity;
   drawbuffer->element_size = element_size;
   drawbuffer->map_index    = 0;
   drawbuffer->map_start    = 0;
   drawbuffer->map          = NULL;

   /* Create and size the GL buffer.  We allocate enough space for
    * 3 times the requested capacity and only remap one third of
    * it at a time. */
   glBindBuffer(GL_ARRAY_BUFFER, drawbuffer->id);
   storage_size = drawbuffer->capacity * element_size * 3;

   /* Since we store indexes in unsigned shorts we want to make
    * sure the entire buffer is indexable. */
   assert(drawbuffer->capacity * 3 <= 0xffff);

   glBufferData(GL_ARRAY_BUFFER, storage_size, NULL, GL_DYNAMIC_DRAW);

   gl_draw_buffer_bind_attributes(drawbuffer, attrs, n_attrs);
   gl_draw_buffer_map_no_bind(drawbuffer);

   return true;

fail:
   /* Unwind in reverse order of acquisition. */
   if (drawbuffer->id != 0)
   {
      glDeleteBuffers(1, &drawbuffer->id);
      drawbuffer->id = 0;
   }
   if (drawbuffer->vao != 0)
   {
      glDeleteVertexArrays(1, &drawbuffer->vao);
      drawbuffer->vao = 0;
   }
   if (program_ok)
   {
      gl_program_free(program);
   }
   if (fs_ok)
   {
      glDeleteShader(fs.id);
   }
   if (vs_ok)
   {
      glDeleteShader(vs.id);
   }
   if (fs.info_log) free(fs.info_log);
   if (vs.info_log) free(vs.info_log);
   free(program);
   memset(drawbuffer, 0, sizeof(*drawbuffer));
   return false;
}

static void gl_framebuffer_init(struct gl_framebuffer *fb,
      struct gl_texture* color_texture)
{
   GLuint id = 0;
   GLenum col_attach_0 = GL_COLOR_ATTACHMENT0;
   glGenFramebuffers(1, &id);

   fb->id                    = id;

   fb->_color_texture.id     = color_texture->id;
   fb->_color_texture.width  = color_texture->width;
   fb->_color_texture.height = color_texture->height;

   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->id);

#ifdef HAVE_OPENGLES3
   glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           color_texture->id,
                           0);
#else
   glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           color_texture->id,
                           0);
#endif

   glDrawBuffers(1, &col_attach_0);
   glViewport( 0,
               0,
               (GLsizei) color_texture->width,
               (GLsizei) color_texture->height);
}

static void gl_texture_init(
      struct gl_texture *tex,
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
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   tex->id     = id;
   tex->width  = width;
   tex->height = height;
}

static void gl_texture_set_sub_image_window(
      struct gl_texture *tex,
      uint16_t top_left[2],
      uint16_t resolution[2],
      size_t row_len,
      GLenum format,
      GLenum ty,
      uint16_t* data)
{
   uint16_t x         = top_left[0];
   uint16_t y         = top_left[1];

   /* `data` is the caller's full 1024x512 VRAM buffer.  `index`
    * picks the first pixel of the upload region.  With x < 1024
    * and y < 512 (both clamped at the GP0 FBWrite parser via
    * x &= 0x3FF, y &= 0x1FF, see gpu.cpp), index is at most
    * 511*1024 + 1023 == 524287, the last element of vram.
    *
    * glTexSubImage2D below then reads (resolution[1]-1) * row_len
    * + resolution[0] words past sub_data using GL_UNPACK_ROW_LENGTH.
    * If x + resolution[0] > 1024 or y + resolution[1] > 512 the
    * upload reads past the end of vram - i.e. an FBWrite whose
    * target rectangle wraps across the VRAM seam.  The PS1 GPU
    * wraps such writes (the SW renderer's texel_put does
    * curx & 1023, cury & 511); this GL fast-path does not, and
    * the corresponding upload is incorrect for wrap-across writes.
    *
    * This is a pre-existing limitation, not introduced by this
    * function.  Fixing it would require splitting the upload
    * into 1-4 glTexSubImage2D calls along the seam(s), or
    * pre-rotating the source data into a scratch buffer. */
   size_t index       = ((size_t) y) * row_len + ((size_t) x);
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

/* Allocate, initialise, and return a heap gl_draw_buffer.  Returns
 * NULL on any failure (allocation, shader compile, program link,
 * GL object creation).  The caller is responsible for calling
 * gl_draw_buffer_free + free() to release a non-NULL return. */
static gl_draw_buffer *gl_draw_buffer_build(const char *vertex_shader_src,
      const char *fragment_shader_src,
      size_t capacity,
      size_t element_size,
      const gl_attribute *attrs,
      size_t n_attrs)
{
   gl_draw_buffer *db = (gl_draw_buffer *)calloc(1, sizeof(*db));
   if (!db)
      return NULL;
   if (!gl_draw_buffer_new(db, vertex_shader_src, fragment_shader_src,
            capacity, element_size, attrs, n_attrs))
   {
      free(db);
      return NULL;
   }
   return db;
}

static void gl_renderer_draw(gl_renderer *renderer)
{
   gl_framebuffer _fb;
   int16_t x;
   int16_t y;
   size_t bi;

   if (!renderer || static_renderer.state == GL_STATE_INVALID)
      return;

   x = renderer->config.draw_offset[0];
   y = renderer->config.draw_offset[1];

   if (renderer->command_buffer->program)
   {
      glUseProgram(renderer->command_buffer->program->id);
      glUniform2i(gl_uniform_map_get(&renderer->command_buffer->program->uniforms, "offset"), (GLint)x, (GLint)y);
      /* We use texture unit 0 */
      glUniform1i(gl_uniform_map_get(&renderer->command_buffer->program->uniforms, "fb_texture"), 0);
   }

   /* Bind the out framebuffer */
   gl_framebuffer_init(&_fb, &renderer->fb_out);

#ifdef HAVE_OPENGLES3
   glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER,
         GL_DEPTH_STENCIL_ATTACHMENT,
         GL_TEXTURE_2D,
         renderer->fb_out_depth.id,
         0);
#else
   glFramebufferTexture(   GL_DRAW_FRAMEBUFFER,
         GL_DEPTH_STENCIL_ATTACHMENT,
         renderer->fb_out_depth.id,
         0);
#endif

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

   if (renderer->batches.count > 0)
   {
      struct gl_primitive_batch *last = &renderer->batches.items[renderer->batches.count - 1];
      last->count = renderer->vertex_index_pos - last->first;
   }

   /* Upload index data to EBO (required for core profile - client-side
    * index pointers are not allowed) */
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->index_buffer);
   glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                   renderer->vertex_index_pos * sizeof(GLushort),
                   renderer->vertex_indices);

   {
      size_t bi;
      for (bi = 0; bi < renderer->batches.count; bi++)
      {
         struct gl_primitive_batch *it = &renderer->batches.items[bi];
         bool opaque;
         GLenum blend_func = GL_FUNC_ADD;
         GLenum blend_src = GL_CONSTANT_ALPHA;
         GLenum blend_dst = GL_CONSTANT_ALPHA;

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
      opaque = it->opaque;
      if (renderer->command_buffer->program)
         glUniform1ui(gl_uniform_map_get(&renderer->command_buffer->program->uniforms, "draw_semi_transparent"), !opaque);
      if (opaque)
         glDisable(GL_BLEND);
      else
      {
         glEnable(GL_BLEND);

         switch (it->transparency_mode)
         {
            /* 0.5xB + 0.5 x F */
            case SEMI_TRANSPARENCY_MODE_AVERAGE:
               blend_func = GL_FUNC_ADD;
               /* Set to 0.5 with glBlendColor */
               blend_src = GL_CONSTANT_ALPHA;
               blend_dst = GL_CONSTANT_ALPHA;
               break;
            /* 1.0xB + 1.0 x F */
            case SEMI_TRANSPARENCY_MODE_ADD:
               blend_func = GL_FUNC_ADD;
               blend_src = GL_ONE;
               blend_dst = GL_ONE;
               break;
            /* 1.0xB - 1.0 x F */
            case SEMI_TRANSPARENCY_MODE_SUBTRACT_SOURCE:
               blend_func = GL_FUNC_REVERSE_SUBTRACT;
               blend_src = GL_ONE;
               blend_dst = GL_ONE;
               break;
            case SEMI_TRANSPARENCY_MODE_ADD_QUARTER_SOURCE:
               blend_func = GL_FUNC_ADD;
               blend_src = GL_CONSTANT_COLOR;
               blend_dst = GL_ONE;
               break;
         }

         glBlendFuncSeparate(blend_src, blend_dst, GL_ONE, GL_ZERO);
         glBlendEquationSeparate(blend_func, GL_FUNC_ADD);
      }

      /* Drawing */
      if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      {
         /* This method doesn't call prepare_draw/finalize_draw itself, it
          * must be handled by the caller. This is because this command
          * can be called several times on the same buffer (i.e. multiple
          * draw calls between the prepare/finalize) */
         glDrawElements(it->draw_mode, it->count, GL_UNSIGNED_SHORT,
                        (GLvoid*)(it->first * sizeof(GLushort)));
      }
      }
   }

   glDisable(GL_STENCIL_TEST);

   renderer->command_buffer->map_start += renderer->command_buffer->map_index;
   renderer->command_buffer->map_index  = 0;
   gl_draw_buffer_map_no_bind(renderer->command_buffer);

   renderer->primitive_ordering = 0;
   gl_primitive_batch_vec_clear(&renderer->batches);
   renderer->opaque = false;
   renderer->vertex_index_pos = 0;
   renderer->mask_test = false;
   renderer->set_mask = false;

   glDeleteFramebuffers(1, &_fb.id);
}

static void gl_renderer_upload_textures(
      gl_renderer *renderer,
      uint16_t top_left[2],
      uint16_t dimensions[2],
      uint16_t pixel_buffer[VRAM_PIXELS])
{
   uint16_t x_start;
   uint16_t x_end;
   uint16_t y_start;
   uint16_t y_end;
   gl_image_load_vertex slice[4];
   gl_framebuffer _fb;

   if (!renderer)
      return;

   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glBindTexture(GL_TEXTURE_2D, renderer->fb_texture.id);
   glTexSubImage2D(  GL_TEXTURE_2D,
         0,
         (GLint) top_left[0],
         (GLint) top_left[1],
         (GLsizei) dimensions[0],
         (GLsizei) dimensions[1],
         GL_RGBA,
#ifdef HAVE_OPENGLES3
         GL_UNSIGNED_SHORT_5_5_5_1,
         /* bits are always in the order that they show
          * REV indicates the channels are in reversed order
          * RGBA
          * 16 bit unsigned short: R5 G5 B5 A1
          * RRRRRGGGGGBBBBBA */
#else
         GL_UNSIGNED_SHORT_1_5_5_5_REV,
         /* ABGR
          * 16 bit unsigned short: A1 B5 G5 R5
          * ABBBBBGGGGGRRRRR */
#endif
         (void*)pixel_buffer);

   x_start    = top_left[0];
   x_end      = x_start + dimensions[0];
   y_start    = top_left[1];
   y_end      = y_start + dimensions[1];

   {
      gl_image_load_vertex init[4] =
      {
         {   {x_start,   y_start }   },
         {   {x_end,     y_start }   },
         {   {x_start,   y_end   }   },
         {   {x_end,     y_end   }   }
      };
      memcpy(slice, init, sizeof(slice));
   }

   if (renderer->image_load_buffer)
   {
      gl_draw_buffer_push_slice(renderer->image_load_buffer, &slice, 4,
            sizeof(gl_image_load_vertex));

      if (renderer->image_load_buffer->program)
      {
         /* fb_texture is always at 1x */
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1i(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "fb_texture"), 0);
         glUniform1ui(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "internal_upscaling"), 1);

         glUseProgram(renderer->command_buffer->program->id);
         glUniform1i(gl_uniform_map_get(&renderer->command_buffer->program->uniforms, "fb_texture"), 0);
      }
   }

   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);

   /* Bind the output framebuffer */
   gl_framebuffer_init(&_fb, &renderer->fb_out);

   if (!gl_draw_buffer_is_empty(renderer->image_load_buffer))
      gl_draw_buffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

   glEnable(GL_SCISSOR_TEST);

#ifdef DEBUG
   get_error("gl_renderer_upload_textures");
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

static bool gl_renderer_new(gl_renderer *renderer, gl_draw_config config)
{
   gl_draw_buffer *command_buffer = NULL;
   uint8_t upscaling         = 1;
   bool display_vram         = false;
   struct retro_variable var = {0};
   uint16_t top_left[2]      = {0, 0};
   uint16_t dimensions[2]    = {
      (uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};
   int crop_overscan         = 1;
   int32_t image_offset_cycles = 0;
   unsigned image_crop       = 0;
   int32_t initial_scanline  = 0;
   int32_t last_scanline     = 239;
   int32_t initial_scanline_pal = 0;
   int32_t last_scanline_pal = 287;
   uint8_t filter            = FILTER_MODE_NEAREST;
   uint8_t depth             = 16;
   enum dither_mode dither_mode = DITHER_NATIVE;
   uint32_t native_width;
   uint32_t native_height;
   GLenum texture_storage    = GL_RGB5_A1;

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
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;
   }

   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }

   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline = atoi(var.value);
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

      renderer->filter_type = filter;
   }

   var.key = BEETLE_OPT(depth);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "32bpp"))
         depth = 32;
   }

   var.key = BEETLE_OPT(dither_mode);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "internal resolution"))
         dither_mode = DITHER_UPSCALED;
      else if (!strcmp(var.value, "disabled"))
         dither_mode  = DITHER_OFF;
   }

   log_cb(RETRO_LOG_DEBUG, "Building OpenGL state (%dx internal res., %dbpp)\n", upscaling, depth);

   {
      size_t cmd_n_attrs;
      size_t out_n_attrs;
      size_t img_n_attrs;
      const gl_attribute *cmd_attrs = gl_command_vertex_attributes(&cmd_n_attrs);
      const gl_attribute *out_attrs = gl_output_vertex_attributes(&out_n_attrs);
      const gl_attribute *img_attrs = gl_image_load_vertex_attributes(&img_n_attrs);
      gl_draw_buffer *output_buffer;
      gl_draw_buffer *image_load_buffer;

      switch(renderer->filter_type)
      {
         case FILTER_MODE_SABR:
            command_buffer = gl_draw_buffer_build(
                  command_vertex_xbr,
                  command_fragment_sabr,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
            break;
         case FILTER_MODE_XBR:
            command_buffer = gl_draw_buffer_build(
                  command_vertex_xbr,
                  command_fragment_xbr,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
            break;
         case FILTER_MODE_BILINEAR:
            command_buffer = gl_draw_buffer_build(
                  command_vertex,
                  command_fragment_bilinear,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
            break;
         case FILTER_MODE_3POINT:
            command_buffer = gl_draw_buffer_build(
                  command_vertex,
                  command_fragment_3point,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
            break;
         case FILTER_MODE_JINC2:
            command_buffer = gl_draw_buffer_build(
                  command_vertex,
                  command_fragment_jinc2,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
            break;
         case FILTER_MODE_NEAREST:
         default:
            command_buffer = gl_draw_buffer_build(
                  command_vertex,
                  command_fragment,
                  VERTEX_BUFFER_LEN,
                  sizeof(gl_command_vertex), cmd_attrs, cmd_n_attrs);
      }

      output_buffer =
         gl_draw_buffer_build(
               output_vertex,
               output_fragment,
               4,
               sizeof(gl_output_vertex), out_attrs, out_n_attrs);

      image_load_buffer =
         gl_draw_buffer_build(
               image_load_vertex,
               image_load_fragment,
               4,
               sizeof(gl_image_load_vertex), img_attrs, img_n_attrs);

      /* If any of the three failed, free the others to avoid
       * leaking GL/heap resources, and refuse the renderer. */
      if (!command_buffer || !output_buffer || !image_load_buffer)
      {
         log_cb(RETRO_LOG_ERROR,
               "[gl_renderer_new] gl_draw_buffer_build failed: cmd=%p out=%p img=%p\n",
               (void *)command_buffer, (void *)output_buffer,
               (void *)image_load_buffer);
         if (command_buffer)
         {
            gl_draw_buffer_free(command_buffer);
            free(command_buffer);
         }
         if (output_buffer)
         {
            gl_draw_buffer_free(output_buffer);
            free(output_buffer);
         }
         if (image_load_buffer)
         {
            gl_draw_buffer_free(image_load_buffer);
            free(image_load_buffer);
         }
         return false;
      }

      renderer->output_buffer     = output_buffer;
      renderer->image_load_buffer = image_load_buffer;
   }

   native_width  = (uint32_t) VRAM_WIDTH_PIXELS;
   native_height = (uint32_t) VRAM_HEIGHT;

   /* gl_texture holding the raw VRAM texture contents. We can't
    * meaningfully upscale it since most games use paletted
    * textures. */
   gl_texture_init(&renderer->fb_texture, native_width, native_height, GL_RGB5_A1);

   if (dither_mode == DITHER_OFF)
   {
      /* Dithering is superfluous when we increase the internal
      * color depth, but users asked for it */
      gl_draw_buffer_disable_attribute(command_buffer, "dither");
   }
   else
   {
      gl_draw_buffer_enable_attribute(command_buffer, "dither");
   }

   if (command_buffer->program)
   {
      uint32_t dither_scaling = dither_mode == DITHER_UPSCALED ? 1 : upscaling;

      glUseProgram(command_buffer->program->id);
      glUniform1ui(gl_uniform_map_get(&command_buffer->program->uniforms, "dither_scaling"), dither_scaling);
   }

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

   gl_texture_init(
         &renderer->fb_out,
         native_width  * upscaling,
         native_height * upscaling,
         texture_storage);

   gl_texture_init(
         &renderer->fb_out_depth,
         renderer->fb_out.width,
         renderer->fb_out.height,
         GL_DEPTH24_STENCIL8);

   renderer->filter_type = filter;
   renderer->command_buffer = command_buffer;
   renderer->vertex_index_pos = 0;

   /* Create index buffer object for core profile compatibility */
   glGenBuffers(1, &renderer->index_buffer);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->index_buffer);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                INDEX_BUFFER_LEN * sizeof(GLushort),
                NULL, GL_DYNAMIC_DRAW);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
   renderer->command_draw_mode = GL_TRIANGLES;
   renderer->semi_transparency_mode =  SEMI_TRANSPARENCY_MODE_AVERAGE;
   /* output_buffer and image_load_buffer were assigned to
    * renderer above inside the gl_draw_buffer_build block. */
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
      gl_renderer_upload_textures(renderer, top_left, dimensions, GPU_get_vram());

   return true;
}

static void gl_renderer_free(gl_renderer *renderer)
{
   if (!renderer)
      return;

   if (renderer->command_buffer)
   {
      gl_draw_buffer_free(renderer->command_buffer);
      free(renderer->command_buffer);
   }
   renderer->command_buffer = NULL;

   if (renderer->output_buffer)
   {
      gl_draw_buffer_free(renderer->output_buffer);
      free(renderer->output_buffer);
   }
   renderer->output_buffer = NULL;

   if (renderer->image_load_buffer)
   {
      gl_draw_buffer_free(renderer->image_load_buffer);
      free(renderer->image_load_buffer);
   }
   renderer->image_load_buffer = NULL;

   /* Release the dynamic batch list backing storage.  C++ used
    * to do this implicitly via std::vector's destructor; in C we
    * must call gl_primitive_batch_vec_free explicitly or leak. */
   gl_primitive_batch_vec_free(&renderer->batches);

   if (renderer->index_buffer)
      glDeleteBuffers(1, &renderer->index_buffer);
   renderer->index_buffer = 0;

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

   {
      unsigned i;
      for (i = 0; i < INDEX_BUFFER_LEN; i++)
         renderer->vertex_indices[i] = 0;
   }
}

static INLINE void apply_scissor(gl_renderer *renderer)
{
   uint16_t _x = renderer->config.draw_area_top_left[0];
   uint16_t _y = renderer->config.draw_area_top_left[1];
   int _w      = renderer->config.draw_area_bot_right[0] - _x;
   int _h      = renderer->config.draw_area_bot_right[1] - _y;
   GLsizei upscale;
   GLsizei x;
   GLsizei y;
   GLsizei w;
   GLsizei h;

   if (_w < 0)
      _w = 0;

   if (_h < 0)
      _h = 0;

   upscale = (GLsizei)renderer->internal_upscaling;

   /* We need to scale those to match the internal resolution if
    * upscaling is enabled */
   x = (GLsizei) _x * upscale;
   y = (GLsizei) _y * upscale;
   w = (GLsizei) _w * upscale;
   h = (GLsizei) _h * upscale;

   glScissor(x, y, w, h);
}

static gl_display_rect compute_gl_display_rect(gl_renderer *renderer)
{
   /* Current function logic mostly backported from Vulkan renderer */

   int32_t clock_div;
   uint32_t width;
   int32_t x;
   uint32_t height;
   int32_t y;

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

      default: /* should never be here -- if we're here, something is terribly wrong */
         break;
   }

   if (renderer->crop_overscan)
   {
      int32_t offset_cycles = renderer->image_offset_cycles;
      int32_t h_start = (int32_t) renderer->config.display_area_hrange[0];
      width = (uint32_t) ((2560/clock_div) - renderer->image_crop);
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
      int32_t offset_cycles = renderer->image_offset_cycles;
      int32_t h_start = (int32_t) renderer->config.display_area_hrange[0];
      width = (uint32_t) (2800/clock_div);
      x = floor((h_start - 488 + offset_cycles) / (double) clock_div);
   }

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

   {
      gl_display_rect r;
      r.x      = x;
      r.y      = y;
      r.width  = width;
      r.height = height;
      return r;
   }
}

static void bind_libretro_framebuffer(gl_renderer *renderer)
{
   GLuint fbo;
   uint32_t w, h;
   uint32_t upscale;
   uint32_t f_w;
   uint32_t f_h;
   uint32_t _w;
   uint32_t _h;
   uint32_t vp_w;
   uint32_t vp_h;
   int32_t x, y;
   int32_t _x = 0;
   int32_t _y = 0;

   if (!renderer)
      return;

   upscale   = renderer->internal_upscaling;
   f_w       = renderer->frontend_resolution[0];
   f_h       = renderer->frontend_resolution[1];
   _w        = renderer->config.display_resolution[0];
   _h        = renderer->config.display_resolution[1];

   /* vp_w and vp_h currently contingent on rsx_intf_set_display_mode behavior... */
   vp_w = renderer->config.display_resolution[0];
   vp_h = renderer->config.display_resolution[1];

   if (renderer->display_vram)
   {
      _x           = 0;
      _y           = 0;
      _w           = VRAM_WIDTH_PIXELS;
      _h           = VRAM_HEIGHT; /* override vram fb dimensions for viewport */
      vp_w = _w;
      vp_h = _h;
   }
   else
   {
      gl_display_rect disp_rect = compute_gl_display_rect(renderer);
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
   fbo = beetle_gl_get_current_framebuffer();
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
   glViewport((GLsizei) x, (GLsizei) y, (GLsizei) vp_w, (GLsizei) vp_h);
}

static bool retro_refresh_variables(gl_renderer *renderer)
{
   uint8_t filter            = FILTER_MODE_NEAREST;
   uint8_t upscaling         = 1;
   bool display_vram         = false;
   struct retro_variable var = {0};
   int crop_overscan         = 1;
   int32_t image_offset_cycles;
   unsigned image_crop;
   int32_t initial_scanline       = 0;
   int32_t last_scanline          = 239;
   int32_t initial_scanline_pal   = 0;
   int32_t last_scanline_pal      = 287;
   uint8_t depth                  = 16;
   enum dither_mode dither_mode   = DITHER_NATIVE;
   bool rebuild_fb_out;
   bool reconfigure_frontend;

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
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;
   }

   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }

   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      initial_scanline = atoi(var.value);
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      last_scanline = atoi(var.value);
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
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "32bpp"))
         depth = 32;
   }

   var.key = BEETLE_OPT(dither_mode);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "1x(native)"))
      {
         dither_mode = DITHER_NATIVE;
         gl_draw_buffer_enable_attribute(renderer->command_buffer, "dither");
      }
      else if (!strcmp(var.value, "internal resolution"))
      {
         dither_mode = DITHER_UPSCALED;
         gl_draw_buffer_enable_attribute(renderer->command_buffer, "dither");
      }
      else if (!strcmp(var.value, "disabled"))
      {
         dither_mode  = DITHER_OFF;
         gl_draw_buffer_disable_attribute(renderer->command_buffer, "dither");
      }
   }

   rebuild_fb_out =
      upscaling != renderer->internal_upscaling ||
      depth != renderer->internal_color_depth;

   if (rebuild_fb_out)
   {
      uint32_t native_width  = (uint32_t) VRAM_WIDTH_PIXELS;
      uint32_t native_height = (uint32_t) VRAM_HEIGHT;
      uint32_t w             = native_width  * upscaling;
      uint32_t h             = native_height * upscaling;
      GLenum texture_storage = GL_RGB5_A1;
      uint16_t top_left[2]   = {0, 0};
      uint16_t dimensions[2] = {(uint16_t) VRAM_WIDTH_PIXELS, (uint16_t) VRAM_HEIGHT};

      if (dither_mode == DITHER_OFF)
         gl_draw_buffer_disable_attribute(renderer->command_buffer, "dither");
      else
         gl_draw_buffer_enable_attribute(renderer->command_buffer, "dither");

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
      gl_texture_init(&renderer->fb_out, w, h, texture_storage);

      /* This is a bit wasteful since it'll re-upload the data
       * to 'fb_texture' even though we haven't touched it but
       * this code is not very performance-critical anyway. */

      if (renderer)
         gl_renderer_upload_textures(renderer, top_left, dimensions, GPU_get_vram());

      glDeleteTextures(1, &renderer->fb_out_depth.id);
      renderer->fb_out_depth.id     = 0;
      renderer->fb_out_depth.width  = 0;
      renderer->fb_out_depth.height = 0;
      gl_texture_init(&renderer->fb_out_depth, w, h, GL_DEPTH24_STENCIL8);
   }

   if (renderer->command_buffer->program)
   {
      uint32_t dither_scaling = dither_mode == DITHER_UPSCALED ? 1 : upscaling;

      glUseProgram(renderer->command_buffer->program->id);
      glUniform1ui(gl_uniform_map_get(&renderer->command_buffer->program->uniforms, "dither_scaling"), dither_scaling);
   }

   glLineWidth((GLfloat) upscaling);

   /* If the scaling factor has changed the frontend should be
   *  reconfigured. We can't do that here because it could
   *  destroy the OpenGL context which would destroy 'this' */
   reconfigure_frontend =
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
      gl_renderer *renderer,
      gl_command_vertex *v,
      unsigned count,
      GLenum mode,
      gl_semi_transparency_mode stm,
      bool mask_test,
      bool set_mask)
{
   bool is_semi_transparent;
   bool is_textured;
   bool is_opaque;
   bool buffer_full;

   if (!renderer)
      return;

   is_semi_transparent = v[0].semi_transparent == 1;
   is_textured         = v[0].texture_blend_mode != 0;
   /* Textured semi-transparent polys can contain opaque texels (when
    * bit 15 of the color is set to 0). Therefore they're drawn twice,
    * once for the opaque texels and once for the semi-transparent
    * ones. Only untextured semi-transparent triangles don't need to be
    * drawn as opaque. */
   is_opaque   = !is_semi_transparent || is_textured;
   buffer_full = gl_draw_buffer_remaining_capacity(renderer->command_buffer) < count;

   if (buffer_full)
   {
      if (!gl_draw_buffer_is_empty(renderer->command_buffer))
         gl_renderer_draw(renderer);
   }

   {
      int16_t z = renderer->primitive_ordering;
      unsigned i;
      renderer->primitive_ordering += 1;

      for (i = 0; i < count; i++)
      {
         v[i].position[2] = z;
         v[i].texture_window[0] = renderer->tex_x_mask;
         v[i].texture_window[1] = renderer->tex_x_or;
         v[i].texture_window[2] = renderer->tex_y_mask;
         v[i].texture_window[3] = renderer->tex_y_or;
      }
   }

   if (renderer->batches.count == 0
       || mode != renderer->command_draw_mode
       || is_opaque != renderer->opaque
       || (is_semi_transparent &&
           stm != renderer->semi_transparency_mode)
       || renderer->set_mask != set_mask
       || renderer->mask_test != mask_test)
   {
      struct gl_primitive_batch batch;

      if (renderer->batches.count > 0)
      {
         struct gl_primitive_batch *last = &renderer->batches.items[renderer->batches.count - 1];
         last->count = renderer->vertex_index_pos - last->first;
      }
      batch.opaque = is_opaque;
      batch.draw_mode = mode;
      batch.transparency_mode = stm;
      batch.set_mask = set_mask;
      batch.mask_test = mask_test;
      batch.first = renderer->vertex_index_pos;
      batch.count = 0;
      gl_primitive_batch_vec_push(&renderer->batches, &batch);

      renderer->semi_transparency_mode = stm;
      renderer->command_draw_mode = mode;
      renderer->opaque = is_opaque;
      renderer->set_mask = set_mask;
      renderer->mask_test = mask_test;
   }
}

static void vertex_add_blended_pass(
      gl_renderer *renderer, int vertex_index)
{
   if (renderer->batches.count > 0)
   {
      struct gl_primitive_batch *last = &renderer->batches.items[renderer->batches.count - 1];
      struct gl_primitive_batch batch;

      last->count = renderer->vertex_index_pos - last->first;

      batch.opaque = false;
      batch.draw_mode = last->draw_mode;
      batch.transparency_mode = last->transparency_mode;
      batch.set_mask = true;
      batch.mask_test = last->mask_test;
      batch.first = vertex_index;
      batch.count = 0;
      gl_primitive_batch_vec_push(&renderer->batches, &batch);

      renderer->opaque = false;
      renderer->set_mask = true;
   }
}

static void push_primitive(
      gl_renderer *renderer,
      gl_command_vertex *v,
      unsigned count,
      GLenum mode,
      gl_semi_transparency_mode stm,
      bool mask_test,
      bool set_mask)
{
   bool is_semi_transparent;
   bool is_textured;
   unsigned index;
   unsigned index_pos;
   unsigned i;

   if (!renderer)
      return;

   is_semi_transparent = v[0].semi_transparent   == 1;
   is_textured         = v[0].texture_blend_mode != 0;

   vertex_preprocessing(renderer, v, count, mode, stm, mask_test, set_mask);

   index     = gl_draw_buffer_next_index(renderer->command_buffer);
   index_pos = renderer->vertex_index_pos;

   for (i = 0; i < count; i++)
      renderer->vertex_indices[renderer->vertex_index_pos++] = index + i;

   /* Add transparent pass if needed */
   if (is_semi_transparent && is_textured)
      vertex_add_blended_pass(renderer, index_pos);

   gl_draw_buffer_push_slice(renderer->command_buffer, v, count,
         sizeof(gl_command_vertex)
         );
}

/* Vertex-attribute layouts.  The original code expressed these as
 * 'static std::vector<gl_attribute> T::attributes()' factory methods;
 * we drop them in favour of file-static const arrays plus small
 * accessor helpers since the layouts never change at runtime. */
static const struct gl_attribute gl_command_vertex_attribs[] = {
   { "position",           offsetof(gl_command_vertex, position),           GL_FLOAT,          4 },
   { "color",              offsetof(gl_command_vertex, color),              GL_UNSIGNED_BYTE,  3 },
   { "texture_coord",      offsetof(gl_command_vertex, texture_coord),      GL_UNSIGNED_SHORT, 2 },
   { "texture_page",       offsetof(gl_command_vertex, texture_page),       GL_UNSIGNED_SHORT, 2 },
   { "clut",               offsetof(gl_command_vertex, clut),               GL_UNSIGNED_SHORT, 2 },
   { "texture_blend_mode", offsetof(gl_command_vertex, texture_blend_mode), GL_UNSIGNED_BYTE,  1 },
   { "depth_shift",        offsetof(gl_command_vertex, depth_shift),        GL_UNSIGNED_BYTE,  1 },
   { "dither",             offsetof(gl_command_vertex, dither),             GL_UNSIGNED_BYTE,  1 },
   { "semi_transparent",   offsetof(gl_command_vertex, semi_transparent),   GL_UNSIGNED_BYTE,  1 },
   { "texture_window",     offsetof(gl_command_vertex, texture_window),     GL_UNSIGNED_BYTE,  4 },
   { "texture_limits",     offsetof(gl_command_vertex, texture_limits),     GL_UNSIGNED_SHORT, 4 }
};

static const struct gl_attribute gl_output_vertex_attribs[] = {
   { "position", offsetof(gl_output_vertex, position), GL_FLOAT,          2 },
   { "fb_coord", offsetof(gl_output_vertex, fb_coord), GL_UNSIGNED_SHORT, 2 }
};

static const struct gl_attribute gl_image_load_vertex_attribs[] = {
   { "position", offsetof(gl_image_load_vertex, position), GL_UNSIGNED_SHORT, 2 }
};

static const struct gl_attribute *gl_command_vertex_attributes(size_t *count)
{
   *count = sizeof(gl_command_vertex_attribs) / sizeof(gl_command_vertex_attribs[0]);
   return gl_command_vertex_attribs;
}

static const struct gl_attribute *gl_output_vertex_attributes(size_t *count)
{
   *count = sizeof(gl_output_vertex_attribs) / sizeof(gl_output_vertex_attribs[0]);
   return gl_output_vertex_attribs;
}

static const struct gl_attribute *gl_image_load_vertex_attributes(size_t *count)
{
   *count = sizeof(gl_image_load_vertex_attribs) / sizeof(gl_image_load_vertex_attribs[0]);
   return gl_image_load_vertex_attribs;
}

static void cleanup_gl_state(void)
{
   /* Cleanup OpenGL context before returning to the frontend.
    * Must reset ALL state that the core modifies, otherwise
    * the frontend's presentation pass may render incorrectly. */
   glDisable(GL_BLEND);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_STENCIL_TEST);
   glBlendColor(0.0, 0.0, 0.0, 0.0);
   glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
   glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xFF);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, 0);
   glUseProgram(0);
   glBindVertexArray(0);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   glLineWidth(1.0);
   glClearColor(0.0, 0.0, 0.0, 0.0);
}

/* Resolve a function via the libretro frontend's get_proc_address,
 * trying the core name first then a NULL-terminated list of common
 * extension suffixes (OES, EXT, ARB, NV, ...).  Returns the first
 * non-NULL pointer or NULL if nothing matched. */
static retro_proc_address_t gl_caps_resolve(
      retro_get_proc_address_t get_proc,
      const char *base_name,
      const char *const *suffixes)
{
   retro_proc_address_t ptr;
   char buf[128];
   const char *s;
   size_t base_len;
   size_t i;

   if (!get_proc || !base_name)
      return NULL;

   ptr = get_proc(base_name);
   if (ptr)
      return ptr;

   if (!suffixes)
      return NULL;

   base_len = strlen(base_name);
   if (base_len + 8 >= sizeof(buf))
      return NULL;

   for (i = 0; (s = suffixes[i]) != NULL; i++)
   {
      size_t s_len;
      s_len = strlen(s);
      if (base_len + s_len + 1 > sizeof(buf))
         continue;
      memcpy(buf, base_name, base_len);
      memcpy(buf + base_len, s, s_len);
      buf[base_len + s_len] = '\0';
      ptr = get_proc(buf);
      if (ptr)
         return ptr;
   }

   return NULL;
}

/* Parse "X.Y" out of a version string fragment.  Accepts a
 * leading "OpenGL ES " prefix.  Stores results into *major /
 * *minor; on parse failure leaves them untouched. */
static void gl_caps_parse_version(const char *version,
      int *major, int *minor)
{
   const char *p;
   int mj = 0;
   int mn = 0;

   if (!version)
      return;

   p = version;
   if (strncmp(p, "OpenGL ES", 9) == 0)
   {
      p += 9;
      while (*p == ' ')
         p++;
   }

   while (*p >= '0' && *p <= '9')
   {
      mj = mj * 10 + (*p - '0');
      p++;
   }
   if (*p != '.')
      return;
   p++;
   while (*p >= '0' && *p <= '9')
   {
      mn = mn * 10 + (*p - '0');
      p++;
   }

   *major = mj;
   *minor = mn;
}

/* Initialise gl_caps once a real GL context is current.
 * Must be called after rglgen_resolve_symbols(...) so the
 * baseline GL function-pointer table is populated. */
static void gl_caps_init(void)
{
   /* Suffix lists ordered most-likely-first.  ARB before EXT
    * because ARB tends to be the canonical desktop extension;
    * OES/EXT before NV because vendor-prefixed are last resort. */
   static const char *const copy_image_suffixes[] = {
      "ARB", "OES", "EXT", "NV", NULL
   };
   static const char *const blit_framebuffer_suffixes[] = {
      "EXT", "NV", "ANGLE", NULL
   };

   retro_get_proc_address_t get_proc = NULL;
   const GLubyte *gl_version_str;
   const GLubyte *gl_vendor_str;
   const GLubyte *gl_renderer_str;

   memset(&gl_caps, 0, sizeof(gl_caps));

   /* Strings: safe to query before anything else. */
   gl_version_str  = glGetString(GL_VERSION);
   gl_vendor_str   = glGetString(GL_VENDOR);
   gl_renderer_str = glGetString(GL_RENDERER);

   gl_caps.version_string = gl_version_str  ? (const char *)gl_version_str  : "(unknown)";
   gl_caps.vendor         = gl_vendor_str   ? (const char *)gl_vendor_str   : "(unknown)";
   gl_caps.renderer       = gl_renderer_str ? (const char *)gl_renderer_str : "(unknown)";

   /* Detect API family from the version string.  GL_VERSION on
    * any GLES context begins with "OpenGL ES "; desktop versions
    * begin with the version number directly. */
   if (gl_version_str
         && strncmp((const char *)gl_version_str, "OpenGL ES", 9) == 0)
      gl_caps.api = GL_API_GLES;
   else
      gl_caps.api = GL_API_DESKTOP;

   /* Parse "X.Y" out of GL_VERSION.  String parse rather than
    * glGetIntegerv(GL_MAJOR_VERSION) because the latter only
    * works on GL 3.0+ / GLES 3.0+ contexts. */
   gl_caps_parse_version(gl_caps.version_string,
         &gl_caps.version_major, &gl_caps.version_minor);
   gl_caps.version_packed =
        ((gl_caps.version_major & 0xFF) << 8)
      |  (gl_caps.version_minor & 0xFF);

   /* Profile detection.  GL_CONTEXT_PROFILE_MASK is a 3.2+ desktop
    * enum and is not defined in any GLES header; GLES has no
    * profile distinction so we don't need it there anyway. */
   if (gl_caps.api == GL_API_GLES)
      gl_caps.profile = GL_PROFILE_ES;
#if defined(GL_CONTEXT_PROFILE_MASK) && defined(GL_CONTEXT_CORE_PROFILE_BIT)
   else if (gl_caps.version_packed >= 0x0302)
   {
      GLint mask = 0;
      glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
      if (mask & GL_CONTEXT_CORE_PROFILE_BIT)
         gl_caps.profile = GL_PROFILE_CORE;
      else
         gl_caps.profile = GL_PROFILE_LEGACY;
   }
#endif
   else
      gl_caps.profile = GL_PROFILE_LEGACY;

   /* Pull the libretro frontend's symbol resolver.  Without it
    * we can't probe for runtime-loaded extension entry points;
    * leave function pointers NULL and let call sites fall back. */
   if (hw_render.get_proc_address)
      get_proc = hw_render.get_proc_address;

   if (get_proc)
   {
      gl_caps.fp_glBlitFramebuffer =
         (PFN_BEETLE_GL_BLITFRAMEBUFFER)gl_caps_resolve(
            get_proc, "glBlitFramebuffer", blit_framebuffer_suffixes);

      gl_caps.fp_glCopyImageSubData =
         (PFN_BEETLE_GL_COPYIMAGESUBDATA)gl_caps_resolve(
            get_proc, "glCopyImageSubData", copy_image_suffixes);
   }

   log_cb(RETRO_LOG_INFO,
         "[gl_caps] %s | %s | %s\n",
         gl_caps.vendor, gl_caps.renderer, gl_caps.version_string);
   log_cb(RETRO_LOG_INFO,
         "[gl_caps] api=%s profile=%s version=%d.%d (packed 0x%04x)\n",
         gl_caps.api == GL_API_GLES ? "GLES"
            : gl_caps.api == GL_API_DESKTOP ? "Desktop" : "?",
         gl_caps.profile == GL_PROFILE_CORE   ? "Core"
            : gl_caps.profile == GL_PROFILE_ES     ? "ES"
            : gl_caps.profile == GL_PROFILE_LEGACY ? "Legacy/Compat" : "?",
         gl_caps.version_major, gl_caps.version_minor,
         gl_caps.version_packed);
   log_cb(RETRO_LOG_INFO,
         "[gl_caps] glBlitFramebuffer:  %s\n",
         gl_caps.fp_glBlitFramebuffer  ? "available" : "NOT available");
   log_cb(RETRO_LOG_INFO,
         "[gl_caps] glCopyImageSubData: %s\n",
         gl_caps.fp_glCopyImageSubData ? "available" : "NOT available");

   /* Floor check.  Beetle's GL renderer needs VAOs (3.0+ core),
    * separate read/draw FBO bindings (3.0+), glMapBufferRange
    * (3.0+), glDrawBuffers in core (3.0+), unsigned-integer
    * uniforms (3.0+), and several other 3.0+ features.  Below
    * GL 3.0 / GLES 3.0 the renderer cannot function, so we mark
    * the context unsupported and gl_context_reset will skip
    * activating the renderer.  Logged as ERROR so the user sees
    * exactly why nothing's drawing.
    *
    * Exception: a parsed version of 0.0 means the version string
    * was unrecognised - rather than refuse outright we let it
    * through and let the call sites fail naturally, since this
    * usually indicates a context-detection oddity rather than a
    * genuinely-too-old driver. */
   if (gl_caps.version_packed != 0 && gl_caps.version_packed < 0x0300)
   {
      gl_caps.unsupported = 1;
      log_cb(RETRO_LOG_ERROR,
            "[gl_caps] GL/GLES version %d.%d is below the supported "
            "floor of 3.0.  This renderer requires GL 3.0+ or GLES "
            "3.0+ features (VAOs, separate read/draw FBOs, "
            "glMapBufferRange, unsigned uniforms, ...).  The "
            "renderer will not be activated; expect a black "
            "screen or no output.\n",
            gl_caps.version_major, gl_caps.version_minor);
   }
}

static void gl_context_reset(void)
{
   log_cb(RETRO_LOG_DEBUG, "gl_context_reset called.\n");

   /* Resolve the GL function-pointer table for this context.
    * rglgen_resolve_symbols walks the rglgen-generated symbol
    * declarations and populates each __rglgen_glFoo via the
    * frontend's get_proc_address callback.  Must run before any
    * gl call that goes through one of those wrappers. */
   if (hw_render.get_proc_address)
      rglgen_resolve_symbols(hw_render.get_proc_address);

   /* Detect what the running driver actually supports.  Must run
    * after the symbol table is populated and before any
    * feature-gated code path is taken. */
   gl_caps_init();

   /* If the version is below our floor, leave the renderer in
    * GL_STATE_INVALID so all subsequent rsx_gl_* entry points
    * short-circuit.  The user sees nothing render but gets an
    * unambiguous error in the log explaining why. */
   if (gl_caps.unsupported)
   {
      log_cb(RETRO_LOG_ERROR,
            "[gl_context_reset] aborting renderer init due to "
            "unsupported GL/GLES version (see [gl_caps] error "
            "above).\n");
      return;
   }

   static_renderer.state_data = (gl_renderer *)calloc(1, sizeof(gl_renderer));
   if (!static_renderer.state_data)
   {
      log_cb(RETRO_LOG_ERROR,
            "[gl_context_reset] OOM allocating gl_renderer\n");
      return;
   }
   /* Initialise the dynamic batch vec.  gl_renderer_new doesn't
    * touch it; the per-frame draw code is the first thing to push
    * into it.  Must be init'd before any push or free or we'd
    * be reading garbage. */
   gl_primitive_batch_vec_init(&static_renderer.state_data->batches);

   if (gl_renderer_new(static_renderer.state_data, persistent_config))
   {
      static_renderer.inited = true;
      static_renderer.state  = GL_STATE_VALID;

      GPU_RestoreStateP1(true);
      GPU_RestoreStateP2(true);
      GPU_RestoreStateP3();
   }
   else
   {
      log_cb(RETRO_LOG_WARN, "[gl_context_reset] gl_renderer_new failed. State will be invalid.\n");
      /* Tear down anything gl_renderer_new acquired before failing
       * so we don't leak GL resources or heap. */
      gl_renderer_free(static_renderer.state_data);
      free(static_renderer.state_data);
      static_renderer.state_data = NULL;
   }
}

static void gl_context_destroy(void)
{
   log_cb(RETRO_LOG_DEBUG, "gl_context_destroy called.\n");

   if (static_renderer.state_data)
   {
      gl_renderer_free(static_renderer.state_data);
      free(static_renderer.state_data);
   }

   static_renderer.state_data = NULL;
   static_renderer.state      = GL_STATE_INVALID;
   static_renderer.inited     = false;
}

static struct retro_system_av_info get_av_info(gl_video_clock std)
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
   struct retro_variable var                = {0};

   /* This function currently queries core options rather than
      checking gl_renderer state; possible to refactor? */

   get_variables(&upscaling, &display_vram);

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
   struct retro_system_av_info result;

   /* This will possibly trigger the frontend to reconfigure itself */
   if (static_renderer.inited)
      rsx_gl_refresh_variables();

   result = get_av_info(static_renderer.video_clock);
   memcpy(info, &result, sizeof(result));
}

bool rsx_gl_open(bool is_pal)
{
   enum retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888;
   gl_video_clock clock = is_pal ? VIDEO_CLOCK_PAL : VIDEO_CLOCK_NTSC;
   /* Compile-time profile string - what GL feature set this build
    * was compiled to assume.  This is independent of the runtime
    * caps detection in gl_caps_init: this tells you what subset of
    * GL the build was *targeted at*; gl_caps tells you what the
    * driver actually exposes.  Useful for triage when a bug report
    * comes in - confirms which build artifact the user is running. */
   const char *profile_str =
#if defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
      "OpenGL ES 3.x"
#elif defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES2)
      "OpenGL ES 2.x"
#elif defined(CORE)
      "OpenGL Core 3.3+"
#else
      "OpenGL (compatibility)"
#endif
      ;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f))
      return false;

   /* Register our hardware render context with the libretro
    * frontend.  This was previously routed through the glsm shim
    * (GLSM_CTL_STATE_CONTEXT_INIT); the shim mostly maintained an
    * extensive gl_state mirror that beetle never used, so the
    * registration is now done directly here.  Compile-time
    * profile selection follows the conditions glsm used (GLES
    * builds always force the matching context_type; desktop
    * builds are configurable but beetle pins to GL Core 3.3). */
   memset(&hw_render, 0, sizeof(hw_render));
#if defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   hw_render.context_type    = RETRO_HW_CONTEXT_OPENGLES3;
#elif defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES2)
   hw_render.context_type    = RETRO_HW_CONTEXT_OPENGLES2;
#else
   hw_render.context_type    = RETRO_HW_CONTEXT_OPENGL_CORE;
   hw_render.version_major   = 3;
   hw_render.version_minor   = 3;
#endif
   hw_render.context_reset      = gl_context_reset;
   hw_render.context_destroy    = gl_context_destroy;
   hw_render.depth              = true;
   hw_render.stencil            = false;
   hw_render.bottom_left_origin = true;
   hw_render.cache_context      = false;

   log_cb(RETRO_LOG_INFO,
         "[rsx_gl_open] requesting hardware render context: %s\n",
         profile_str);

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
   {
      log_cb(RETRO_LOG_ERROR,
            "[rsx_gl_open] frontend rejected SET_HW_RENDER for %s\n",
            profile_str);
      return false;
   }

   /* No context until 'context_reset' is called */
   static_renderer.video_clock  = clock;

   return true;
}

void rsx_gl_close(void)
{
   static_renderer.state       = GL_STATE_INVALID;
   static_renderer.video_clock = VIDEO_CLOCK_NTSC;
}

void rsx_gl_refresh_variables(void)
{
   gl_renderer* renderer = NULL;
   bool reconfigure_frontend;

   if (!static_renderer.inited)
      return;

   switch (static_renderer.state)
   {
      case GL_STATE_VALID:
         renderer = static_renderer.state_data;
         break;
      case GL_STATE_INVALID:
         /* Nothing to be done if we don't have a GL context */
         return;
   }

   reconfigure_frontend = retro_refresh_variables(renderer);

   if (reconfigure_frontend)
   {
      /* The resolution has changed, we must tell the frontend
       * to change its format */
      struct retro_variable var = {0};
      struct retro_system_av_info av_info;
      bool ok;

      av_info = get_av_info(static_renderer.video_clock);

      /* This call can potentially (but not necessarily) call
       * 'context_destroy' and 'context_reset' to reinitialize */
      ok = environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);

      if (!ok)
      {
         log_cb(RETRO_LOG_WARN, "Couldn't change frontend resolution\n");
         log_cb(RETRO_LOG_DEBUG, "Try resetting to enable the new configuration\n");
      }
   }
}

void rsx_gl_prepare_frame(void)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
   {
      log_cb(RETRO_LOG_ERROR, "[rsx_gl_prepare_frame] Renderer state marked as valid but state data is null.\n");
      return;
   }

   /* In case we're upscaling we need to increase the line width
    * proportionally */
   glLineWidth((GLfloat)renderer->internal_upscaling);
   glEnable(GL_SCISSOR_TEST);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   /* Used for PSX GPU command blending */
   glBlendColor(0.25, 0.25, 0.25, 0.5);

   apply_scissor(renderer);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, renderer->fb_texture.id);
}

static void compute_vram_framebuffer_dimensions(gl_renderer *renderer)
{
   /* Compute native PSX framebuffer dimensions for current frame
      and store results in renderer->config.display_resolution */

   uint16_t clock_div;
   uint16_t fb_width;
   uint16_t fb_height;

   if (!renderer)
      return;

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

      default: /* Should not be here, if we ever get here then log and crash? */
         break;
   } /* First we get the horizontal range in number of pixel clock period */
   fb_width = (renderer->config.display_area_hrange[1] - renderer->config.display_area_hrange[0]); /* Then we apply the divider */
   fb_width /= clock_div; /* Then the rounding formula straight outta No$ */
   fb_width = (fb_width + 2) & ~3;

   fb_height = (renderer->config.display_area_vrange[1] - renderer->config.display_area_vrange[0]);
   fb_height *= renderer->config.is_480i ? 2 : 1;

   renderer->config.display_resolution[0] = fb_width;
   renderer->config.display_resolution[1] = fb_height;
}

void rsx_gl_finalize_frame(const void *fb, unsigned width,
                           unsigned height, unsigned pitch)
{
   gl_renderer *renderer;

   /* Setup 2 triangles that cover the entire framebuffer
      then copy the displayed portion of the screen from fb_out */

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Draw pending commands */
   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   /* Calculate native PSX framebuffer dimensions to update renderer
      state before calling bind_libretro_framebuffer */
   compute_vram_framebuffer_dimensions(renderer);

   /* We can now render to the frontend's buffer */
   bind_libretro_framebuffer(renderer);

   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_BLEND);

   /* Clear the screen no matter what: prevents possible leftover 
      pixels from previous frame when loading save state for any 
      games not using standard framebuffer heights */
   glClearColor(0.0, 0.0, 0.0, 0.0);
   glClear(GL_COLOR_BUFFER_BIT);

   if (!renderer->config.display_off || renderer->display_vram)
   {
      /* First we draw the visible part of fb_out */
      uint16_t fb_x_start = renderer->config.display_top_left[0];
      uint16_t fb_y_start = renderer->config.display_top_left[1];
      uint16_t fb_width   = renderer->config.display_resolution[0];
      uint16_t fb_height  = renderer->config.display_resolution[1];

      GLint depth_24bpp   = (GLint) renderer->config.display_24bpp;

      /* Bind 'fb_out' to texture unit 1 */
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, renderer->fb_out.id);

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
         gl_output_vertex slice[4] =
         {
            { {-1.0, -1.0}, {0,         fb_height}   },
            { { 1.0, -1.0}, {fb_width , fb_height}   },
            { {-1.0,  1.0}, {0,         0} },
            { { 1.0,  1.0}, {fb_width,  0} }
         };

         if (renderer->output_buffer)
         {
            gl_draw_buffer_push_slice(renderer->output_buffer, &slice, 4,
                  sizeof(gl_output_vertex));

            if (renderer->output_buffer->program)
            {
               glUseProgram(renderer->output_buffer->program->id);
               glUniform1i(gl_uniform_map_get(&renderer->output_buffer->program->uniforms, "fb"), 1);
               glUniform2ui(gl_uniform_map_get(&renderer->output_buffer->program->uniforms, "offset"), fb_x_start, fb_y_start);

               glUniform1i(gl_uniform_map_get(&renderer->output_buffer->program->uniforms, "depth_24bpp"), depth_24bpp);

               glUniform1ui(gl_uniform_map_get(&renderer->output_buffer->program->uniforms, "internal_upscaling"), renderer->internal_upscaling);
            }

            if (!gl_draw_buffer_is_empty(renderer->output_buffer))
               gl_draw_buffer_draw(renderer->output_buffer, GL_TRIANGLE_STRIP);
         }
      }
   }

   /* TODO - Hack: copy fb_out back into fb_texture at the end of every
    * frame to make offscreen rendering kinda sorta work. Very messy
    * and slow. */
   {
      gl_framebuffer _fb;
      gl_image_load_vertex slice[4] =
      {
         {   {   0,   0   }   },
         {   {1023,   0   }   },
         {   {   0, 511   }   },
         {   {1023, 511   }   },
      };

      if (renderer->image_load_buffer)
      {
         gl_draw_buffer_push_slice(renderer->image_load_buffer, &slice, 4,
               sizeof(gl_image_load_vertex));

         if (renderer->image_load_buffer->program)
         {
            glUseProgram(renderer->image_load_buffer->program->id);
            glUniform1i(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "fb_texture"), 1);
         }
      }

      /* GL_SCISSOR_TEST and GL_BLEND were disabled at the top of
       * the finalize block (a few hundred lines up) for the
       * frontend output draw, and nothing in the output draw or
       * gl_draw_buffer_draw modifies either, so they are still off
       * here.  Two reflexive disables removed. */

      gl_framebuffer_init(&_fb, &renderer->fb_texture);

      if (renderer->image_load_buffer->program)
      {
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1ui(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "internal_upscaling"), renderer->internal_upscaling);
      }

      if (!gl_draw_buffer_is_empty(renderer->image_load_buffer))
         gl_draw_buffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

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
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->tex_x_mask = ~(tww << 3);
   renderer->tex_x_or   = (twx & tww) << 3;
   renderer->tex_y_mask = ~(twh << 3);
   renderer->tex_y_or   = (twy & twh) << 3;
}

void rsx_gl_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
   /* No-op for the GL backend.  The PS1 GPU's mask state
    * (MaskSetOR / MaskEvalAND) is forwarded to the renderer
    * per-draw via the `mask_test` and `set_mask` parameters on
    * each push_primitive / rsx_gl_load_image / rsx_gl_copy_rect /
    * rsx_gl_fill_rect call (see e.g. push_primitive's mask_test
    * argument).  The GL renderer batches by mask_test value (see
    * the comparison in gl_renderer_*_finalize) and never reads
    * any persistent renderer-global mask register, so there's
    * nothing for this entry point to update.
    *
    * The interface exists because the rsx_intf vtable is shared
    * across SW / GL / Vulkan backends, and at least one backend
    * (the SW path, kept in sync via the gpu.cpp-side MaskSetOR /
    * MaskEvalAND fields) does care about the global form of this
    * state.  For GL the per-draw values are sufficient. */
   (void)mask_set_or;
   (void)mask_eval_and;
}

void rsx_gl_set_draw_offset(int16_t x, int16_t y)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Finish drawing anything with the current offset */
   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   renderer->config.draw_offset[0] = x;
   renderer->config.draw_offset[1] = y;
}

void rsx_gl_set_draw_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{

   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   /* Finish drawing anything in the current area */
   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   renderer->config.draw_area_top_left[0] = x0;
   renderer->config.draw_area_top_left[1] = y0;
   /* Draw area coordinates are inclusive */
   renderer->config.draw_area_bot_right[0] = x1 + 1;
   renderer->config.draw_area_bot_right[1] = y1 + 1;

   apply_scissor(renderer);
}

void rsx_gl_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_top_left[0] = xstart;
   renderer->config.display_top_left[1] = ystart;
}

void rsx_gl_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->config.display_area_hrange[0] = x1;
   renderer->config.display_area_hrange[1] = x2;
}

void rsx_gl_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
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
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
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
   gl_renderer *renderer;
   gl_semi_transparency_mode semi_transparency_mode    = SEMI_TRANSPARENCY_MODE_ADD;
   bool semi_transparent        = false;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_AVERAGE;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_SUBTRACT_SOURCE;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD_QUARTER_SOURCE;
         break;
      default:
         break;
   }

   {
      gl_command_vertex v[3] =
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
   gl_renderer *renderer;
   gl_semi_transparency_mode semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
   bool semi_transparent     = false;
   bool is_semi_transparent;
   bool is_textured;
   unsigned index;
   unsigned index_pos;
   unsigned i;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_AVERAGE;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_SUBTRACT_SOURCE;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD_QUARTER_SOURCE;
         break;
      default:
         break;
   }

   {
      gl_command_vertex v[4] =
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

      is_semi_transparent = v[0].semi_transparent == 1;
      is_textured         = v[0].texture_blend_mode != 0;

      vertex_preprocessing(renderer, v, 4,
            GL_TRIANGLES, semi_transparency_mode, mask_test, set_mask);

      index     = gl_draw_buffer_next_index(renderer->command_buffer);
      index_pos = renderer->vertex_index_pos;

      for (i = 0; i < 6; i++)
         renderer->vertex_indices[renderer->vertex_index_pos++] = index + indices[i];

      /* Add transparent pass if needed */
      if (is_semi_transparent && is_textured)
         vertex_add_blended_pass(renderer, index_pos);

      gl_draw_buffer_push_slice(renderer->command_buffer, v, 4,
            sizeof(gl_command_vertex));
   }
}

void rsx_gl_push_line(
      int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0, uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   gl_renderer *renderer;
   gl_semi_transparency_mode semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
   bool semi_transparent = false;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   switch (blend_mode)
   {
      case -1:
         semi_transparent       = false;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 0:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_AVERAGE;
         break;
      case 1:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD;
         break;
      case 2:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_SUBTRACT_SOURCE;
         break;
      case 3:
         semi_transparent       = true;
         semi_transparency_mode = SEMI_TRANSPARENCY_MODE_ADD_QUARTER_SOURCE;
         break;
      default:
         break;
   }

   {
      gl_command_vertex v[2] = {
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
}

void rsx_gl_load_image(
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram,
      bool mask_test, bool set_mask)
{
   gl_renderer *renderer;
   gl_framebuffer _fb;
   uint16_t top_left[2];
   uint16_t dimensions[2];
   uint16_t x_start;
   uint16_t x_end;
   uint16_t y_start;
   uint16_t y_end;
   gl_image_load_vertex slice[4];

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   renderer->set_mask     = set_mask;
   renderer->mask_test    = mask_test;

   top_left[0]            = x;
   top_left[1]            = y;
   dimensions[0]          = w;
   dimensions[1]          = h;

   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   gl_texture_set_sub_image_window(
         &renderer->fb_texture,
         top_left,
         dimensions,
         (size_t) VRAM_WIDTH_PIXELS,
         GL_RGBA,
#ifdef HAVE_OPENGLES3
         GL_UNSIGNED_SHORT_5_5_5_1,
#else
         GL_UNSIGNED_SHORT_1_5_5_5_REV,
#endif
         vram);

   x_start    = top_left[0];
   x_end      = x_start + dimensions[0];
   y_start    = top_left[1];
   y_end      = y_start + dimensions[1];

   {
      gl_image_load_vertex init[4] =
      {
         {   {x_start,   y_start }   },
         {   {x_end,     y_start }   },
         {   {x_start,   y_end   }   },
         {   {x_end,     y_end   }   }
      };
      memcpy(slice, init, sizeof(slice));
   }

   if (renderer->image_load_buffer)
   {
      gl_draw_buffer_push_slice(renderer->image_load_buffer, slice, 4,
            sizeof(gl_image_load_vertex));

      if (renderer->image_load_buffer->program)
      {
         glUseProgram(renderer->image_load_buffer->program->id);
         glUniform1i(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "fb_texture"), 0);
         /* fb_texture is always at 1x */
         glUniform1ui(gl_uniform_map_get(&renderer->image_load_buffer->program->uniforms, "internal_upscaling"), 1);
      }
   }

   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);

   /* Bind the output framebuffer */
   gl_framebuffer_init(&_fb, &renderer->fb_out);

   if (!gl_draw_buffer_is_empty(renderer->image_load_buffer))
      gl_draw_buffer_draw(renderer->image_load_buffer, GL_TRIANGLE_STRIP);

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

   gl_renderer *renderer;
   uint16_t top_left[2];
   uint16_t dimensions[2];
   uint8_t col[3];
   uint16_t draw_area_top_left[2];
   uint16_t draw_area_bot_right[2];

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   top_left[0]   = x;
   top_left[1]   = y;
   dimensions[0] = w;
   dimensions[1] = h;
   col[0]        = (uint8_t) color;
   col[1]        = (uint8_t) (color >> 8);
   col[2]        = (uint8_t) (color >> 16);

   /* Draw pending commands */
   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   /* Fill rect ignores the draw area. Save the previous value
    * and reconfigure the scissor box to the fill rectangle
    * instead. */
   draw_area_top_left[0]  = renderer->config.draw_area_top_left[0];
   draw_area_top_left[1]  = renderer->config.draw_area_top_left[1];
   draw_area_bot_right[0] = renderer->config.draw_area_bot_right[0];
   draw_area_bot_right[1] = renderer->config.draw_area_bot_right[1];

   renderer->config.draw_area_top_left[0]  = top_left[0];
   renderer->config.draw_area_top_left[1]  = top_left[1];
   renderer->config.draw_area_bot_right[0] = top_left[0] + dimensions[0];
   renderer->config.draw_area_bot_right[1] = top_left[1] + dimensions[1];

   apply_scissor(renderer);

   /* This scope is intentional, just like in the Rust version */
   {
      /* Bind the out framebuffer */
      gl_framebuffer _fb;
      gl_framebuffer_init(&_fb, &renderer->fb_out);

#ifdef HAVE_OPENGLES3
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_TEXTURE_2D,
            renderer->fb_out_depth.id,
            0);
#else
      glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            renderer->fb_out_depth.id,
            0);
#endif

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
      bool mask_test, bool set_mask) /* TODO use mask for copy. See software renderer */
{
   gl_renderer *renderer;
   uint16_t source_top_left[2];
   uint16_t target_top_left[2];
   uint16_t dimensions[2];
   uint32_t upscale;
   GLint new_src_x;
   GLint new_src_y;
   GLint new_dst_x;
   GLint new_dst_y;
   GLsizei new_w;
   GLsizei new_h;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
   if (!renderer)
      return;

   if (src_x == dst_x && src_y == dst_y)
     return;

   renderer->set_mask          = set_mask;
   renderer->mask_test         = mask_test;

   source_top_left[0] = src_x;
   source_top_left[1] = src_y;
   target_top_left[0] = dst_x;
   target_top_left[1] = dst_y;
   dimensions[0]      = w;
   dimensions[1]      = h;

   /* Draw pending commands */
   if (!gl_draw_buffer_is_empty(renderer->command_buffer))
      gl_renderer_draw(renderer);

   upscale = renderer->internal_upscaling;

   new_src_x = (GLint) source_top_left[0] * (GLint) upscale;
   new_src_y = (GLint) source_top_left[1] * (GLint) upscale;
   new_dst_x = (GLint) target_top_left[0] * (GLint) upscale;
   new_dst_y = (GLint) target_top_left[1] * (GLint) upscale;

   new_w = (GLsizei) dimensions[0] * (GLsizei) upscale;
   new_h = (GLsizei) dimensions[1] * (GLsizei) upscale;

   /* === Choose the copy mechanism at runtime ===
    *
    * Preference order:
    *
    *   1. glCopyImageSubData (GL 4.3 / GLES 3.2 / GL_ARB_copy_image
    *      / GL_OES_copy_image / GL_EXT_copy_image / GL_NV_copy_image).
    *      Direct texture-to-texture, no framebuffer dance.  XXX it
    *      gives undefined results if the source and target areas
    *      overlap; this is not handled explicitly.
    *
    *   2. Portable fallback: bind fb_out to a transient FBO, use
    *      glCopyTexSubImage2D from the read-bound FBO into the
    *      same texture.  Works on every profile that has FBO
    *      support (GL 3.0+, GLES 2.0+).  This is the path used
    *      historically by the NEW_COPY_RECT codepath, kept here
    *      as the universal fallback.  Known to cause screen
    *      flickering on Dead or Alive / Tekken 3 (high-res
    *      interlaced); investigate before relying on it for
    *      serious work. */
   if (gl_caps.fp_glCopyImageSubData)
   {
      gl_caps.fp_glCopyImageSubData(
            renderer->fb_out.id, GL_TEXTURE_2D, 0, new_src_x, new_src_y, 0,
            renderer->fb_out.id, GL_TEXTURE_2D, 0, new_dst_x, new_dst_y, 0,
            new_w, new_h, 1);
   }
#ifdef GL_READ_FRAMEBUFFER
   else
   {
      GLuint fb;
      glGenFramebuffers(1, &fb);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);

      /* glFramebufferTexture (layered, GL 3.2+) is not present on
       * GLES; use the 2D variant on GLES profiles. */
#ifdef HAVE_OPENGLES3
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            renderer->fb_out.id,
            0);
#else
      glFramebufferTexture(GL_READ_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            renderer->fb_out.id,
            0);
#endif

      glReadBuffer(GL_COLOR_ATTACHMENT0);

      /* TODO - binding the same texture to the framebuffer and
       * GL_TEXTURE_2D may be undefined; consider using
       * glReadPixels / glTexSubImage2D via a scratch buffer
       * instead. */
      glBindTexture(GL_TEXTURE_2D, renderer->fb_out.id);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, new_dst_x, new_dst_y,
            new_src_x, new_src_y, new_w, new_h);

      glDeleteFramebuffers(1, &fb);
   }
#endif /* GL_READ_FRAMEBUFFER */

   /* === Mirror the FBCopy result into fb_texture ===
    *
    * fb_out is the GPU rendering target (upscaled).  fb_texture is
    * the 1x source the command shader samples for textured draws.
    * The two are kept loosely in sync by an end-of-frame
    * fb_out -> fb_texture mirror at the bottom of
    * rsx_gl_finalize_frame, which is enough for textures that
    * games upload via FBWrite (those go straight into fb_texture
    * via rsx_gl_load_image).
    *
    * FBCopy is the case the end-of-frame mirror handles
    * imperfectly: the dest region in fb_out is up to date
    * immediately, but fb_texture won't see it until the frame
    * ends, so a textured draw later in this same frame that
    * samples the dest region reads stale data.  When the
    * "Software gl_framebuffer" core option is enabled, the shadow
    * software renderer keeps GPU.vram in sync and that path can
    * eventually feed fb_texture; with it disabled, GPU.vram is
    * not maintained for the FBCopy region and fb_texture goes
    * stale for the rest of the frame.  This is what produced
    * the visible one-frame lag (and a fully-stale first frame)
    * in the FF7 battle swirl on GL with software FB off.
    *
    * Mirror the dest region from fb_out down to fb_texture using
    * glBlitFramebuffer so subsequent textured draws in this same
    * frame see the freshly-copied pixels.  Only do it when:
    *
    *   - has_software_fb is false: when it's true the existing
    *     end-of-frame mirror plus the SW shadow's vram already
    *     produce the right result, and adding a per-FBCopy
    *     mirror would just waste GPU work.
    *
    *   - gl_caps.fp_glBlitFramebuffer is non-NULL: GLES 2.0
    *     drivers without GL_NV_framebuffer_blit etc. lack the
    *     entry point; in that environment FF7 swirl on GL+SW-FB-off
    *     stays as broken as it was before this commit (i.e. fully
    *     stale fb_texture for the swirl region).  Such platforms
    *     should leave Software gl_framebuffer enabled.
    *
    * Note that fb_texture is permanently 1x - this mirror does
    * not preserve the upscaled detail in fb_out's dest region.
    * The visible result is that the swirl quad samples a 1x
    * snapshot of upscaled content, producing chunky pixelation
    * during the swirl.  That is a fundamental consequence of the
    * GL backend's dual-surface architecture (fb_texture stores
    * paletted texture data which can't be meaningfully upscaled)
    * and is out of scope for this fix. */
#ifdef GL_READ_FRAMEBUFFER
   if (!has_software_fb && gl_caps.fp_glBlitFramebuffer)
   {
      GLuint read_fbo = 0;
      GLuint draw_fbo = 0;
      GLboolean scissor_was_enabled;

      scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);

      glGenFramebuffers(1, &read_fbo);
      glGenFramebuffers(1, &draw_fbo);

      /* Read source: fb_out at upscaled coords. */
      glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
#ifdef HAVE_OPENGLES3
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            renderer->fb_out.id,
            0);
#else
      glFramebufferTexture(GL_READ_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            renderer->fb_out.id,
            0);
#endif
      glReadBuffer(GL_COLOR_ATTACHMENT0);

      /* Draw target: fb_texture at native coords. */
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
#ifdef HAVE_OPENGLES3
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            renderer->fb_texture.id,
            0);
#else
      glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            renderer->fb_texture.id,
            0);
#endif

      /* glBlitFramebuffer writes to the destination through the
       * scissor; disable so we always cover the full target rect. */
      if (scissor_was_enabled)
         glDisable(GL_SCISSOR_TEST);

      /* GL_NEAREST matches the historical 1x-degrade behaviour
       * of the software FBCopy path. */
      gl_caps.fp_glBlitFramebuffer(
            new_dst_x, new_dst_y,
            new_dst_x + new_w, new_dst_y + new_h,
            (GLint) dst_x, (GLint) dst_y,
            (GLint) (dst_x + w), (GLint) (dst_y + h),
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);

      if (scissor_was_enabled)
         glEnable(GL_SCISSOR_TEST);

      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &read_fbo);
      glDeleteFramebuffers(1, &draw_fbo);
   }
#endif /* GL_READ_FRAMEBUFFER */

#ifdef DEBUG
   get_error("rsx_gl_copy_rect");
#endif
}

void rsx_gl_toggle_display(bool status)
{
   gl_renderer *renderer;

   if (static_renderer.state == GL_STATE_INVALID)
      return;

   renderer = static_renderer.state_data;
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
