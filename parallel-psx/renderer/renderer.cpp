#include "renderer.hpp"
#include "renderer_pipelines.hpp"
#ifndef NDEBUG
#include "timer.hpp"
#endif
#include <algorithm>
#include <math.h>
#include <string.h>

#include <libretro.h>
#include <libretro_options.h>
#include <libretro_cbs.h>

#ifdef __cplusplus
extern "C"
{
#endif
   extern retro_environment_t environ_cb;
#ifdef __cplusplus
}
#endif

using namespace Vulkan;
using namespace std;

namespace PSX
{
Renderer::Renderer(Device &device, unsigned scaling_, unsigned msaa_, const SaveState *state)
    : device(device)
    , scaling(scaling_)
    , msaa(msaa_)
{
	// Sanity check settings, 16x IR with 16x MSAA will exhaust most GPUs VRAM alone.
	if (scaling == 16 && msaa > 1)
	{
		LOGI("[Vulkan]: Internal resolution scale of 16x is used, limiting MSAA to 1x for memory reasons.\n");
		msaa = 1;
	}
	else if (scaling == 8 && msaa > 4)
	{
		LOGI("[Vulkan]: Internal resolution scale of 8x is used, limiting MSAA to 4x for memory reasons.\n");
		msaa = 4;
	}

	// Verify we can actually render at our target scaling factor.
	// Some devices only support 8K textures, which means max 8x scale.
	VkImageFormatProperties props;
	if (device.get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
				0,
				&props))
	{
		unsigned max_scaling = std::min(props.maxExtent.width / FB_WIDTH, props.maxExtent.height / FB_HEIGHT);
		unsigned new_scale = scaling;
		while (new_scale > max_scaling)
			new_scale >>= 1;

		if (new_scale != scaling)
		{
			LOGI("[Vulkan]: Internal resolution scale of %ux was chosen, but this is not supported, using %ux instead.\n",
					scaling, new_scale);
			scaling = new_scale;
		}
	}
	else
	{
		throw std::runtime_error("[Vulkan]: RGBA8_UNORM is not supported. This should never happen, and something might have been corrupted.\n");
	}

	auto info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;

	if (state)
	{
		render_state = state->state;
		atlas.set_texture_offset(render_state.texture_offset_x, render_state.texture_offset_y);
		atlas.set_texture_mode(render_state.texture_mode);
		atlas.set_draw_rect(render_state.draw_rect);
		atlas.set_palette_offset(render_state.palette_offset_x, render_state.palette_offset_y);
		atlas.set_texture_window(render_state.cached_window_rect);
		atlas.write_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	}

	ImageInitialData initial_vram = {
		state ? state->vram.data() : nullptr, 0, 0,
	};
	framebuffer = device.create_image(info, state ? &initial_vram : nullptr);
	framebuffer->set_layout(Layout::General);

	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.format = VK_FORMAT_R8_UNORM;
	info.levels = 1;
	bias_framebuffer = device.create_image(info, nullptr);

	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.levels = trailing_zeroes(scaling) + 1;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	scaled_framebuffer = device.create_image(info);
	scaled_framebuffer->set_layout(Layout::General);

	{
		auto view_info = scaled_framebuffer->get_view().get_create_info();
		for (unsigned i = 0; i < info.levels; i++)
		{
			view_info.base_level = i;
			view_info.levels = 1;
			scaled_views.push_back(device.create_image_view(view_info));
		}
	}

	// Check for support.
	if (msaa > 1)
	{
		if (!device.get_device_features().enabled_features.sampleRateShading)
		{
			msaa = 1;
			LOGI("[Vulkan]: sampleRateShading is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device.get_device_features().enabled_features.shaderStorageImageMultisample)
		{
			msaa = 1;
			LOGI("[Vulkan]: shaderStorageImageMultisample is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device.get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_SAMPLED_BIT,
					0,
					&props))
		{
			LOGI("[Vulkan]: Cannot use multisampling with this device.\n");
			msaa = 1;
		}
		else if ((msaa & props.sampleCounts) == 0)
		{
			unsigned new_msaa = msaa >> 1;
			while (new_msaa)
			{
				if (new_msaa & props.sampleCounts)
				{
					LOGI("[Vulkan]: MSAA sample count of %u is not supported, falling back to %u.\n",
							msaa, new_msaa);
					msaa = new_msaa;
					break;
				}
			}

			if (msaa == 0)
				msaa = 1;
		}
	}

	if (msaa > 1)
	{
		info.levels = 1;
		info.samples = static_cast<VkSampleCountFlagBits>(msaa);
		scaled_framebuffer_msaa = device.create_image(info);
		scaled_framebuffer_msaa->set_layout(Layout::General);
		// General layout for MSAA is going to be brutal bandwidth-wise, but we have no real choice.
		// The expectation is that this will be used with a lower scaling factor to compensate.
	}

	atlas.set_hazard_listener(this);

	init_pipelines();

	ensure_command_buffer();
	cmd->clear_image(*scaled_framebuffer, {});
	if (!state)
		cmd->clear_image(*framebuffer, {});
	cmd->full_barrier();

	auto dither_info = ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8_UNORM);
	// This lut is biased with 4 to be able to use UNORM easily.
	static const uint8_t dither_lut_data[16] = { 0, 4, 1, 5, 6, 2, 7, 3, 1, 5, 0, 4, 7, 3, 6, 2 };

	ImageInitialData dither_initial = { dither_lut_data };
	dither_lut = device.create_image(dither_info, &dither_initial);

	static const int8_t quad_data[] = {
		-128, -128, +127, -128, -128, +127, +127, +127,
	};

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::Device;
	buffer_create_info.size = sizeof(quad_data);
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	quad = device.create_buffer(buffer_create_info, quad_data);

	flush();
	reset_scissor_queue();
}

void Renderer::set_scanout_semaphore(Semaphore semaphore)
{
	scanout_semaphore = semaphore;
}

Renderer::SaveState Renderer::save_vram_state()
{
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = FB_WIDTH * FB_HEIGHT * sizeof(uint32_t);
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	auto buffer = device.create_buffer(buffer_create_info, nullptr);
	atlas.read_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();
	cmd->copy_image_to_buffer(*buffer, *framebuffer, 0, { 0, 0, 0 }, { FB_WIDTH, FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	flush();

	device.wait_idle();
	void *ptr = device.map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	std::vector<uint32_t> vram(FB_WIDTH * FB_HEIGHT);
	memcpy(vram.data(), ptr, FB_WIDTH * FB_HEIGHT * sizeof(uint32_t));
	device.unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	return { move(vram), render_state };
}

void Renderer::set_filter_mode(FilterMode mode)
{
	if (mode != primitive_filter_mode)
	{
		primitive_filter_mode = mode;
		init_primitive_pipelines();
	}
}

void Renderer::init_primitive_pipelines()
{
	switch (primitive_filter_mode)
	{
	case FilterMode::XBR:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_xbr_frag, sizeof(opaque_flat_xbr_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_xbr_frag, sizeof(opaque_textured_xbr_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_xbr_frag, sizeof(opaque_semitrans_xbr_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_xbr_frag, sizeof(semitrans_xbr_frag));
		break;

	case FilterMode::SABR:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_sabr_frag, sizeof(opaque_flat_sabr_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_sabr_frag, sizeof(opaque_textured_sabr_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_sabr_frag, sizeof(opaque_semitrans_sabr_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_sabr_frag, sizeof(semitrans_sabr_frag));
		break;

	case FilterMode::Bilinear:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_bilinear_frag, sizeof(opaque_flat_bilinear_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_bilinear_frag, sizeof(opaque_textured_bilinear_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_bilinear_frag, sizeof(opaque_semitrans_bilinear_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_bilinear_frag, sizeof(semitrans_bilinear_frag));
		break;

	case FilterMode::Bilinear3Point:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_3point_frag, sizeof(opaque_flat_3point_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_3point_frag, sizeof(opaque_textured_3point_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_3point_frag, sizeof(opaque_semitrans_3point_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_3point_frag, sizeof(semitrans_3point_frag));
		break;

	case FilterMode::JINC2:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_jinc2_frag, sizeof(opaque_flat_jinc2_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_jinc2_frag, sizeof(opaque_textured_jinc2_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_jinc2_frag, sizeof(opaque_semitrans_jinc2_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_jinc2_frag, sizeof(semitrans_jinc2_frag));
		break;

	default:
		pipelines.opaque_flat =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), opaque_flat_frag, sizeof(opaque_flat_frag));
		pipelines.opaque_textured = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_textured_frag, sizeof(opaque_textured_frag));
		pipelines.opaque_semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				opaque_semitrans_frag, sizeof(opaque_semitrans_frag));
		pipelines.semi_transparent = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				semitrans_frag, sizeof(semitrans_frag));
		break;
	}
}

void Renderer::init_primitive_feedback_pipelines()
{
	if (msaa > 1)
	{
		// TODO: The masked pipelines do not have filter options.
		pipelines.semi_transparent_masked_add = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				feedback_msaa_add_frag, sizeof(feedback_msaa_add_frag));
		pipelines.semi_transparent_masked_average = device.request_program(
				opaque_textured_vert, sizeof(opaque_textured_vert), feedback_msaa_avg_frag, sizeof(feedback_msaa_avg_frag));
		pipelines.semi_transparent_masked_sub = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				feedback_msaa_sub_frag, sizeof(feedback_msaa_sub_frag));
		pipelines.semi_transparent_masked_add_quarter =
			device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert), feedback_msaa_add_quarter_frag,
					sizeof(feedback_msaa_add_quarter_frag));

		pipelines.flat_masked_add = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_msaa_flat_add_frag, sizeof(feedback_msaa_flat_add_frag));
		pipelines.flat_masked_average = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_msaa_flat_avg_frag, sizeof(feedback_msaa_flat_avg_frag));
		pipelines.flat_masked_sub = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_msaa_flat_sub_frag, sizeof(feedback_msaa_flat_sub_frag));
		pipelines.flat_masked_add_quarter =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), feedback_msaa_flat_add_quarter_frag,
					sizeof(feedback_msaa_flat_add_quarter_frag));
	}
	else
	{
		// TODO: The masked pipelines do not have filter options.
		pipelines.semi_transparent_masked_add = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				feedback_add_frag, sizeof(feedback_add_frag));
		pipelines.semi_transparent_masked_average = device.request_program(
				opaque_textured_vert, sizeof(opaque_textured_vert), feedback_avg_frag, sizeof(feedback_avg_frag));
		pipelines.semi_transparent_masked_sub = device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert),
				feedback_sub_frag, sizeof(feedback_sub_frag));
		pipelines.semi_transparent_masked_add_quarter =
			device.request_program(opaque_textured_vert, sizeof(opaque_textured_vert), feedback_add_quarter_frag,
					sizeof(feedback_add_quarter_frag));

		pipelines.flat_masked_add = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_flat_add_frag, sizeof(feedback_flat_add_frag));
		pipelines.flat_masked_average = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_flat_avg_frag, sizeof(feedback_flat_avg_frag));
		pipelines.flat_masked_sub = device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert),
				feedback_flat_sub_frag, sizeof(feedback_flat_sub_frag));
		pipelines.flat_masked_add_quarter =
			device.request_program(opaque_flat_vert, sizeof(opaque_flat_vert), feedback_flat_add_quarter_frag,
					sizeof(feedback_flat_add_quarter_frag));
	}
}

void Renderer::init_pipelines()
{
	switch (scaling)
	{
	case 16:
		pipelines.resolve_to_unscaled = device.request_program(resolve_to_unscaled_16, sizeof(resolve_to_unscaled_16));
		break;

	case 8:
		pipelines.resolve_to_unscaled = device.request_program(resolve_to_unscaled_8, sizeof(resolve_to_unscaled_8));
		break;

	case 4:
		pipelines.resolve_to_unscaled = device.request_program(resolve_to_unscaled_4, sizeof(resolve_to_unscaled_4));
		break;

	default:
		pipelines.resolve_to_unscaled = device.request_program(resolve_to_unscaled_2, sizeof(resolve_to_unscaled_2));
		break;
	}

	pipelines.scaled_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.scaled_dither_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), scaled_dither_quad_frag, sizeof(scaled_dither_quad_frag));
	pipelines.bpp24_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), bpp24_quad_frag, sizeof(bpp24_quad_frag));
	pipelines.bpp24_yuv_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), bpp24_yuv_quad_frag, sizeof(bpp24_yuv_quad_frag));
	pipelines.unscaled_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.unscaled_dither_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), unscaled_dither_quad_frag, sizeof(unscaled_dither_quad_frag));

	pipelines.copy_to_vram = device.request_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.copy_to_vram_masked = device.request_program(copy_vram_masked_comp, sizeof(copy_vram_masked_comp));

	if (msaa > 1)
	{
		pipelines.resolve_to_scaled =
			device.request_program(resolve_msaa_to_scaled, sizeof(resolve_msaa_to_scaled));

		pipelines.blit_vram_scaled =
			device.request_program(blit_vram_msaa_scaled_comp, sizeof(blit_vram_msaa_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device.request_program(blit_vram_msaa_scaled_masked_comp, sizeof(blit_vram_msaa_scaled_masked_comp));
		pipelines.blit_vram_msaa_cached_scaled =
			device.request_program(blit_vram_msaa_cached_scaled_comp, sizeof(blit_vram_msaa_cached_scaled_comp));
		pipelines.blit_vram_msaa_cached_scaled_masked =
			device.request_program(blit_vram_msaa_cached_scaled_masked_comp, sizeof(blit_vram_msaa_cached_scaled_masked_comp));
	}
	else
	{
		pipelines.resolve_to_scaled = device.request_program(resolve_to_scaled, sizeof(resolve_to_scaled));

		pipelines.blit_vram_scaled = device.request_program(blit_vram_scaled_comp, sizeof(blit_vram_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device.request_program(blit_vram_scaled_masked_comp, sizeof(blit_vram_scaled_masked_comp));
	}

	pipelines.blit_vram_cached_scaled =
		device.request_program(blit_vram_cached_scaled_comp, sizeof(blit_vram_cached_scaled_comp));
	pipelines.blit_vram_cached_scaled_masked =
		device.request_program(blit_vram_cached_scaled_masked_comp, sizeof(blit_vram_cached_scaled_masked_comp));

	pipelines.blit_vram_unscaled = device.request_program(blit_vram_unscaled_comp, sizeof(blit_vram_unscaled_comp));
	pipelines.blit_vram_unscaled_masked =
		device.request_program(blit_vram_unscaled_masked_comp, sizeof(blit_vram_unscaled_masked_comp));
	pipelines.blit_vram_cached_unscaled =
		device.request_program(blit_vram_cached_unscaled_comp, sizeof(blit_vram_cached_unscaled_comp));
	pipelines.blit_vram_cached_unscaled_masked =
		device.request_program(blit_vram_cached_unscaled_masked_comp, sizeof(blit_vram_cached_unscaled_masked_comp));

	pipelines.mipmap_resolve =
		device.request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_resolve_frag, sizeof(mipmap_resolve_frag));
	pipelines.mipmap_dither_resolve =
		device.request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_dither_resolve_frag, sizeof(mipmap_dither_resolve_frag));

	pipelines.mipmap_energy = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_frag, sizeof(mipmap_energy_frag));
	pipelines.mipmap_energy_first = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_first_frag, sizeof(mipmap_energy_first_frag));
	pipelines.mipmap_energy_blur = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_blur_frag, sizeof(mipmap_energy_blur_frag));

	init_primitive_pipelines();
	init_primitive_feedback_pipelines();
}

void Renderer::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
	render_state.draw_rect = rect;

	const auto nequal = [this](const VkRect2D &a, const Rect &b) {
		return (a.offset.x != int(b.x * scaling)) || (a.offset.y != int(b.y * scaling)) ||
		       (a.extent.width != b.width * scaling) || (a.extent.height != b.height * scaling);
	};

	if (nequal(queue.scissors.back(), rect))
		queue.scissors.push_back(
		    { { int(rect.x * scaling), int(rect.y * scaling) }, { rect.width * scaling, rect.height * scaling } });
}

void Renderer::clear_rect(const Rect &rect, FBColor color)
{
	last_scanout.reset();
	atlas.clear_rect(rect, color);

	VK_ASSERT(rect.x + rect.width <= FB_WIDTH);
	VK_ASSERT(rect.y + rect.height <= FB_HEIGHT);
}

Rect Renderer::compute_window_rect(const TextureWindow &window)
{
	unsigned mask_bits_x = 32 - leading_zeroes(window.mask_x);
	unsigned mask_bits_y = 32 - leading_zeroes(window.mask_y);
	unsigned x = window.or_x & ~((1u << mask_bits_x) - 1);
	unsigned y = window.or_y & ~((1u << mask_bits_y) - 1);
	return { x, y, 1u << mask_bits_x, 1u << mask_bits_y };
}

void Renderer::set_texture_window(const TextureWindow &window)
{
	render_state.texture_window = window;
	render_state.cached_window_rect = compute_window_rect(window);
}

BufferHandle Renderer::scanout_vram_to_buffer(unsigned &width, unsigned &height)
{
	atlas.read_transfer(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = scaling * scaling * FB_WIDTH * FB_HEIGHT * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	auto buffer = device.create_buffer(buffer_create_info, nullptr);
	cmd->copy_image_to_buffer(*buffer, *scaled_framebuffer, 0, { 0, 0, 0 },
	                          { scaling * FB_WIDTH, scaling * FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	flush();
	device.wait_idle();
	width = FB_WIDTH * scaling;
	height = FB_HEIGHT * scaling;
	return buffer;
}

void Renderer::copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram)
{
	if (rect.x + rect.width > FB_WIDTH || rect.y + rect.height > FB_HEIGHT)
	{
	   LOGI("copy_vram_to_cpu_synchronous: TODO: handle wraparound case.\n");
	   return;
	}

#ifndef NDEBUG
	Util::Timer timer;
	timer.start();
#endif

	atlas.read_transfer(Domain::Unscaled, rect);
	ensure_command_buffer();

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = rect.width * rect.height * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	auto buffer = device.create_buffer(buffer_create_info, nullptr);
	cmd->copy_image_to_buffer(*buffer, *framebuffer, 0, { int(rect.x), int(rect.y), 0 },
	                          { rect.width, rect.height, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	auto fence = flush_and_signal();
	fence->wait();

	auto *mapped = static_cast<const uint32_t *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT));

	for (uint16_t y = 0; y < rect.height; y++)
	   for (uint16_t x = 0; x < rect.width; x++)
		  vram[(y + rect.y) * FB_WIDTH + (x + rect.x)] = uint16_t(mapped[y * rect.width + x]);

	device.unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);

#ifndef NDEBUG
	double readback_time = timer.end();
	LOGI("copy_vram_to_cpu_synchronous() took %.3f ms!\n",
			readback_time * 1e3);
#endif
}

BufferHandle Renderer::scanout_to_buffer(bool draw_area, unsigned &width, unsigned &height)
{
	auto &rect = draw_area ? render_state.draw_rect : render_state.display_mode;
	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
		return BufferHandle(nullptr);

	atlas.flush_render_pass();

	atlas.read_transfer(Domain::Scaled, rect);
	ensure_command_buffer();

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = scaling * scaling * rect.width * rect.height * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	auto buffer = device.create_buffer(buffer_create_info, nullptr);
	cmd->copy_image_to_buffer(*buffer, *scaled_framebuffer, 0, { int(scaling * rect.x), int(scaling * rect.y), 0 },
	                          { scaling * rect.width, scaling * rect.height, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	flush();
	device.wait_idle();
	width = scaling * rect.width;
	height = scaling * rect.height;
	return buffer;
}

void Renderer::mipmap_framebuffer()
{
	auto &rect = render_state.display_mode;
	unsigned levels = scaled_views.size();
	for (unsigned i = 1; i <= levels; i++)
	{
		RenderPassInfo rp;
		unsigned current_scale = max(scaling >> i, 1u);

		if (i == levels)
			rp.color_attachments[0] = &bias_framebuffer->get_view();
		else
			rp.color_attachments[0] = scaled_views[i].get();

		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
		rp.render_area = { { int(rect.x * current_scale), int(rect.y * current_scale) },
			               { rect.width * current_scale, rect.height * current_scale } };

		if (i == levels)
		{
			cmd->image_barrier(*bias_framebuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		}

		cmd->begin_render_pass(rp);

		if (i == levels)
			cmd->set_program(*pipelines.mipmap_energy_blur);
		else if (i == 1)
			cmd->set_program(*pipelines.mipmap_energy_first);
		else
			cmd->set_program(*pipelines.mipmap_energy);

		cmd->set_texture(0, 0, *scaled_views[i - 1], StockSampler::LinearClamp);

		cmd->set_quad_state();
		cmd->set_vertex_binding(0, *quad, 0, 2);
		struct Push
		{
			float offset[2];
			float range[2];
			float inv_resolution[2];
			float uv_min[2];
			float uv_max[2];
		};
		Push push = {
			{ float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
			{ float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
			{ 1.0f / (FB_WIDTH * current_scale), 1.0f / (FB_HEIGHT * current_scale) },
			{ (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
			{ (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		counters.draw_calls++;
		counters.vertices += 4;
		cmd->draw(4);

		cmd->end_render_pass();

		if (i == levels)
		{
			cmd->image_barrier(*bias_framebuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
		else
		{
			cmd->image_barrier(*scaled_framebuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
	}
}

Renderer::DisplayRect Renderer::compute_display_rect()
{
	unsigned clock_div;
	switch (render_state.width_mode)
	{
	case WidthMode::WIDTH_MODE_256:
		clock_div = 10;
		break;
	case WidthMode::WIDTH_MODE_320:
		clock_div = 8;
		break;
	case WidthMode::WIDTH_MODE_512:
		clock_div = 5;
		break;
	case WidthMode::WIDTH_MODE_640:
		clock_div = 4;
		break;
	case WidthMode::WIDTH_MODE_368:
		clock_div = 7;
		break;
	}

	unsigned display_width;
	int left_offset;
	if (render_state.crop_overscan)
	{
		// Horizontal crop amount is currently hardcoded. Future improvement could allow adjusting this.
		display_width = 2560/clock_div;
		left_offset = floor((render_state.horiz_start + render_state.offset_cycles - 608) / (double) clock_div);
	}
	else
	{
		display_width = 2800/clock_div;
		left_offset = floor((render_state.horiz_start + render_state.offset_cycles - 488) / (double) clock_div);
	}

	unsigned display_height;
	int upper_offset;
	if (render_state.is_pal)
	{
		display_height = render_state.slend_pal - render_state.slstart_pal + 1;
		upper_offset = render_state.vert_start - 20 - render_state.slstart_pal;
	}
	else
	{
		display_height = render_state.slend - render_state.slstart + 1;
		upper_offset = render_state.vert_start - 16 - render_state.slstart;
	}
	display_height *= (render_state.is_480i ? 2 : 1);
	upper_offset *= (render_state.is_480i ? 2 : 1);

	return DisplayRect(left_offset, upper_offset, display_width, display_height);
}

ImageHandle Renderer::scanout_to_texture()
{
	atlas.flush_render_pass();

	if (last_scanout)
		return last_scanout;

	auto &rect = render_state.display_mode;

	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
	{
		// Black screen, just flush out everything.
		atlas.read_fragment(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });

		ensure_command_buffer();

		auto info = ImageCreateInfo::render_target(64u, 64u, VK_FORMAT_R8G8B8A8_UNORM);

		if (!reuseable_scanout || reuseable_scanout->get_create_info().width != info.width ||
		    reuseable_scanout->get_create_info().height != info.height)
		{
			//LOG("Creating new scanout image.\n");
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			info.usage =
			    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			reuseable_scanout = device.create_image(info);
		}

		RenderPassInfo rp;
		rp.color_attachments[0] = &reuseable_scanout->get_view();
		rp.num_color_attachments = 1;
		rp.clear_attachments = 1;
		rp.store_attachments = 1;

		cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		cmd->begin_render_pass(rp);
		cmd->end_render_pass();

		cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                   VK_ACCESS_SHADER_READ_BIT);

		last_scanout = reuseable_scanout;
		return reuseable_scanout;
	}

	bool bpp24 = render_state.scanout_mode == ScanoutMode::BGR24;
	bool ssaa = render_state.scanout_filter == ScanoutFilter::SSAA && scaling != 1;

	if (bpp24 || ssaa)
	{
		auto tmp = rect;
		if (bpp24)
		{
			tmp.width = (tmp.width * 3 + 1) / 2;
			tmp.width = min(tmp.width, FB_WIDTH - tmp.x);
		}
		atlas.read_fragment(Domain::Unscaled, tmp);
	}
	else
		atlas.read_fragment(Domain::Scaled, rect);

	ensure_command_buffer();

	if (render_state.adaptive_smoothing && !bpp24 && !ssaa && scaling != 1)
		mipmap_framebuffer();

	if (scanout_semaphore)
	{
		flush();
		// We only need to wait in the scanout pass.
		device.add_wait_semaphore(CommandBuffer::Type::Generic, scanout_semaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);
		scanout_semaphore.reset();
	}

	ensure_command_buffer();

	bool scaled = !bpp24 && !ssaa;

	unsigned render_scale = scaled ? scaling : 1;

	auto display_rect = compute_display_rect();

	auto info = ImageCreateInfo::render_target(
			display_rect.width * render_scale,
			display_rect.height * render_scale,
			render_state.scanout_mode == ScanoutMode::ABGR1555_Dither ? VK_FORMAT_A1R5G5B5_UNORM_PACK16 : VK_FORMAT_R8G8B8A8_UNORM);

	if (!reuseable_scanout ||
			reuseable_scanout->get_create_info().width != info.width ||
			reuseable_scanout->get_create_info().height != info.height ||
			reuseable_scanout->get_create_info().format != info.format)
	{
		//LOG("Creating new scanout image.\n");
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		reuseable_scanout = device.create_image(info);
	}

	RenderPassInfo rp;
	rp.color_attachments[0] = &reuseable_scanout->get_view();
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	rp.clear_color[0] = {0, 0, 0, 0};
	//rp.clear_color[0] = {60.0f/256.0f, 230.0f/256.0f, 60.0f/256.0f, 0};
	rp.clear_attachments = 1;

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd->begin_render_pass(rp);
	cmd->set_quad_state();

	auto old_vp = cmd->get_viewport();
	VkViewport new_vp = {display_rect.x * (float) render_scale,
	                     display_rect.y * (float) render_scale,
	                     rect.width * (float) render_scale,
	                     rect.height * (float) render_scale,
	                     old_vp.minDepth,
	                     old_vp.maxDepth};

	cmd->set_viewport(new_vp);

	bool dither = render_state.scanout_mode == ScanoutMode::ABGR1555_Dither;

	if (bpp24)
	{
		if (render_state.scanout_filter == ScanoutFilter::MDEC_YUV)
			cmd->set_program(*pipelines.bpp24_yuv_quad_blitter);
		else
			cmd->set_program(*pipelines.bpp24_quad_blitter);
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	}
	else if (ssaa)
	{
		if (dither)
			cmd->set_program(*pipelines.unscaled_dither_quad_blitter);
		else
			cmd->set_program(*pipelines.unscaled_quad_blitter);

		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	}
	else if (!render_state.adaptive_smoothing || scaling == 1)
	{
		if (dither)
			cmd->set_program(*pipelines.scaled_dither_quad_blitter);
		else
			cmd->set_program(*pipelines.scaled_quad_blitter);

		cmd->set_texture(0, 0, *scaled_views[0], StockSampler::LinearClamp);
	}
	else
	{
		if (dither)
			cmd->set_program(*pipelines.mipmap_dither_resolve);
		else
			cmd->set_program(*pipelines.mipmap_resolve);

		cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::TrilinearClamp);
		cmd->set_texture(0, 1, bias_framebuffer->get_view(), StockSampler::LinearClamp);
	}

	if (dither)
	{
		cmd->set_texture(0, 2, dither_lut->get_view(), StockSampler::NearestWrap);
		struct DitherData
		{
			float range;
			float inv_range;
			float dither_scale;
			int32_t dither_shift;
		};
		auto *dither = cmd->allocate_typed_constant_data<DitherData>(0, 3, 1);
		dither->range = 31.0f;
		dither->inv_range = 1.0f / 31.0f;
		dither->dither_scale = 1.0f;

		if (render_state.dither_native_resolution && scaled)
		{
			int32_t shift = 0;
			unsigned tmp = scaling >> 1;
			while (tmp)
			{
				shift++;
				tmp >>= 1;
			}
			dither->dither_shift = shift;
		}
		else
		{
			dither->dither_shift = 0;
		}
	}

	cmd->set_vertex_binding(0, *quad, 0, 2);
	struct Push
	{
		float offset[2];
		float scale[2];
		float uv_min[2];
		float uv_max[2];
		float max_bias;
	};
	Push push = { { float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
		          { float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
		          { (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
		          { (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		          float(scaled_views.size() - 1) };

	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	counters.draw_calls++;
	counters.vertices += 4;
	cmd->draw(4);

	cmd->end_render_pass();

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_SHADER_READ_BIT);

	last_scanout = reuseable_scanout;

	return reuseable_scanout;
}

void Renderer::scanout()
{
	auto image = scanout_to_texture();

	ensure_command_buffer();
	cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
	cmd->set_quad_state();
	cmd->set_program(*pipelines.scaled_quad_blitter);
	cmd->set_texture(0, 0, image->get_view(), StockSampler::LinearClamp);

	cmd->set_vertex_binding(0, *quad, 0, 2);
	struct Push
	{
		float offset[2];
		float scale[2];
	};
	const Push push = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };

	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	counters.draw_calls++;
	counters.vertices += 4;
	cmd->draw(4);
	cmd->end_render_pass();
}

void Renderer::hazard(StatusFlags flags)
{
	VkPipelineStageFlags src_stages = 0;
	VkAccessFlags src_access = 0;
	VkPipelineStageFlags dst_stages = 0;
	VkAccessFlags dst_access = 0;

	if (flags & (STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (flags & (STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	}

	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (flags & (STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		src_access |= VK_ACCESS_SHADER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	if (flags & (STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (flags & (STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		src_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Invalidate render target caches.
	if (flags & (STATUS_TRANSFER_SFB_WRITE | STATUS_COMPUTE_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		              VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	// 24-bpp scanout hazard
	if (flags & STATUS_COMPUTE_FB_WRITE)
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT;
	}

	dst_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

	// If we have out-standing jobs in the compute pipe, issue them into cmdbuffer before injecting the barrier.
	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_READ | STATUS_COMPUTE_SFB_WRITE))
	{
		flush_blits();
		flush_resolves();
	}

	VK_ASSERT(src_stages);
	VK_ASSERT(dst_stages);
	ensure_command_buffer();
	cmd->barrier(src_stages, src_access, dst_stages, dst_access);
}

void Renderer::flush_resolves()
{
	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};

	if (!queue.scaled_resolves.empty())
	{
		ensure_command_buffer();
		cmd->set_program(*pipelines.resolve_to_scaled);

		cmd->set_storage_texture(0, 0, *scaled_views[0]);
		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
		if (msaa > 1)
			cmd->set_storage_texture(0, 2, scaled_framebuffer_msaa->get_view());

		unsigned size = queue.scaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min(size - i, 1024u);

			Push push = { { 1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT) }, scaling };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.scaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->dispatch(scaling, scaling, to_run);
		}
	}

	if (!queue.unscaled_resolves.empty())
	{
		ensure_command_buffer();
		cmd->set_program(*pipelines.resolve_to_unscaled);
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		cmd->set_texture(0, 1, *scaled_views[0], StockSampler::LinearClamp);

		unsigned size = queue.unscaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min(size - i, 1024u);

			Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.unscaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->dispatch(1, 1, to_run);
		}
	}

	queue.scaled_resolves.clear();
	queue.unscaled_resolves.clear();
}

void Renderer::resolve(Domain target_domain, unsigned x, unsigned y)
{
	if (target_domain == Domain::Scaled)
		queue.scaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
	else
		queue.unscaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
}

void Renderer::ensure_command_buffer()
{
	if (!cmd)
		cmd = device.request_command_buffer();
}

void Renderer::discard_render_pass()
{
	reset_queue();
}

float Renderer::allocate_depth(const Rect &rect)
{
	atlas.write_fragment(rect);
	primitive_index++;
	return 1.0f - primitive_index * (4.0f / 0xffffff); // Double the epsilon to be safe(r) when w is used.
	//iCB: Doubled again for added safety, otherwise we get Z-fighting when drawing multi-pass blended primitives.
}

void Renderer::build_attribs(BufferVertex *output, const Vertex *vertices, unsigned count)
{
	unsigned shift;
	switch (render_state.texture_mode)
	{
	case TextureMode::Palette4bpp:
		shift = 2;
		break;
	case TextureMode::Palette8bpp:
		shift = 1;
		break;
	default:
		shift = 0;
		break;
	}

	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.texture_window.mask_x == 0xffu && render_state.texture_window.mask_y == 0xffu)
		{
			unsigned min_u = render_state.UVLimits.min_u;
			unsigned min_v = render_state.UVLimits.min_v;
			unsigned max_u = render_state.UVLimits.max_u;
			unsigned max_v = render_state.UVLimits.max_v;
			unsigned width = max_u - min_u + 1;
			unsigned height = max_v - min_v + 1;

			if (max_u > 255 || max_v > 255) // Wraparound behavior, assume the whole page is hit.
				atlas.set_texture_window({ 0, 0, 256u >> shift, 256 });
			else
			{
				min_u >>= shift;
				max_u = (max_u + (1 << shift) - 1) >> shift;
				width = max_u - min_u + 1;
				atlas.set_texture_window({ min_u, min_v, width, height });
			}
		}
		else
		{
			// If we have a masked texture window, assume this is the true rect we should use.
			auto effective_rect = render_state.cached_window_rect;
			atlas.set_texture_window(
			    { effective_rect.x >> shift, effective_rect.y, effective_rect.width >> shift, effective_rect.height });
		}
	}

	// Compute bounding box for the draw call.
	float max_x = 0.0f;
	float max_y = 0.0f;
	float min_x = float(FB_WIDTH);
	float min_y = float(FB_HEIGHT);
	float x[4];
	float y[4];
	for (unsigned i = 0; i < count; i++)
	{
		float tmp_x = vertices[i].x + render_state.draw_offset_x;
		float tmp_y = vertices[i].y + render_state.draw_offset_y;
		max_x = max(max_x, tmp_x);
		max_y = max(max_y, tmp_y);
		min_x = min(min_x, tmp_x);
		min_y = min(min_y, tmp_y);
		x[i] = tmp_x;
		y[i] = tmp_y;
	}

	// Clamp the rect.
	min_x = floorf(max(min_x, 0.0f));
	min_y = floorf(max(min_y, 0.0f));
	max_x = ceilf(min(max_x, float(FB_WIDTH)));
	max_y = ceilf(min(max_y, float(FB_HEIGHT)));

	const Rect rect = {
		unsigned(min_x), unsigned(min_y), unsigned(max_x) - unsigned(min_x), unsigned(max_y) - unsigned(min_y),
	};

	float z = allocate_depth(rect);
	for (unsigned i = 0; i < count; i++)
	{
		output[i] = {
			x[i],
			y[i],
			z,
			vertices[i].w,
			vertices[i].color & 0xffffffu,
			render_state.texture_window,
			int16_t(render_state.palette_offset_x),
			int16_t(render_state.palette_offset_y),
#if 0
			int16_t(shift | (render_state.dither << 8)),
#else
			int16_t(shift),
#endif
			int16_t(vertices[i].u),
			int16_t(vertices[i].v),
			int16_t(render_state.texture_offset_x),
			int16_t(render_state.texture_offset_y),
			render_state.UVLimits.min_u,
			render_state.UVLimits.min_v,
			render_state.UVLimits.max_u,
			render_state.UVLimits.max_v,
		};

		if (render_state.texture_mode != TextureMode::None && !render_state.texture_color_modulate)
			output[i].color = 0x808080;

		output[i].color |= render_state.force_mask_bit ? 0xff000000u : 0u;
	}
}

std::vector<Renderer::BufferVertex> *Renderer::select_pipeline(unsigned prims, int scissor)
{
	// For mask testing, force primitives through the serialized blend path.
	if (render_state.mask_test)
		return nullptr;

	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.semi_transparent != SemiTransparentMode::None)
		{
			for (unsigned i = 0; i < prims; i++)
				queue.semi_transparent_opaque_scissor.emplace_back(queue.semi_transparent_opaque_scissor.size(),
				                                                   scissor);
			return &queue.semi_transparent_opaque;
		}
		else
		{
			for (unsigned i = 0; i < prims; i++)
				queue.opaque_textured_scissor.emplace_back(queue.opaque_textured_scissor.size(), scissor);
			return &queue.opaque_textured;
		}
	}
	else if (render_state.semi_transparent != SemiTransparentMode::None)
		return nullptr;
	else
	{
		for (unsigned i = 0; i < prims; i++)
			queue.opaque_scissor.emplace_back(queue.opaque_scissor.size(), scissor);
		return &queue.opaque;
	}
}

void Renderer::build_line_quad(Vertex *output, const Vertex *input)
{
	const float dx = input[1].x - input[0].x;
	const float dy = input[1].y - input[0].y;
	if (dx == 0.0f && dy == 0.0f)
	{
		// Degenerate, render a point.
		output[0].x = input[0].x;
		output[0].y = input[0].y;
		output[1].x = input[0].x + 1.0f;
		output[1].y = input[0].y;
		output[2].x = input[1].x;
		output[2].y = input[1].y + 1.0f;
		output[3].x = input[1].x + 1.0f;
		output[3].y = input[1].y + 1.0f;

		auto c = input[0].color;
		output[0].w = 1.0f;
		output[0].color = c;
		output[1].w = 1.0f;
		output[1].color = c;
		output[2].w = 1.0f;
		output[2].color = c;
		output[3].w = 1.0f;
		output[3].color = c;
		return;
	}

	const float abs_dx = fabsf(dx);
	const float abs_dy = fabsf(dy);
	float fill_dx, fill_dy;
	float dxdk, dydk;

	float pad_x0 = 0.0f;
	float pad_x1 = 0.0f;
	float pad_y0 = 0.0f;
	float pad_y1 = 0.0f;

	// Check for vertical or horizontal major lines.
	// When expanding to a rect, do so in the appropriate direction.
	// FIXME: This scheme seems to kinda work, but it seems very hard to find a method
	// that looks perfect on every game.
	// Vagrant Story speech bubbles are a very good test case here!
	if (abs_dx > abs_dy)
	{
		fill_dx = 0.0f;
		fill_dy = 1.0f;
		dxdk = 1.0f;
		dydk = dy / abs_dx;

		if (dx > 0.0f)
		{
			// Right
			pad_x1 = 1.0f;
			pad_y1 = dydk;
		}
		else
		{
			// Left
			pad_x0 = 1.0f;
			pad_y0 = -dydk;
		}
	}
	else
	{
		fill_dx = 1.0f;
		fill_dy = 0.0f;
		dydk = 1.0f;
		dxdk = dx / abs_dy;

		if (dy > 0.0f)
		{
			// Down
			pad_y1 = 1.0f;
			pad_x1 = dxdk;
		}
		else
		{
			// Up
			pad_y0 = 1.0f;
			pad_x0 = -dxdk;
		}
	}

	const float x0 = input[0].x + pad_x0;
	const float y0 = input[0].y + pad_y0;
	const float c0 = input[0].color;
	const float x1 = input[1].x + pad_x1;
	const float y1 = input[1].y + pad_y1;
	const float c1 = input[1].color;

	output[0].x = x0;
	output[0].y = y0;
	output[1].x = x0 + fill_dx;
	output[1].y = y0 + fill_dy;
	output[2].x = x1;
	output[2].y = y1;
	output[3].x = x1 + fill_dx;
	output[3].y = y1 + fill_dy;

	output[0].w = 1.0f;
	output[0].color = c0;
	output[1].w = 1.0f;
	output[1].color = c0;
	output[2].w = 1.0f;
	output[2].color = c1;
	output[3].w = 1.0f;
	output[3].color = c1;
}

void Renderer::draw_line(const Vertex *vertices)
{
	// We can move this to GPU, but means more draw calls and more pipeline swapping.
	// This should be plenty fast for the quite small amount of lines games render.
	Vertex vert[4];
	build_line_quad(vert, vertices);
	draw_quad(vert);
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	last_scanout.reset();
	counters.native_draw_calls++;

	BufferVertex vert[3];
	build_attribs(vert, vertices, 3);
	const int scissor_index = queue.scissor_invariant ? -1 : int(queue.scissors.size() - 1);
	auto *out = select_pipeline(1, scissor_index);
	if (out)
	{
		for (unsigned i = 0; i < 3; i++)
			out->push_back(vert[i]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		for (unsigned i = 0; i < 3; i++)
			queue.semi_transparent.push_back(vert[i]);
		queue.semi_transparent_state.push_back({ scissor_index, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		if (render_state.mask_test && render_state.semi_transparent != SemiTransparentMode::None)
			render_pass_is_feedback = true;
	}
}

void Renderer::draw_quad(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	last_scanout.reset();
	counters.native_draw_calls++;

	BufferVertex vert[4];
	build_attribs(vert, vertices, 4);
	const int scissor_index = queue.scissor_invariant ? -1 : int(queue.scissors.size() - 1);
	auto *out = select_pipeline(2, scissor_index);

	if (out)
	{
		out->push_back(vert[0]);
		out->push_back(vert[1]);
		out->push_back(vert[2]);
		out->push_back(vert[3]);
		out->push_back(vert[2]);
		out->push_back(vert[1]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		queue.semi_transparent.push_back(vert[0]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[3]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent_state.push_back({ scissor_index, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });
		queue.semi_transparent_state.push_back({ scissor_index, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test });

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		if (render_state.mask_test && render_state.semi_transparent != SemiTransparentMode::None)
			render_pass_is_feedback = true;
	}
}

void Renderer::clear_quad(const Rect &rect, FBColor color, bool candidate)
{
	last_scanout.reset();
	auto old = atlas.set_texture_mode(TextureMode::None);
	float z = allocate_depth(rect);
	atlas.set_texture_mode(old);

	BufferVertex pos0 = { float(rect.x), float(rect.y), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos1 = { float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos2 = { float(rect.x), float(rect.y) + float(rect.height), z, 1.0f, fbcolor_to_rgba8(color) };
	BufferVertex pos3 = { float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f,
		                  fbcolor_to_rgba8(color) };
	queue.opaque.push_back(pos0);
	queue.opaque.push_back(pos1);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos3);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos1);
	queue.opaque_scissor.emplace_back(queue.opaque_scissor.size(), -1);
	queue.opaque_scissor.emplace_back(queue.opaque_scissor.size(), -1);

	if (candidate)
		queue.clear_candidates.push_back({ rect, color, z });
}

const Renderer::ClearCandidate *Renderer::find_clear_candidate(const Rect &rect) const
{
	const ClearCandidate *ret = nullptr;
	for (auto &c : queue.clear_candidates)
	{
		if (c.rect == rect)
			ret = &c;
	}
	return ret;
}

void Renderer::flush_render_pass(const Rect &rect)
{
	ensure_command_buffer();

	RenderPassInfo info = {};
	info.clear_depth_stencil = { 1.0f, 0 };
	info.color_attachments[0] = scaled_views.front().get();
	info.depth_stencil =
		&device.get_transient_attachment(FB_WIDTH * scaling, FB_HEIGHT * scaling,
		                                 device.get_default_depth_format(), 0, msaa, 1);
	info.num_color_attachments = 1;
	info.store_attachments = 1 << 0;
	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;

	if (msaa > 1)
	{
		info.num_color_attachments = 2;
		info.color_attachments[1] = info.color_attachments[0];
		info.color_attachments[0] = &scaled_framebuffer_msaa->get_view();
		info.store_attachments |= 1 << 1;
	}

	RenderPassInfo::Subpass subpass;
	info.num_subpasses = 1;
	info.subpasses = &subpass;
	subpass.num_color_attachments = 1;

	auto *clear_candidate = find_clear_candidate(rect);

	subpass.color_attachments[0] = 0;
	if (render_pass_is_feedback)
	{
		subpass.num_input_attachments = 1;
		subpass.input_attachments[0] = 0;
	}

	if (msaa > 1)
	{
		subpass.resolve_attachments[0] = 1;
		subpass.num_resolve_attachments = 1;
	}

	if (clear_candidate)
	{
		info.clear_depth_stencil.depth = clear_candidate->z;
		fbcolor_to_rgba32f(info.clear_color[0].float32, clear_candidate->color);
		info.clear_attachments = 1 << 0;
	}
	else
	{
		info.load_attachments = 1 << 0;
		counters.fragment_readback_pixels += rect.width * rect.height * scaling * scaling;
	}

	counters.fragment_writeout_pixels += rect.width * rect.height * scaling * scaling;

	info.render_area.offset = { int(rect.x * scaling), int(rect.y * scaling) };
	info.render_area.extent = { rect.width * scaling, rect.height * scaling };

	counters.render_passes++;
	cmd->begin_render_pass(info);
	cmd->set_scissor(info.render_area);
	queue.default_scissor = info.render_area;
	cmd->set_texture(0, 2, dither_lut->get_view(), StockSampler::NearestWrap);

	render_opaque_primitives();
	render_opaque_texture_primitives();
	render_semi_transparent_opaque_texture_primitives();
	render_semi_transparent_primitives();

	cmd->end_render_pass();

	// Render passes are implicitly synchronized.
	cmd->image_barrier(*scaled_framebuffer,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);

	reset_queue();
}

void Renderer::dispatch(const vector<BufferVertex> &vertices, vector<pair<unsigned, int>> &scissors)
{
	sort(begin(scissors), end(scissors), [](const pair<unsigned, int> &a, const pair<unsigned, int> &b) {
		if (a.second != b.second)
			return a.second > b.second;
		return a.first > b.first;
	});

	// Render flat-shaded primitives.
	auto *vert = static_cast<BufferVertex *>(
	    cmd->allocate_vertex_data(0, vertices.size() * sizeof(BufferVertex), sizeof(BufferVertex)));

	int scissor = scissors.front().second;
	unsigned last_draw = 0;
	unsigned i = 1;
	unsigned size = scissors.size();

	cmd->set_scissor(scissor < 0 ? queue.default_scissor : queue.scissors[scissor]);
	memcpy(vert, vertices.data() + 3 * scissors.front().first, 3 * sizeof(BufferVertex));
	vert += 3;

	for (; i < size; i++, vert += 3)
	{
		if (scissors[i].second != scissor)
		{
			unsigned to_draw = i - last_draw;
			cmd->draw(3 * to_draw, 1, 3 * last_draw, 0);
			counters.draw_calls++;
			last_draw = i;

			scissor = scissors[i].second;
			cmd->set_scissor(scissor < 0 ? queue.default_scissor : queue.scissors[scissor]);
		}
		memcpy(vert, vertices.data() + 3 * scissors[i].first, 3 * sizeof(BufferVertex));
	}

	unsigned to_draw = size - last_draw;
	cmd->draw(3 * to_draw, 1, 3 * last_draw, 0);
	counters.draw_calls++;
	counters.vertices += vertices.size();
}

void Renderer::render_opaque_primitives()
{
	auto &vertices = queue.opaque;
	auto &scissors = queue.opaque_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_program(*pipelines.opaque_flat);

	dispatch(vertices, scissors);
}

void Renderer::render_semi_transparent_primitives()
{
	unsigned prims = queue.semi_transparent_state.size();
	if (!prims)
		return;

	unsigned last_draw_offset = 0;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_depth_test(true, false);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));
	cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);

	auto size = queue.semi_transparent.size() * sizeof(BufferVertex);
	void *verts = cmd->allocate_vertex_data(0, size, sizeof(BufferVertex));
	memcpy(verts, queue.semi_transparent.data(), size);

	auto last_state = queue.semi_transparent_state[0];

	const auto set_state = [&](const SemiTransparentState &state) {
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestWrap);
		
		if (state.scissor_index < 0)
			cmd->set_scissor(queue.default_scissor);
		else
			cmd->set_scissor(queue.scissors[state.scissor_index]);

		switch (state.semi_transparent)
		{
		case SemiTransparentMode::None:
		{
			// For opaque primitives which are just masked, we can make use of fixed function blending.
			cmd->set_blend_enable(true);
			cmd->set_program(state.textured ? *pipelines.opaque_textured : *pipelines.opaque_flat);
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
			                       VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
			break;
		}
		case SemiTransparentMode::Add:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent_masked_add : *pipelines.flat_masked_add);
				cmd->pixel_barrier();
				cmd->set_input_attachments(0, 3);
				cmd->set_blend_enable(false);
				if (msaa > 1)
				{
					// Need to blend per-sample.
					cmd->set_multisample_state(false, false, true);
				}
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::Average:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent_masked_average :
				                                  *pipelines.flat_masked_average);
				cmd->set_input_attachments(0, 3);
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				if (msaa > 1)
				{
					// Need to blend per-sample.
					cmd->set_multisample_state(false, false, true);
				}
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				static const float rgba[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_constants(rgba);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::Sub:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent_masked_sub : *pipelines.flat_masked_sub);
				cmd->set_input_attachments(0, 3);
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				if (msaa > 1)
				{
					// Need to blend per-sample.
					cmd->set_multisample_state(false, false, true);
				}
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_op(VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		case SemiTransparentMode::AddQuarter:
		{
			if (state.masked)
			{
				cmd->set_program(state.textured ? *pipelines.semi_transparent_masked_add_quarter :
				                                  *pipelines.flat_masked_add_quarter);
				cmd->set_input_attachments(0, 3);
				cmd->pixel_barrier();
				cmd->set_blend_enable(false);
				if (msaa > 1)
				{
					// Need to blend per-sample.
					cmd->set_multisample_state(false, false, true);
				}
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ONE);
			}
			else
			{
				static const float rgba[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
				cmd->set_program(state.textured ? *pipelines.semi_transparent : *pipelines.opaque_flat);
				cmd->set_blend_enable(true);
				cmd->set_blend_constants(rgba);
				cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
				cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
				                       VK_BLEND_FACTOR_ZERO);
			}
			break;
		}
		}
	};
	set_state(last_state);

	// These pixels are blended, so we have to render them in-order.
	// Batch up as long as we can.
	for (unsigned i = 1; i < prims; i++)
	{
		// If we need programmable shading, we can't batch as primitives may overlap.
		// We could in theory do some fancy tests here, but probably overkill here.
		if ((last_state.masked && last_state.semi_transparent != SemiTransparentMode::None) ||
		    (last_state != queue.semi_transparent_state[i]))
		{
			unsigned to_draw = i - last_draw_offset;
			counters.draw_calls++;
			counters.vertices += to_draw * 3;
			cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
			if (msaa > 1)
				cmd->set_multisample_state(false);
			last_draw_offset = i;

			last_state = queue.semi_transparent_state[i];
			set_state(last_state);
		}
	}

	unsigned to_draw = prims - last_draw_offset;
	counters.draw_calls++;
	counters.vertices += to_draw * 3;
	cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
	if (msaa > 1)
		cmd->set_multisample_state(false);
}

void Renderer::render_semi_transparent_opaque_texture_primitives()
{
	auto &vertices = queue.semi_transparent_opaque;
	auto &scissors = queue.semi_transparent_opaque_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_program(*pipelines.opaque_semi_transparent);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));
	cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);

	dispatch(vertices, scissors);
}

void Renderer::render_opaque_texture_primitives()
{
	auto &vertices = queue.opaque_textured;
	auto &scissors = queue.opaque_textured_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_program(*pipelines.opaque_textured);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x)); // Pad to support AMD
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));
	cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);

	dispatch(vertices, scissors);
}

void Renderer::flush_blits()
{
	ensure_command_buffer();
	const auto blit = [&](const std::vector<BlitInfo> &infos, Program &program, bool scaled) {
		if (infos.empty())
			return;

		cmd->set_program(program);

		if (scaled)
		{
			cmd->set_storage_texture(0, 0, *scaled_views[0]);
			cmd->set_texture(0, 1, *scaled_views[0], StockSampler::NearestClamp);
			if (msaa > 1)
			{
				cmd->set_storage_texture(0, 2, scaled_framebuffer_msaa->get_view());
				cmd->set_texture(0, 3, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
			}
		}
		else
		{
			cmd->set_storage_texture(0, 0, framebuffer->get_view());
			cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
		}

		unsigned size = infos.size();
		unsigned scale = scaled ? scaling : 1u;
		for (unsigned i = 0; i < size; i += 512)
		{
			unsigned to_blit = min(size - i, 512u);
			void *ptr = cmd->allocate_constant_data(1, 0, to_blit * sizeof(BlitInfo));
			memcpy(ptr, infos.data() + i, to_blit * sizeof(BlitInfo));
			cmd->dispatch(scale, scale, to_blit);
		}
	};

	blit(queue.scaled_blits, *pipelines.blit_vram_scaled, true);
	blit(queue.scaled_masked_blits, *pipelines.blit_vram_scaled_masked, true);
	blit(queue.unscaled_blits, *pipelines.blit_vram_unscaled, false);
	blit(queue.unscaled_masked_blits, *pipelines.blit_vram_unscaled_masked, false);
	queue.scaled_blits.clear();
	queue.scaled_masked_blits.clear();
	queue.unscaled_blits.clear();
	queue.unscaled_masked_blits.clear();
}

void Renderer::blit_vram(const Rect &dst, const Rect &src)
{
	VK_ASSERT(dst.width == src.width);
	VK_ASSERT(dst.height == src.height);

	// Happens a lot in Square games for some reason.
	if (dst == src)
		return;

	if (dst.width == 0 || dst.height == 0)
		return;

	last_scanout.reset();
	auto domain = atlas.blit_vram(dst, src);

	if (dst.intersects(src))
	{
		// The software implementation takes texture cache into account by copying 128 horizontal pixels at a time ...
		// We can do it with compute.
		ensure_command_buffer();

		unsigned factor = domain == Domain::Scaled ? scaling : 1u;

		// Slower path where we do this in a single workgroup which steps through line by line, just like the software version.
		struct Push
		{
			uint32_t src_offset[2];
			uint32_t dst_offset[2];
			uint32_t extent[2];
			int32_t scaling;
		};
		Push push = {
			{ src.x, src.y }, { dst.x, dst.y }, { dst.width, dst.height }, int(factor),
		};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->set_program(domain == Domain::Scaled ?
		                     (render_state.mask_test ? *pipelines.blit_vram_cached_scaled_masked :
		                                               *pipelines.blit_vram_cached_scaled) :
		                     (render_state.mask_test ? *pipelines.blit_vram_cached_unscaled_masked :
		                                               *pipelines.blit_vram_cached_unscaled));

		cmd->set_storage_texture(0, 0, domain == Domain::Scaled ? *scaled_views[0] : framebuffer->get_view());
		cmd->dispatch(factor, factor, 1);

		if (msaa > 1 && domain == Domain::Scaled)
		{
			cmd->set_storage_texture(0, 0, scaled_framebuffer_msaa->get_view());
			cmd->set_program(render_state.mask_test ?
					*pipelines.blit_vram_msaa_cached_scaled_masked :
					*pipelines.blit_vram_msaa_cached_scaled);
			cmd->dispatch(factor, factor, msaa);
		}
		//LOG("Intersecting blit_vram, hitting slow path (src: %u, %u, dst: %u, %u, size: %u, %u)\n", src.x, src.y, dst.x,
		//    dst.y, dst.width, dst.height);
	}
	else
	{
		if (domain == Domain::Scaled)
		{
			auto &q = render_state.mask_test ? queue.scaled_masked_blits : queue.scaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					for (unsigned s = 0; s < msaa; s++)
						q.push_back({
							{ (x + src.x) * scaling, (y + src.y) * scaling },
							{ (x + dst.x) * scaling, (y + dst.y) * scaling },
							{ min(BLOCK_WIDTH, width - x) * scaling, min(BLOCK_HEIGHT, height - y) * scaling },
							render_state.force_mask_bit ? 0x8000u : 0u, s,
						});
		}
		else
		{
			auto &q = render_state.mask_test ? queue.unscaled_masked_blits : queue.unscaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					q.push_back({
					    { x + src.x, y + src.y },
					    { x + dst.x, y + dst.y },
					    { min(BLOCK_WIDTH, width - x), min(BLOCK_HEIGHT, height - y) },
						render_state.force_mask_bit ? 0x8000u : 0u, 0,
					});
		}
	}
}

uint16_t *Renderer::begin_copy(BufferHandle handle)
{
	return static_cast<uint16_t *>(device.map_host_buffer(*handle, MEMORY_ACCESS_WRITE_BIT));
}

void Renderer::end_copy(BufferHandle handle)
{
	device.unmap_host_buffer(*handle, MEMORY_ACCESS_WRITE_BIT);
}

BufferHandle Renderer::copy_cpu_to_vram(const Rect &rect)
{
	last_scanout.reset();
	atlas.write_compute(Domain::Unscaled, rect);
	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::Host;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	auto buffer = device.create_buffer(buffer_create_info, nullptr);

	BufferViewCreateInfo view_info = {};
	view_info.buffer = buffer.get();

	struct Push
	{
		Rect rect;
		uint32_t offset;
		uint32_t mask_or;
	};

	ensure_command_buffer();
	cmd->set_program(render_state.mask_test ? *pipelines.copy_to_vram_masked : *pipelines.copy_to_vram);
	cmd->set_storage_texture(0, 0, framebuffer->get_view());

	// Vulkan minimum limit, for large buffer views, split up the work.
	if (rect.width * rect.height > device.get_gpu_properties().limits.maxTexelBufferElements)
	{
		for (unsigned y = 0; y < rect.height; y += BLOCK_HEIGHT)
		{
			unsigned y_size = min(rect.height - y, BLOCK_HEIGHT);
			view_info.offset = y * rect.width * sizeof(uint16_t);
			view_info.range = y_size * rect.width * sizeof(uint16_t);
			view_info.format = VK_FORMAT_R16_UINT;
			auto view = device.create_buffer_view(view_info);

			Rect small_rect = { rect.x, rect.y + y, rect.width, y_size };

			cmd->set_buffer_view(0, 1, *view);
			Push push = { small_rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
			cmd->push_constants(&push, 0, sizeof(push));
			cmd->dispatch((small_rect.width + 7) >> 3, (small_rect.height + 7) >> 3, 1);
		}
	}
	else
	{
		view_info.offset = 0;
		view_info.range = size;
		view_info.format = VK_FORMAT_R16_UINT;
		auto view = device.create_buffer_view(view_info);

		cmd->set_buffer_view(0, 1, *view);

		Push push = { rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
		cmd->push_constants(&push, 0, sizeof(push));

		// TODO: Batch up work.
		cmd->dispatch((rect.width + 7) >> 3, (rect.height + 7) >> 3, 1);
	}

	return buffer;
}

Renderer::~Renderer()
{
	flush();
}

void Renderer::reset_scissor_queue()
{
	queue.scissors.clear();
	auto &rect = render_state.draw_rect;
	queue.scissors.push_back(
	    { { int(rect.x * scaling), int(rect.y * scaling) }, { rect.width * scaling, rect.height * scaling } });
}

void Renderer::reset_queue()
{
	queue.opaque.clear();
	queue.opaque_scissor.clear();
	queue.opaque_textured.clear();
	queue.opaque_textured_scissor.clear();
	queue.textures.clear();
	queue.semi_transparent.clear();
	queue.semi_transparent_state.clear();
	queue.semi_transparent_opaque.clear();
	queue.semi_transparent_opaque_scissor.clear();
	queue.clear_candidates.clear();
	primitive_index = 0;
	render_pass_is_feedback = false;

	reset_scissor_queue();
}
}
