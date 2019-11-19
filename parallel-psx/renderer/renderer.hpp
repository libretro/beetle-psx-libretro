#pragma once

#include "../atlas/atlas.hpp"
#include "../vulkan/device.hpp"
#include "../vulkan/vulkan.hpp"

#ifdef VULKAN_WSI
#include "wsi.hpp"
#endif

#include <string.h>

namespace PSX
{

struct Vertex
{
	float x, y, w;
	uint32_t color;
	uint16_t u, v;
};

struct TextureWindow
{
	uint8_t mask_x, mask_y, or_x, or_y;
};

struct UVRect
{
	uint16_t min_u, min_v, max_u, max_v;
};

enum class SemiTransparentMode
{
	None,
	Average,
	Add,
	Sub,
	AddQuarter
};

class Renderer : private HazardListener
{
public:
	enum class ScanoutMode
	{
		// Use extra precision bits.
		ABGR1555_555,
		// Use extra precision bits to dither down to a native ABGR1555 image.
		// The dither happens in the wrong place, but should be "good" enough to feel authentic.
		ABGR1555_Dither,
		// MDEC
		BGR24
	};

	enum class ScanoutFilter
	{
		None,
		SSAA,
		MDEC_YUV
	};

	enum class WidthMode
	{
		WIDTH_MODE_256 = 0,
		WIDTH_MODE_320 = 1,
		WIDTH_MODE_512 = 2,
		WIDTH_MODE_640 = 3,
		WIDTH_MODE_368 = 4
	};

	struct DisplayRect
	{
		// Unlike Rect, the x-y coordinates for a DisplayRect can be negative
		int x = 0;
		int y = 0;
		unsigned width = 0;
		unsigned height = 0;

		DisplayRect() = default;
		DisplayRect(int x, int y, unsigned width, unsigned height)
		    : x(x)
		    , y(y)
		    , width(width)
		    , height(height)
		{
		}
	};

	struct RenderState
	{
		Rect display_mode;
		TextureWindow texture_window;
		Rect cached_window_rect;
		Rect draw_rect;
		int draw_offset_x = 0;
		int draw_offset_y = 0;
		unsigned palette_offset_x = 0;
		unsigned palette_offset_y = 0;
		unsigned texture_offset_x = 0;
		unsigned texture_offset_y = 0;

		int vert_start = 0x10;
		int vert_end = 0x100;
		int horiz_start = 0x200;
		int horiz_end = 0xC00;

		bool is_pal = false;
		bool is_480i = false;
		WidthMode width_mode = WidthMode::WIDTH_MODE_320;
		bool crop_overscan = false;

		// Experimental horizontal offset feature
		int offset_cycles = 0;

		int slstart = 0;
		int slend = 239;

		int slstart_pal = 0;
		int slend_pal = 287;

		TextureMode texture_mode = TextureMode::None;
		SemiTransparentMode semi_transparent = SemiTransparentMode::None;
		ScanoutMode scanout_mode = ScanoutMode::ABGR1555_555;
		ScanoutFilter scanout_filter = ScanoutFilter::None;
		bool dither_native_resolution = false;
		bool force_mask_bit = false;
		bool texture_color_modulate = false;
		bool mask_test = false;
		bool display_on = false;
		//bool dither = false;
		bool adaptive_smoothing = true;

		UVRect UVLimits;
	};

	struct SaveState
	{
		std::vector<uint32_t> vram;
		RenderState state;
	};

	Renderer(Vulkan::Device &device, unsigned scaling, unsigned msaa, const SaveState *save_state);
	~Renderer();

	void set_adaptive_smoothing(bool enable)
	{
		render_state.adaptive_smoothing = enable;
	}

	void set_draw_rect(const Rect &rect);
	inline void set_draw_offset(int x, int y)
	{
		render_state.draw_offset_x = x;
		render_state.draw_offset_y = y;
	}

	inline void set_scissored_invariant(bool invariant) override
	{
		queue.scissor_invariant = invariant;
	}

	void set_texture_window(const TextureWindow &rect);
	inline void set_texture_offset(unsigned x, unsigned y)
	{
		atlas.set_texture_offset(x, y);
		render_state.texture_offset_x = x;
		render_state.texture_offset_y = y;
	}

	inline void set_palette_offset(unsigned x, unsigned y)
	{
		atlas.set_palette_offset(x, y);
		render_state.palette_offset_x = x;
		render_state.palette_offset_y = y;
	}

	Vulkan::BufferHandle copy_cpu_to_vram(const Rect &rect);
	void copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram);
	uint16_t *begin_copy(Vulkan::BufferHandle handle);
	void end_copy(Vulkan::BufferHandle handle);

	void blit_vram(const Rect &dst, const Rect &src);

	void set_horizontal_display_range(int x1, int x2)
	{
		render_state.horiz_start = x1;
		render_state.horiz_end = x2;
	}

	void set_vertical_display_range(int y1, int y2)
	{
		render_state.vert_start = y1;
		render_state.vert_end = y2;
	}

	void set_display_mode(const Rect &rect, ScanoutMode mode, bool is_pal, bool is_480i, WidthMode width_mode)
	{
		if (rect != render_state.display_mode || render_state.scanout_mode != mode)
			last_scanout.reset();

		render_state.display_mode = rect;
		render_state.scanout_mode = mode;

		render_state.is_pal = is_pal;
		render_state.is_480i = is_480i;
		render_state.width_mode = width_mode;
	}

	void set_horizontal_overscan_cropping(bool crop_overscan)
	{
		render_state.crop_overscan = crop_overscan;
	}

	void set_horizontal_offset_cycles(int offset_cycles)
	{
		render_state.offset_cycles = offset_cycles;
	}

	void set_visible_scanlines(int slstart, int slend, int slstart_pal, int slend_pal)
	{
		// May need bounds checking to reject bad inputs. Currently assume all inputs are valid.
		render_state.slstart = slstart;
		render_state.slend = slend;
		render_state.slstart_pal = slstart_pal;
		render_state.slend_pal = slend_pal;
	}

	void set_display_filter(ScanoutFilter filter)
	{
		render_state.scanout_filter = filter;
	}

	void toggle_display(bool enable)
	{
		if (enable != render_state.display_on)
			last_scanout.reset();

		render_state.display_on = enable;
	}

#if 0
	void set_dither(bool dither)
	{
		render_state.dither = dither;
	}
#endif

	void set_dither_native_resolution(bool enable)
	{
		render_state.dither_native_resolution = enable;
	}

	void set_scanout_semaphore(Vulkan::Semaphore semaphore);
	void scanout();
	Vulkan::BufferHandle scanout_to_buffer(bool draw_area, unsigned &width, unsigned &height);
	Vulkan::BufferHandle scanout_vram_to_buffer(unsigned &width, unsigned &height);
	Vulkan::ImageHandle scanout_to_texture();

	inline void set_texture_mode(TextureMode mode)
	{
		render_state.texture_mode = mode;
		atlas.set_texture_mode(mode);
	}

	inline void set_semi_transparent(SemiTransparentMode state)
	{
		render_state.semi_transparent = state;
	}

	inline void set_force_mask_bit(bool enable)
	{
		render_state.force_mask_bit = enable;
	}

	inline void set_mask_test(bool enable)
	{
		render_state.mask_test = enable;
	}

	inline void set_texture_color_modulate(bool enable)
	{
		render_state.texture_color_modulate = enable;
	}

	inline void set_UV_limits(uint16_t min_u, uint16_t min_v, uint16_t max_u, uint16_t max_v)
	{
		render_state.UVLimits.min_u = min_u;
		render_state.UVLimits.min_v = min_v;
		render_state.UVLimits.max_u = max_u;
		render_state.UVLimits.max_v = max_v;
	}

	// Draw commands
	void clear_rect(const Rect &rect, FBColor color);
	void draw_line(const Vertex *vertices);
	void draw_triangle(const Vertex *vertices);
	void draw_quad(const Vertex *vertices);

	SaveState save_vram_state();

	void reset_counters()
	{
		counters = {};
	}

	void flush()
	{
		if (cmd)
			device.submit(cmd);
		cmd.reset();
		device.flush_frame();
	}

	Vulkan::Fence flush_and_signal()
	{
		Vulkan::Fence fence;
		if (cmd)
			device.submit(cmd, &fence);
		cmd.reset();
		device.flush_frame();
		return fence;
	}

	struct
	{
		unsigned render_passes = 0;
		unsigned fragment_readback_pixels = 0;
		unsigned fragment_writeout_pixels = 0;
		unsigned draw_calls = 0;
		unsigned vertices = 0;
		unsigned native_draw_calls = 0;
	} counters;

	enum class FilterMode
	{
		NearestNeighbor = 0,
		XBR = 1,
		SABR = 2,
		Bilinear = 3,
		Bilinear3Point = 4,
		JINC2 = 5
	};

	void set_filter_mode(FilterMode mode);
	ScanoutMode get_scanout_mode() const
	{
		return render_state.scanout_mode;
	}

private:
	Vulkan::Device &device;
	unsigned scaling;
	unsigned msaa;
	FilterMode primitive_filter_mode = FilterMode::NearestNeighbor;
	Vulkan::ImageHandle scaled_framebuffer;
	Vulkan::ImageHandle scaled_framebuffer_msaa;
	Vulkan::ImageHandle bias_framebuffer;
	Vulkan::ImageHandle framebuffer;
	Vulkan::Semaphore scanout_semaphore;
	std::vector<Vulkan::ImageViewHandle> scaled_views;
	FBAtlas atlas;

	Vulkan::CommandBufferHandle cmd;

	void hazard(StatusFlags flags) override;
	void resolve(Domain target_domain, unsigned x, unsigned y) override;
	void flush_render_pass(const Rect &rect) override;
	void discard_render_pass() override;
	void clear_quad(const Rect &rect, FBColor color, bool candidate) override;

	struct
	{
		Vulkan::Program *copy_to_vram;
		Vulkan::Program *copy_to_vram_masked;
		Vulkan::Program *unscaled_quad_blitter;
		Vulkan::Program *scaled_quad_blitter;
		Vulkan::Program *unscaled_dither_quad_blitter;
		Vulkan::Program *scaled_dither_quad_blitter;
		Vulkan::Program *bpp24_quad_blitter;
		Vulkan::Program *bpp24_yuv_quad_blitter;
		Vulkan::Program *resolve_to_scaled;
		Vulkan::Program *resolve_to_unscaled;

		Vulkan::Program *blit_vram_scaled;
		Vulkan::Program *blit_vram_scaled_masked;

		Vulkan::Program *blit_vram_cached_scaled;
		Vulkan::Program *blit_vram_cached_scaled_masked;
		Vulkan::Program *blit_vram_msaa_cached_scaled;
		Vulkan::Program *blit_vram_msaa_cached_scaled_masked;

		Vulkan::Program *blit_vram_unscaled;
		Vulkan::Program *blit_vram_unscaled_masked;
		Vulkan::Program *blit_vram_cached_unscaled;
		Vulkan::Program *blit_vram_cached_unscaled_masked;

		Vulkan::Program *opaque_flat;
		Vulkan::Program *opaque_textured;
		Vulkan::Program *opaque_semi_transparent;
		Vulkan::Program *semi_transparent;
		Vulkan::Program *semi_transparent_masked_add;
		Vulkan::Program *semi_transparent_masked_average;
		Vulkan::Program *semi_transparent_masked_sub;
		Vulkan::Program *semi_transparent_masked_add_quarter;
		Vulkan::Program *flat_masked_add;
		Vulkan::Program *flat_masked_average;
		Vulkan::Program *flat_masked_sub;
		Vulkan::Program *flat_masked_add_quarter;

		Vulkan::Program *mipmap_resolve;
		Vulkan::Program *mipmap_dither_resolve;
		Vulkan::Program *mipmap_energy_first;
		Vulkan::Program *mipmap_energy;
		Vulkan::Program *mipmap_energy_blur;
	} pipelines;

	Vulkan::ImageHandle dither_lut;

	void init_pipelines();
	void init_primitive_pipelines();
	void init_primitive_feedback_pipelines();
	void ensure_command_buffer();

	RenderState render_state;

	struct BufferVertex
	{
		float x, y, z, w;
		uint32_t color;
		TextureWindow window;
		int16_t pal_x, pal_y, params;
		int16_t u, v, base_uv_x, base_uv_y;
		uint16_t min_u, min_v, max_u, max_v;
	};

	struct BlitInfo
	{
		uint32_t src_offset[2];
		uint32_t dst_offset[2];
		uint32_t extent[2];
		uint32_t mask;
		uint32_t sample;
	};

	struct SemiTransparentState
	{
		int scissor_index;
		SemiTransparentMode semi_transparent;
		bool textured;
		bool masked;

		bool operator==(const SemiTransparentState &other) const
		{
			return scissor_index == other.scissor_index && semi_transparent == other.semi_transparent &&
			       textured == other.textured && masked == other.masked;
		}

		bool operator!=(const SemiTransparentState &other) const
		{
			return !(*this == other);
		}
	};

	struct ClearCandidate
	{
		Rect rect;
		FBColor color;
		float z;
	};

	struct OpaqueQueue
	{
		// Non-textured primitives.
		std::vector<BufferVertex> opaque;
		std::vector<std::pair<unsigned, int>> opaque_scissor;

		// Textured primitives, no semi-transparency.
		std::vector<BufferVertex> opaque_textured;
		std::vector<std::pair<unsigned, int>> opaque_textured_scissor;

		// Textured primitives, semi-transparency enabled.
		std::vector<BufferVertex> semi_transparent_opaque;
		std::vector<std::pair<unsigned, int>> semi_transparent_opaque_scissor;

		std::vector<BufferVertex> semi_transparent;
		std::vector<SemiTransparentState> semi_transparent_state;

		std::vector<Vulkan::ImageHandle> textures;

		std::vector<VkRect2D> scaled_resolves;
		std::vector<VkRect2D> unscaled_resolves;
		std::vector<BlitInfo> scaled_blits;
		std::vector<BlitInfo> scaled_masked_blits;
		std::vector<BlitInfo> unscaled_blits;
		std::vector<BlitInfo> unscaled_masked_blits;

		std::vector<VkRect2D> scissors;
		std::vector<ClearCandidate> clear_candidates;
		VkRect2D default_scissor;
		bool scissor_invariant = false;
	} queue;
	unsigned primitive_index = 0;
	bool render_pass_is_feedback = false;
	float last_uv_scale_x, last_uv_scale_y;

	void dispatch(const std::vector<BufferVertex> &vertices, std::vector<std::pair<unsigned, int>> &scissors);
	void render_opaque_primitives();
	void render_opaque_texture_primitives();
	void render_semi_transparent_opaque_texture_primitives();
	void render_semi_transparent_primitives();
	void reset_queue();

	float allocate_depth(const Rect &rect);

	void build_attribs(BufferVertex *verts, const Vertex *vertices, unsigned count);
	void build_line_quad(Vertex *quad, const Vertex *line);
	std::vector<BufferVertex> *select_pipeline(unsigned prims, int scissor);

	void flush_resolves();
	void flush_blits();
	void reset_scissor_queue();
	const ClearCandidate *find_clear_candidate(const Rect &rect) const;

	Rect compute_window_rect(const TextureWindow &window);

	Vulkan::ImageHandle last_scanout;
	Vulkan::ImageHandle reuseable_scanout;
	DisplayRect compute_display_rect();

	void mipmap_framebuffer();
	Vulkan::BufferHandle quad;
};
}
