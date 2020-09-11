#include "atlas.hpp"
#include <algorithm>
#include <assert.h>

using namespace std;

namespace PSX
{

FBAtlas::FBAtlas()
{
	for (auto &f : fb_info)
		f = STATUS_FB_PREFER;
}

void FBAtlas::load_image(const Rect &rect)
{
	write_compute(Domain::Unscaled, rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) |= STATUS_TEXTURE_LOADED;
}

bool FBAtlas::texture_loaded(const Rect &rect)
{
	unsigned ret = 0;

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	// Ignore first/last row/column which tend to be different
	// Otherwise transition screen in Silent Hill gets wonky
	for (unsigned y = ybegin + 1; y <= yend - 1; y++)
		for (unsigned x = xbegin + 1; x <= xend - 1; x++)
			if (info(x, y) & STATUS_TEXTURE_LOADED)
				return true;
	return false;
}

Domain FBAtlas::blit_vram(const Rect &dst, const Rect &src)
{
#if 0
	auto src_domain = find_suitable_domain(src);
	auto dst_domain = find_suitable_domain(dst);
	Domain domain;
	if (src_domain != dst_domain)
		domain = Domain::Unscaled;
	else
		domain = src_domain;
#else
	auto domain = find_suitable_domain(src);
#endif

	sync_domain(domain, src);
	sync_domain(domain, dst);
	read_domain(domain, Stage::Compute, src);
	write_domain(domain, Stage::Compute, dst);

	unsigned dst_xbegin = dst.x / BLOCK_WIDTH;
	unsigned dst_xend = (dst.x + dst.width - 1) / BLOCK_WIDTH;
	unsigned dst_ybegin = dst.y / BLOCK_HEIGHT;
	unsigned dst_yend = (dst.y + dst.height - 1) / BLOCK_HEIGHT;

	unsigned src_xbegin = src.x / BLOCK_WIDTH;
	unsigned src_xend = (src.x + src.width - 1) / BLOCK_WIDTH;
	unsigned src_ybegin = src.y / BLOCK_HEIGHT;
	unsigned src_yend = (src.y + src.height - 1) / BLOCK_HEIGHT;

	for (unsigned j = 0; j <= min(dst_yend - dst_ybegin, src_yend - src_ybegin); j++)
		for (unsigned i = 0; i <= min(dst_xend - dst_xbegin, src_xend - src_xbegin); i++)
		{
			bool loaded = info(src_xbegin + i, src_ybegin + j) & STATUS_TEXTURE_LOADED;
			if (loaded)
				info(dst_xbegin + i, dst_ybegin + j) |= STATUS_TEXTURE_LOADED;
			else
				info(dst_xbegin + i, dst_ybegin + j) &= ~STATUS_TEXTURE_LOADED;
		}

	return domain;
}

void FBAtlas::read_fragment(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Fragment, rect);
}

void FBAtlas::read_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Compute, rect);
}

void FBAtlas::write_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Compute, rect);
}

void FBAtlas::read_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::write_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::read_texture(Domain domain)
{
	auto shifted = renderpass.texture_window;
	bool palette;
	switch (renderpass.texture_mode)
	{
	case TextureMode::Palette4bpp:
	case TextureMode::Palette8bpp:
		palette = true;
		break;

	default:
		palette = false;
		break;
	}
	shifted.x += renderpass.texture_offset_x;
	shifted.y += renderpass.texture_offset_y;

	//auto domain = palette ? Domain::Unscaled : find_suitable_domain(shifted);
	sync_domain(domain, shifted);

	Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
		                  renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

	if (palette)
		sync_domain(domain, palette_rect);

	read_domain(domain, Stage::FragmentTexture, shifted);
	if (palette)
		read_domain(domain, Stage::FragmentTexture, palette_rect);
}

bool FBAtlas::write_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains = 0;
	unsigned resolve_domains = 0;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE | STATUS_FB_READ | STATUS_TEXTURE_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_WRITE;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_WRITE;
		else if (stage == Stage::Fragment)
		{
			// Write-after-write in fragment is handled implicitly.
			// Write-after-read means rendering to a block after reading it as a texture.
			// This is a hazard we must handle.
			hazard_domains &= ~STATUS_FRAGMENT_FB_WRITE;
			resolve_domains = STATUS_FRAGMENT_FB_WRITE;
		}
		resolve_domains |= STATUS_FB_ONLY;
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE | STATUS_SFB_READ | STATUS_TEXTURE_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_WRITE;
		else if (stage == Stage::Fragment)
		{
			// Write-after-write in fragment is handled implicitly.
			// Write-after-read means rendering to a block after reading it as a texture.
			// This is a hazard we must handle.
			hazard_domains &= ~STATUS_FRAGMENT_SFB_WRITE;
			resolve_domains = STATUS_FRAGMENT_SFB_WRITE;
		}
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_WRITE;
		resolve_domains |= STATUS_SFB_ONLY;
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	// Trying to update VRAM before fragment is done reading it.
	// We could use copy-on-write here to avoid flushing, but this scenario is very rare.
	if (write_domains & STATUS_TEXTURE_READ)
		flush_render_pass();

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) = (info(x, y) & ~STATUS_OWNERSHIP_MASK) | resolve_domains;

	return (write_domains & STATUS_FRAGMENT_SFB_READ) != 0;
}

void FBAtlas::read_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains = 0;
	unsigned resolve_domains = 0;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_READ;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT_FB_READ;
			resolve_domains = STATUS_FRAGMENT_FB_READ;
		}
		else if (stage == Stage::FragmentTexture)
		{
			hazard_domains &= ~(STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ);
			resolve_domains = STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ;
		}
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_READ;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT_SFB_READ;
			resolve_domains = STATUS_FRAGMENT_SFB_READ;
		}
		else if (stage == Stage::FragmentTexture)
		{
			hazard_domains &= ~(STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ);
			resolve_domains = STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ;
		}
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) |= resolve_domains;
}

void FBAtlas::sync_domain(Domain domain, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	// If we need to see a "clean" version
	// of a framebuffer domain, we need to see
	// anything other than this flag.
	unsigned dirty_bits = 1u << (domain == Domain::Unscaled ? STATUS_SFB_ONLY : STATUS_FB_ONLY);
	unsigned bits = 0;

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			bits |= 1u << (info(x, y) & STATUS_OWNERSHIP_MASK);

	unsigned write_domains = 0;

	// We're asserting that a region is up to date, but it's
	// not, so we have to resolve it.
	if ((bits & dirty_bits) == 0)
		return;

	// For scaled domain,
	// we need to blit from unscaled domain to scaled.
	unsigned ownership;
	unsigned hazard_domains;
	unsigned resolve_domains;
	if (domain == Domain::Scaled)
	{
		ownership = STATUS_FB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_SFB_READ;

		//resolve_domains = STATUS_TRANSFER_FB_READ | STATUS_FB_PREFER | STATUS_TRANSFER_SFB_WRITE;
		resolve_domains = STATUS_COMPUTE_FB_READ | STATUS_FB_PREFER | STATUS_COMPUTE_SFB_WRITE;
	}
	else
	{
		ownership = STATUS_SFB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_FB_READ;

		//resolve_domains = STATUS_TRANSFER_SFB_READ | STATUS_SFB_PREFER | STATUS_TRANSFER_FB_WRITE;
		resolve_domains = STATUS_COMPUTE_SFB_READ | STATUS_SFB_PREFER | STATUS_COMPUTE_FB_WRITE;
	}

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			auto &mask = info(x, y);
			// If our block isn't in the ownership class we want,
			// we need to read from one block and write to the other.
			// We might have to wait for writers on read,
			// and add hazard masks for our writes
			// so other readers can wait for us.
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
				write_domains |= mask & hazard_domains;
		}
	}

	// If we hit any hazard, resolve it.
	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			auto &mask = info(x, y);
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
			{
				mask &= ~STATUS_OWNERSHIP_MASK;
				mask |= resolve_domains;
				listener->resolve(domain, (BLOCK_WIDTH * x) & (FB_WIDTH - 1), (BLOCK_HEIGHT * y) & (FB_HEIGHT - 1));
			}
		}
	}
}

Domain FBAtlas::find_suitable_domain(const Rect &rect)
{
	if (inside_render_pass(rect))
		return Domain::Scaled;

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			unsigned i = info(x, y) & STATUS_OWNERSHIP_MASK;
			if (i == STATUS_FB_ONLY || i == STATUS_FB_PREFER)
				return Domain::Unscaled;
		}
	}
	return Domain::Scaled;
}

bool FBAtlas::inside_render_pass(const Rect &rect)
{
	if (!renderpass.inside)
		return false;

	unsigned xbegin = rect.x & ~(BLOCK_WIDTH - 1);
	unsigned ybegin = rect.y & ~(BLOCK_HEIGHT - 1);
	unsigned xend = ((rect.x + rect.width - 1) | (BLOCK_WIDTH - 1)) + 1;
	unsigned yend = ((rect.y + rect.height - 1) | (BLOCK_HEIGHT - 1)) + 1;

	unsigned x0 = max(renderpass.rect.x, xbegin);
	unsigned x1 = min(renderpass.rect.x + renderpass.rect.width, xend);
	unsigned y0 = max(renderpass.rect.y, ybegin);
	unsigned y1 = min(renderpass.rect.y + renderpass.rect.height, yend);

	return x1 > x0 && y1 > y0;
}

void FBAtlas::flush_render_pass()
{
	if (!renderpass.inside)
		return;

	// Clear out the "shadow" stage.
	for (auto &f : fb_info)
		f &= ~STATUS_TEXTURE_READ;

	renderpass.inside = false;
	auto const &rect = renderpass.rect;
	write_domain(Domain::Scaled, Stage::Fragment, rect);
	listener->flush_render_pass(rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) &= ~STATUS_TEXTURE_LOADED;
}

void FBAtlas::set_texture_window(const Rect &rect)
{
	renderpass.texture_window = rect;
}

void FBAtlas::extend_render_pass(const Rect &rect, bool scissor)
{
	bool scissor_invariant = !scissor || renderpass.scissor.contains(rect);
	listener->set_scissored_invariant(scissor_invariant);
	auto scissored_rect = !scissor_invariant ? rect.scissor(renderpass.scissor) : rect;

	if (!scissored_rect.width || !scissored_rect.height)
		return;

	if (!renderpass.inside)
	{
		renderpass.rect = scissored_rect;
		sync_domain(Domain::Scaled, renderpass.rect);
		write_domain(Domain::Scaled, Stage::Fragment, renderpass.rect);
		renderpass.inside = true;
	}
	else if (!renderpass.rect.contains(scissored_rect))
	{
		renderpass.rect.extend_bounding_box(scissored_rect);

		// Avoid sync/write domain flushing our own render pass.
		renderpass.inside = false;

		// If we cleared the screen and we created a clear candidate,
		// everything inside this render pass can be safely discarded.
		if (!scissor && scissored_rect == renderpass.rect)
			discard_render_pass();

		sync_domain(Domain::Scaled, renderpass.rect);
		if (write_domain(Domain::Scaled, Stage::Fragment, renderpass.rect))
		{
			// If render pass was flushed here due to write-after-read hazards, set rect to
			// our new scissored_rect instead.
			renderpass.inside = true;
			flush_render_pass();
			renderpass.rect = scissored_rect;
		}

		renderpass.inside = true;
	}
}

void FBAtlas::write_fragment(Domain domain, const Rect &rect)
{
	bool reads_window = renderpass.texture_mode != TextureMode::None;
	if (reads_window)
	{
		Rect shifted = renderpass.texture_window;
		bool reads_palette;
		switch (renderpass.texture_mode)
		{
		case TextureMode::Palette4bpp:
		case TextureMode::Palette8bpp:
			reads_palette = true;
			break;

		default:
			reads_palette = false;
			break;
		}
		shifted.x += renderpass.texture_offset_x;
		shifted.y += renderpass.texture_offset_y;

		const Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
			                        renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

		if (reads_palette)
		{
			if (inside_render_pass(shifted) || inside_render_pass(palette_rect))
				flush_render_pass();
		}
		else if (inside_render_pass(shifted))
			flush_render_pass();

		read_texture(domain);
	}

	extend_render_pass(rect, true);
}

void FBAtlas::clear_rect(const Rect &rect, FBColor color)
{
	// If we're clearing completely outside the renderpass, we're probably doing another render pass
	// somewhere else, so end the current one and start a new one instead.
	if (renderpass.inside && !renderpass.rect.intersects(rect))
		flush_render_pass();

	extend_render_pass(rect, false);

	// If the render pass area doesn't increase later, we can use loadOp == CLEAR instead of LOAD,
	// which helps a lot on mobile GPUs.
	listener->clear_quad(rect, color, renderpass.rect == rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) &= ~STATUS_TEXTURE_LOADED;
}

void FBAtlas::set_draw_rect(const Rect &rect)
{
	renderpass.scissor = rect;
}

void FBAtlas::discard_render_pass()
{
	renderpass.inside = false;
	listener->discard_render_pass();
}

void FBAtlas::notify_external_barrier(StatusFlags domains)
{
	static const StatusFlags compute_read_stages = STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ;
	static const StatusFlags compute_write_stages = STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE;
	static const StatusFlags transfer_read_stages = STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ;
	static const StatusFlags transfer_write_stages = STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE;
	static const StatusFlags fragment_write_stages = STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_FB_WRITE;
	static const StatusFlags fragment_read_stages = STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_FB_READ;

	if (domains & compute_write_stages)
		domains |= compute_write_stages | compute_read_stages;
	if (domains & compute_read_stages)
		domains |= compute_read_stages;
	if (domains & transfer_write_stages)
		domains |= transfer_write_stages | transfer_read_stages;
	if (domains & transfer_read_stages)
		domains |= transfer_read_stages;
	if (domains & fragment_write_stages)
		domains |= fragment_write_stages | fragment_read_stages;
	if (domains & fragment_read_stages)
		domains |= fragment_read_stages;

	for (auto &f : fb_info)
		f &= ~domains;
}

void FBAtlas::pipeline_barrier(StatusFlags domains)
{
	if (domains & (STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_SFB_READ))
		flush_render_pass();
	listener->hazard(domains);
	notify_external_barrier(domains);
}
}
