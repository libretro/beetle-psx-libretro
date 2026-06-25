#ifndef RHI_SPIRV_REFLECT_H
#define RHI_SPIRV_REFLECT_H

/*
 * C-compatible SPIR-V reflection shim interface.
 *
 * The Vulkan RHI translation unit is being moved to plain C, but SPIR-V
 * reflection is done with SPIRV-Cross, which is C++. To keep the C++ dependency
 * out of the main file, the reflection is implemented in a separate C++ shim
 * (rhi_spirv_reflect.cpp) and exposed through this extern "C" interface over
 * POD structs. The struct field layout here mirrors the Vulkan::ResourceLayout /
 * Vulkan::DescriptorSetLayout used by the RHI, so the result can be copied
 * across the boundary field-for-field.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RHI_SPIRV_NUM_DESCRIPTOR_SETS 4
#define RHI_SPIRV_NUM_SPEC_CONSTANTS  8

/* Mirror of Vulkan::DescriptorSetLayout (all POD bitmask fields). */
typedef struct RhiSpirvDescriptorSetLayout
{
   uint32_t sampled_image_mask;
   uint32_t storage_image_mask;
   uint32_t uniform_buffer_mask;
   uint32_t storage_buffer_mask;
   uint32_t sampled_buffer_mask;
   uint32_t input_attachment_mask;
   uint32_t sampler_mask;
   uint32_t separate_image_mask;
   uint32_t fp_mask;
   uint32_t immutable_sampler_mask;
   uint64_t immutable_samplers;
} RhiSpirvDescriptorSetLayout;

/* Mirror of Vulkan::ResourceLayout. */
typedef struct RhiSpirvResourceLayout
{
   uint32_t input_mask;
   uint32_t output_mask;
   uint32_t push_constant_size;
   uint32_t spec_constant_mask;
   RhiSpirvDescriptorSetLayout sets[RHI_SPIRV_NUM_DESCRIPTOR_SETS];
} RhiSpirvResourceLayout;

/*
 * Reflect a SPIR-V module into the POD resource layout. `data` points to
 * `word_count` 32-bit SPIR-V words. `out` is fully overwritten (the caller need
 * not pre-zero it). Immutable-sampler bindings are resolved from resource names
 * via the stock-sampler naming convention; the encoded values match
 * Vulkan::StockSampler's enumerator order.
 */
void rhi_spirv_reflect(const uint32_t *data, size_t word_count,
      RhiSpirvResourceLayout *out);

#ifdef __cplusplus
}
#endif

#endif /* RHI_SPIRV_REFLECT_H */
