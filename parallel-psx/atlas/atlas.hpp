#pragma once

#include <stdint.h>
#include <vector>

namespace PSX
{
static const unsigned FB_WIDTH = 1024;
static const unsigned FB_HEIGHT = 512;
static const unsigned BLOCK_WIDTH = 8;
static const unsigned BLOCK_HEIGHT = 8;
static const unsigned NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH;
static const unsigned NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT;

enum class Domain : unsigned
{
	Unscaled,
	Scaled
};

enum class Stage : unsigned
{
	Compute,
	Transfer,
	Fragment,
	FragmentTexture
};

enum class TextureMode
{
	None,
	Palette4bpp,
	Palette8bpp,
	ABGR1555
};

struct Rect
{
	unsigned x = 0;
	unsigned y = 0;
	unsigned width = 0;
	unsigned height = 0;

	Rect() = default;
	Rect(unsigned x, unsigned y, unsigned width, unsigned height)
	    : x(x)
	    , y(y)
	    , width(width)
	    , height(height)
	{
	}

	inline bool operator==(const Rect &rect) const
	{
		return x == rect.x && y == rect.y && width == rect.width && height == rect.height;
	}

	inline bool operator!=(const Rect &rect) const
	{
		return x != rect.x || y != rect.y || width != rect.width || height != rect.height;
	}

	inline bool contains(const Rect &rect) const
	{
		return x <= rect.x && y <= rect.y && (x + width) >= (rect.x + rect.width) &&
		       (y + height) >= (rect.y + rect.height);
	}

	inline bool intersects(const Rect &rect) const
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned xend = (x_end_self < x_end_other) ? x_end_self : x_end_other;
		unsigned xbegin = (x > rect.x) ? x : rect.x;
		unsigned yend = (y_end_self < y_end_other) ? y_end_self : y_end_other;
		unsigned ybegin = (y > rect.y) ? y : rect.y;
		return xbegin < xend && ybegin < yend;
	}

	inline Rect scissor(const Rect &rect) const
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned x0 = (x > rect.x) ? x : rect.x;
		unsigned y0 = (y > rect.y) ? y : rect.y;
		unsigned x1 = (x_end_self < x_end_other) ? x_end_self : x_end_other;
		unsigned y1 = (y_end_self < y_end_other) ? y_end_self : y_end_other;
		unsigned width = (x1 > x0) ? (x1 - x0) : 0u;
		unsigned height = (y1 > y0) ? (y1 - y0) : 0u;
		return { x0, y0, width, height };
	}

	inline void extend_bounding_box(const Rect &rect)
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned x0 = (x < rect.x) ? x : rect.x;
		unsigned y0 = (y < rect.y) ? y : rect.y;
		unsigned x1 = (x_end_self > x_end_other) ? x_end_self : x_end_other;
		unsigned y1 = (y_end_self > y_end_other) ? y_end_self : y_end_other;
		x = x0;
		y = y0;
		width = x1 - x0;
		height = y1 - y0;
	}
};

enum StatusFlag
{
	STATUS_FB_ONLY = 0,
	STATUS_FB_PREFER = 1,
	STATUS_SFB_ONLY = 2,
	STATUS_SFB_PREFER = 3,
	STATUS_OWNERSHIP_MASK = 3,

	STATUS_COMPUTE_FB_READ = 1 << 2,
	STATUS_COMPUTE_FB_WRITE = 1 << 3,
	STATUS_COMPUTE_SFB_READ = 1 << 4,
	STATUS_COMPUTE_SFB_WRITE = 1 << 5,

	STATUS_TRANSFER_FB_READ = 1 << 6,
	STATUS_TRANSFER_SFB_READ = 1 << 7,
	STATUS_TRANSFER_FB_WRITE = 1 << 8,
	STATUS_TRANSFER_SFB_WRITE = 1 << 9,

	STATUS_FRAGMENT_SFB_READ = 1 << 10,
	STATUS_FRAGMENT_SFB_WRITE = 1 << 11,
	STATUS_FRAGMENT_FB_READ = 1 << 12,
	STATUS_FRAGMENT_FB_WRITE = 1 << 13,

	// A special stage to allow fragment to detect when it's causing a feedback loop with texture read -> fragment write.
	// This flag is added in combination with FRAGMENT_FB_READ.
	STATUS_TEXTURE_READ = 1 << 14,

	// For determining if a texture read is from a loaded image or previous rendered content
	STATUS_TEXTURE_RENDERED = 1 << 15,

	STATUS_FB_READ = STATUS_COMPUTE_FB_READ | STATUS_TRANSFER_FB_READ | STATUS_FRAGMENT_FB_READ,
	STATUS_FB_WRITE = STATUS_COMPUTE_FB_WRITE | STATUS_TRANSFER_FB_WRITE | STATUS_FRAGMENT_FB_WRITE,
	STATUS_SFB_READ = STATUS_COMPUTE_SFB_READ | STATUS_TRANSFER_SFB_READ | STATUS_FRAGMENT_SFB_READ,
	STATUS_SFB_WRITE = STATUS_COMPUTE_SFB_WRITE | STATUS_TRANSFER_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_FRAGMENT =
	    STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_ALL = STATUS_FB_READ | STATUS_FB_WRITE | STATUS_SFB_READ | STATUS_SFB_WRITE
};
using StatusFlags = uint16_t;

class Renderer;

class FBAtlas
{
public:
	FBAtlas();

	void set_hazard_listener(Renderer *hazard)
	{
		listener = hazard;
	}

	void read_compute(Domain domain, const Rect &rect);
	void write_compute(Domain domain, const Rect &rect);
	void read_transfer(Domain domain, const Rect &rect);
	void write_transfer(Domain domain, const Rect &rect);
	void read_fragment(Domain domain, const Rect &rect);
	Domain blit_vram(const Rect &dst, const Rect &src);
	void load_image(const Rect &rect);
	bool texture_rendered(const Rect &rect);

	void write_fragment(Domain domain, const Rect &rect);
	void clear_rect(const Rect &rect, uint32_t color);
	void set_draw_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);

	TextureMode set_texture_mode(TextureMode mode)
	{
		TextureMode old = renderpass.texture_mode;
		renderpass.texture_mode = mode;
		return old;
	}

	void set_texture_offset(unsigned x, unsigned y)
	{
		renderpass.texture_offset_x = x;
		renderpass.texture_offset_y = y;
	}

	void set_palette_offset(unsigned x, unsigned y)
	{
		renderpass.palette_offset_x = x;
		renderpass.palette_offset_y = y;
	}

	void pipeline_barrier(StatusFlags domains);
	void notify_external_barrier(StatusFlags domains);
	void flush_render_pass();

private:
	StatusFlags fb_info[NUM_BLOCKS_X * NUM_BLOCKS_Y];
	Renderer *listener = nullptr;

	void read_domain(Domain domain, Stage stage, const Rect &rect);
	bool write_domain(Domain domain, Stage stage, const Rect &rect);
	void sync_domain(Domain domain, const Rect &rect);
	void read_texture(Domain domain);
	Domain find_suitable_domain(const Rect &rect);

	struct
	{
		Rect rect;
		Rect scissor;
		Rect texture_window;
		unsigned texture_offset_x = 0, texture_offset_y = 0;
		unsigned palette_offset_x = 0, palette_offset_y = 0;
		TextureMode texture_mode = TextureMode::None;
		bool inside = false;
	} renderpass;

	void extend_render_pass(const Rect &rect, bool scissor);

	StatusFlags &info(unsigned block_x, unsigned block_y)
	{
		block_x &= NUM_BLOCKS_X - 1;
		block_y &= NUM_BLOCKS_Y - 1;
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	const StatusFlags &info(unsigned block_x, unsigned block_y) const
	{
		block_x &= NUM_BLOCKS_X - 1;
		block_y &= NUM_BLOCKS_Y - 1;
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	void discard_render_pass();
	bool inside_render_pass(const Rect &rect);
};
}
