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
		unsigned xend = std::min(x + width, rect.x + rect.width);
		unsigned xbegin = std::max(x, rect.x);
		unsigned yend = std::min(y + height, rect.y + rect.height);
		unsigned ybegin = std::max(y, rect.y);
		return xbegin < xend && ybegin < yend;
	}

	inline Rect scissor(const Rect &rect) const
	{
		unsigned x0 = std::max(x, rect.x);
		unsigned y0 = std::max(y, rect.y);
		unsigned x1 = std::min(x + width, rect.x + rect.width);
		unsigned y1 = std::min(y + height, rect.y + rect.height);
		unsigned width = std::max(int(x1) - int(x0), 0);
		unsigned height = std::max(int(y1) - int(y0), 0);
		return { x0, y0, width, height };
	}

	inline void extend_bounding_box(const Rect &rect)
	{
		unsigned x0 = std::min(x, rect.x);
		unsigned y0 = std::min(y, rect.y);
		unsigned x1 = std::max(x + width, rect.x + rect.width);
		unsigned y1 = std::max(y + height, rect.y + rect.height);
		x = x0;
		y = y0;
		width = x1 - x0;
		height = y1 - y0;
	}
};

using FBColor = uint32_t;

static inline uint32_t fbcolor_to_rgba8(FBColor color)
{
	// 3 LSBs are ignored.
	return color & 0xfff8f8f8u;
}

static inline void fbcolor_to_rgba32f(float *v, FBColor color)
{
	// 3 LSBs are ignored.
	unsigned r = (color >> 0) & 0xf8;
	unsigned g = (color >> 8) & 0xf8;
	unsigned b = (color >> 16) & 0xf8;
	v[0] = r * (1.0f / 255.0f);
	v[1] = g * (1.0f / 255.0f);
	v[2] = b * (1.0f / 255.0f);
	// Mask bit is always cleared.
	v[3] = 0.0f;
}

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

	STATUS_FB_READ = STATUS_COMPUTE_FB_READ | STATUS_TRANSFER_FB_READ | STATUS_FRAGMENT_FB_READ,
	STATUS_FB_WRITE = STATUS_COMPUTE_FB_WRITE | STATUS_TRANSFER_FB_WRITE | STATUS_FRAGMENT_FB_WRITE,
	STATUS_SFB_READ = STATUS_COMPUTE_SFB_READ | STATUS_TRANSFER_SFB_READ | STATUS_FRAGMENT_SFB_READ,
	STATUS_SFB_WRITE = STATUS_COMPUTE_SFB_WRITE | STATUS_TRANSFER_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_FRAGMENT =
	    STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_ALL = STATUS_FB_READ | STATUS_FB_WRITE | STATUS_SFB_READ | STATUS_SFB_WRITE
};
using StatusFlags = uint16_t;

class HazardListener
{
public:
	virtual ~HazardListener() = default;
	virtual void hazard(StatusFlags flags) = 0;
	virtual void resolve(Domain target_domain, unsigned x, unsigned y) = 0;
	virtual void flush_render_pass(const Rect &rect) = 0;
	virtual void discard_render_pass() = 0;
	virtual void clear_quad(const Rect &rect, FBColor color, bool clear_candidate) = 0;
	virtual void set_scissored_invariant(bool invariant) = 0;
};

class FBAtlas
{
public:
	FBAtlas();

	void set_hazard_listener(HazardListener *hazard)
	{
		listener = hazard;
	}

	void read_compute(Domain domain, const Rect &rect);
	void write_compute(Domain domain, const Rect &rect);
	void read_transfer(Domain domain, const Rect &rect);
	void write_transfer(Domain domain, const Rect &rect);
	void read_fragment(Domain domain, const Rect &rect);
	Domain blit_vram(const Rect &dst, const Rect &src);

	void write_fragment(const Rect &rect);
	void clear_rect(const Rect &rect, FBColor color);
	void set_draw_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);

	TextureMode set_texture_mode(TextureMode mode)
	{
		std::swap(renderpass.texture_mode, mode);
		return mode;
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
	HazardListener *listener = nullptr;

	void read_domain(Domain domain, Stage stage, const Rect &rect);
	bool write_domain(Domain domain, Stage stage, const Rect &rect);
	void sync_domain(Domain domain, const Rect &rect);
	void read_texture();
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
