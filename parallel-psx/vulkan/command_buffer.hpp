/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "vulkan_common.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan.hpp"
#include "buffer_pool.hpp"
#include <string.h>

namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
	COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

	COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
	COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,

	COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,

	COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7,

	COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT
};
using CommandBufferDirtyFlags = uint32_t;

#define COMPARE_OP_BITS 3
#define BLEND_FACTOR_BITS 5
#define BLEND_OP_BITS 3
#define CULL_MODE_BITS 2
union PipelineState {
	struct State
	{
		// Depth state.
		unsigned depth_write : 1;
		unsigned depth_test : 1;
		unsigned blend_enable : 1;

		unsigned cull_mode : CULL_MODE_BITS;

		unsigned depth_compare : COMPARE_OP_BITS;

		unsigned alpha_to_coverage : 1;
		unsigned alpha_to_one : 1;
		unsigned sample_shading : 1;

		unsigned src_color_blend : BLEND_FACTOR_BITS;
		unsigned dst_color_blend : BLEND_FACTOR_BITS;
		unsigned color_blend_op : BLEND_OP_BITS;
		unsigned src_alpha_blend : BLEND_FACTOR_BITS;
		unsigned dst_alpha_blend : BLEND_FACTOR_BITS;
		unsigned alpha_blend_op : BLEND_OP_BITS;
		unsigned topology : 4;

		unsigned spec_constant_mask : 8;
	} state;
	uint32_t words[4];
};

struct PotentialState
{
	float blend_constants[4];
	uint32_t spec_constants[VULKAN_NUM_SPEC_CONSTANTS];
};

struct VertexAttribState
{
	uint32_t binding;
	VkFormat format;
	uint32_t offset;
};

struct IndexState
{
	VkBuffer buffer;
	VkDeviceSize offset;
	VkIndexType index_type;
};

struct VertexBindingState
{
	VkBuffer buffers[VULKAN_NUM_VERTEX_BUFFERS];
	VkDeviceSize offsets[VULKAN_NUM_VERTEX_BUFFERS];
	VkDeviceSize strides[VULKAN_NUM_VERTEX_BUFFERS];
	VkVertexInputRate input_rates[VULKAN_NUM_VERTEX_BUFFERS];
};

struct ResourceBinding
{
	union {
		VkDescriptorBufferInfo buffer;
		struct
		{
			VkDescriptorImageInfo fp;
			VkDescriptorImageInfo integer;
		} image;
		VkBufferView buffer_view;
	};
};

struct ResourceBindings
{
	ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];
};

static_assert(VULKAN_NUM_DESCRIPTOR_SETS == 4, "Number of descriptor sets != 4.");

class CommandBuffer;
struct CommandBufferDeleter
{
	void operator()(CommandBuffer *cmd);
};

class Device;
class CommandBuffer : public Util::IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter>
{
public:
	friend struct CommandBufferDeleter;
	enum class Type
	{
		Generic,
		AsyncGraphics,
		AsyncCompute,
		AsyncTransfer,
		Count
	};

	~CommandBuffer();
	VkCommandBuffer get_command_buffer() const
	{
		return cmd;
	}

	void begin_region(const char *name, const float *color = nullptr);
	void end_region();

	Device &get_device()
	{
		return *device;
	}

	void clear_image(const Image &image, const VkClearValue &value);

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
	void copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits);

	void copy_image_to_buffer(const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset,
	                          const VkExtent3D &extent, unsigned row_length, unsigned slice_height,
	                          const VkImageSubresourceLayers &subresrouce);

	void full_barrier();
	void pixel_barrier();
	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access);

	void barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
	             unsigned barriers, const VkMemoryBarrier *globals,
	             unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers,
	             unsigned image_barriers, const VkImageMemoryBarrier *images);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);

	void blit_image(const Image &dst,
	                const Image &src,
	                const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR);

	// Prepares an image to have its mipmap generated.
	// Puts the top-level into TRANSFER_SRC_OPTIMAL, and all other levels are invalidated with an UNDEFINED -> TRANSFER_DST_OPTIMAL.
	void barrier_prepare_generate_mipmap(const Image &image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                                     bool need_top_level_barrier = true);

	// The image must have been transitioned with barrier_prepare_generate_mipmap before calling this function.
	// After calling this function, the image will be entirely in TRANSFER_SRC_OPTIMAL layout.
	// Wait for TRANSFER stage to drain before transitioning away from TRANSFER_SRC_OPTIMAL.
	void generate_mipmap(const Image &image);

	void begin_render_pass(const RenderPassInfo &info, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
	void end_render_pass();
	void set_program(Program &program);


	void set_buffer_view(unsigned set, unsigned binding, const BufferView &view);
	void set_input_attachments(unsigned set, unsigned start_binding);
	void set_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler);
	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range);

	void *allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size);

	template <typename T>
	T *allocate_typed_constant_data(unsigned set, unsigned binding, unsigned count)
	{
		return static_cast<T *>(allocate_constant_data(set, binding, count * sizeof(T)));
	}

	void *allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
	                           VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);

	void set_viewport(const VkViewport &viewport);
	const VkViewport &get_viewport() const;
	void set_scissor(const VkRect2D &rect);

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void set_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type);

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
	          uint32_t first_instance = 0);

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

	void set_opaque_state();
	void set_quad_state();

#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (static_state.state.value != value)                \
		{                                                     \
			static_state.state.value = value;                 \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                   \
	do                                                        \
	{                                                         \
		if (potential_static_state.value != value)            \
		{                                                     \
			potential_static_state.value = value;             \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

	inline void set_depth_test(bool depth_test, bool depth_write)
	{
		SET_STATIC_STATE(depth_test);
		SET_STATIC_STATE(depth_write);
	}

	inline void set_depth_compare(VkCompareOp depth_compare)
	{
		SET_STATIC_STATE(depth_compare);
	}

	inline void set_blend_enable(bool blend_enable)
	{
		SET_STATIC_STATE(blend_enable);
	}

	inline void set_blend_factors(VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend,
	                              VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
	{
		SET_STATIC_STATE(src_color_blend);
		SET_STATIC_STATE(dst_color_blend);
		SET_STATIC_STATE(src_alpha_blend);
		SET_STATIC_STATE(dst_alpha_blend);
	}

	inline void set_blend_factors(VkBlendFactor src_blend, VkBlendFactor dst_blend)
	{
		set_blend_factors(src_blend, src_blend, dst_blend, dst_blend);
	}

	inline void set_blend_op(VkBlendOp color_blend_op, VkBlendOp alpha_blend_op)
	{
		SET_STATIC_STATE(color_blend_op);
		SET_STATIC_STATE(alpha_blend_op);
	}

	inline void set_blend_op(VkBlendOp blend_op)
	{
		set_blend_op(blend_op, blend_op);
	}

	inline void set_primitive_topology(VkPrimitiveTopology topology)
	{
		SET_STATIC_STATE(topology);
	}

	inline void set_multisample_state(bool alpha_to_coverage, bool alpha_to_one = false, bool sample_shading = false)
	{
		SET_STATIC_STATE(alpha_to_coverage);
		SET_STATIC_STATE(alpha_to_one);
		SET_STATIC_STATE(sample_shading);
	}

	inline void set_cull_mode(VkCullModeFlags cull_mode)
	{
		SET_STATIC_STATE(cull_mode);
	}

	inline void set_blend_constants(const float blend_constants[4])
	{
		SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
	}

	inline void set_specialization_constant_mask(uint32_t spec_constant_mask)
	{
		VK_ASSERT((spec_constant_mask & ~((1u << VULKAN_NUM_SPEC_CONSTANTS) - 1u)) == 0u);
		SET_STATIC_STATE(spec_constant_mask);
	}

	template <typename T>
	inline void set_specialization_constant(unsigned index, const T &value)
	{
		VK_ASSERT(index < VULKAN_NUM_SPEC_CONSTANTS);
		static_assert(sizeof(value) == sizeof(uint32_t), "Spec constant data must be 32-bit.");
		if (memcmp(&potential_static_state.spec_constants[index], &value, sizeof(value)))
		{
			memcpy(&potential_static_state.spec_constants[index], &value, sizeof(value));
			if (static_state.state.spec_constant_mask & (1u << index))
				set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
		}
	}

	inline Type get_command_buffer_type() const
	{
		return type;
	}

	void end();

private:
	friend class Util::ObjectPool<CommandBuffer>;
	CommandBuffer(Device *device, VkCommandBuffer cmd, Type type);

	Device *device;
	VkCommandBuffer cmd;
	Type type;

	const Framebuffer *framebuffer = nullptr;
	const RenderPass *actual_render_pass = nullptr;
	const RenderPass *compatible_render_pass = nullptr;

	VertexAttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS] = {};
	IndexState index = {};
	VertexBindingState vbo = {};
	ResourceBindings bindings;

	VkPipeline current_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout current_pipeline_layout = VK_NULL_HANDLE;
	PipelineLayout *current_layout = nullptr;
	Program *current_program = nullptr;
	unsigned current_subpass = 0;
	VkSubpassContents current_contents = VK_SUBPASS_CONTENTS_INLINE;

	VkViewport viewport = {};
	VkRect2D scissor = {};

	CommandBufferDirtyFlags dirty = ~0u;
	uint32_t dirty_sets = 0;
	uint32_t dirty_vbos = 0;
	uint32_t active_vbos = 0;
	bool is_compute = true;

	void set_dirty(CommandBufferDirtyFlags flags)
	{
		dirty |= flags;
	}

	CommandBufferDirtyFlags get_and_clear(CommandBufferDirtyFlags flags)
	{
		CommandBufferDirtyFlags mask = dirty & flags;
		dirty &= ~flags;
		return mask;
	}

	PipelineState static_state;
	PotentialState potential_static_state = {};
#ifndef _MSC_VER
	static_assert(sizeof(static_state.words) >= sizeof(static_state.state),
	              "Hashable pipeline state is not large enough!");
#endif

	void flush_render_state();
	VkPipeline build_graphics_pipeline(Util::Hash hash);
	VkPipeline build_compute_pipeline(Util::Hash hash);
	void flush_graphics_pipeline();
	void flush_compute_pipeline();
	void flush_descriptor_sets();
	void begin_graphics();
	void flush_descriptor_set(uint32_t set);
	void begin_compute();
	void begin_context();

	void flush_compute_state();

	BufferBlock vbo_block;
	BufferBlock ubo_block;

	void set_texture(unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view,
	                 VkImageLayout layout,
	                 uint64_t cookie);

	void init_viewport_scissor(const RenderPassInfo &info, const Framebuffer *framebuffer);
};


using CommandBufferHandle = Util::IntrusivePtr<CommandBuffer>;
}
