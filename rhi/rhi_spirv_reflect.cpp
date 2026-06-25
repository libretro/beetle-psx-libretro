/*
 * SPIR-V reflection shim (C++).
 *
 * Implements rhi_spirv_reflect() from rhi_spirv_reflect.h using SPIRV-Cross.
 * This is the only place the Vulkan RHI uses SPIRV-Cross (and, with it, C++ STL);
 * keeping it here lets the main rhi_lib_vulkan translation unit be plain C. The
 * reflection logic is a direct port of what previously lived inline in
 * Vulkan::Shader::Shader.
 */

#include "rhi_spirv_reflect.h"

#include <string.h>
#include <libretro.h>
#include <spirv_cross.hpp>

/* The main RHI translation unit owns this global (set from retro_set_environment).
 * Declare it here so the shim's diagnostics route through the same logger. */
extern retro_log_printf_t libretro_log;
#define LOGE(...) do { if (libretro_log) libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); } while (0)

using spirv_cross::Compiler;
using spirv_cross::Resource;
using spirv_cross::ShaderResources;
using spirv_cross::SpecializationConstant;
using spirv_cross::SPIRType;

/* Spec-constant slot count check (mirrors VULKAN_NUM_SPEC_CONSTANTS). */
#define RHI_SPIRV_VK_NUM_SPEC_CONSTANTS RHI_SPIRV_NUM_SPEC_CONSTANTS

namespace
{
   /* Stock-sampler ids, matching Vulkan::StockSampler's enumerator order so the
    * encoded value can be used directly on the RHI side. */
   enum StockSamplerId
   {
      NearestClamp = 0,
      LinearClamp,
      TrilinearClamp,
      NearestWrap,
      LinearWrap,
      TrilinearWrap,
      NearestShadow,
      LinearShadow,
      StockSamplerCount
   };

   /* Resolve a stock sampler from a resource name. Returns true and writes the
    * id on a match. Same name table as the previous inline get_stock_sampler. */
   bool get_stock_sampler(unsigned *sampler, const char *name)
   {
      if (strstr(name, "NearestClamp"))
         *sampler = NearestClamp;
      else if (strstr(name, "LinearClamp"))
         *sampler = LinearClamp;
      else if (strstr(name, "TrilinearClamp"))
         *sampler = TrilinearClamp;
      else if (strstr(name, "NearestWrap"))
         *sampler = NearestWrap;
      else if (strstr(name, "LinearWrap"))
         *sampler = LinearWrap;
      else if (strstr(name, "TrilinearWrap"))
         *sampler = TrilinearWrap;
      else if (strstr(name, "NearestShadow"))
         *sampler = NearestShadow;
      else if (strstr(name, "LinearShadow"))
         *sampler = LinearShadow;
      else
         return false;
      return true;
   }

   inline bool has_immutable_sampler(const RhiSpirvDescriptorSetLayout *set, unsigned binding)
   {
      return (set->immutable_sampler_mask & (1u << binding)) != 0;
   }

   inline unsigned get_immutable_sampler(const RhiSpirvDescriptorSetLayout *set, unsigned binding)
   {
      return unsigned((set->immutable_samplers >> (4 * binding)) & 0xf);
   }

   inline void set_immutable_sampler(RhiSpirvDescriptorSetLayout *set, unsigned binding, unsigned sampler)
   {
      set->immutable_samplers |= uint64_t(sampler) << (4 * binding);
      set->immutable_sampler_mask |= 1u << binding;
   }
}

void rhi_spirv_reflect(const uint32_t *data, size_t word_count,
      RhiSpirvResourceLayout *out)
{
   memset(out, 0, sizeof(*out));

   Compiler compiler(data, word_count);
   ShaderResources resources = compiler.get_shader_resources();

   for (Resource &image : resources.sampled_images)
   {
      uint32_t set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
      const SPIRType &type = compiler.get_type(image.base_type_id);
      if (type.image.dim == spv::DimBuffer)
         out->sets[set].sampled_buffer_mask |= 1u << binding;
      else
         out->sets[set].sampled_image_mask |= 1u << binding;

      if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
         out->sets[set].fp_mask |= 1u << binding;

      unsigned sampler;
      if (type.image.dim != spv::DimBuffer && get_stock_sampler(&sampler, image.name.c_str()))
      {
         if (has_immutable_sampler(&out->sets[set], binding))
         {
            if (sampler != get_immutable_sampler(&out->sets[set], binding))
               LOGE("Immutable sampler mismatch detected!\n");
         }
         else
            set_immutable_sampler(&out->sets[set], binding, sampler);
      }
   }

   for (Resource &image : resources.subpass_inputs)
   {
      uint32_t set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
      out->sets[set].input_attachment_mask |= 1u << binding;

      const SPIRType &type = compiler.get_type(image.base_type_id);
      if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
         out->sets[set].fp_mask |= 1u << binding;
   }

   for (Resource &image : resources.separate_images)
   {
      uint32_t set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);

      const SPIRType &type = compiler.get_type(image.base_type_id);
      if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
         out->sets[set].fp_mask |= 1u << binding;

      if (type.image.dim == spv::DimBuffer)
         out->sets[set].sampled_buffer_mask |= 1u << binding;
      else
         out->sets[set].separate_image_mask |= 1u << binding;
   }

   for (Resource &image : resources.separate_samplers)
   {
      uint32_t set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
      out->sets[set].sampler_mask |= 1u << binding;

      unsigned sampler;
      if (get_stock_sampler(&sampler, image.name.c_str()))
      {
         if (has_immutable_sampler(&out->sets[set], binding))
         {
            if (sampler != get_immutable_sampler(&out->sets[set], binding))
               LOGE("Immutable sampler mismatch detected!\n");
         }
         else
            set_immutable_sampler(&out->sets[set], binding, sampler);
      }
   }

   for (Resource &image : resources.storage_images)
   {
      uint32_t set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
      out->sets[set].storage_image_mask |= 1u << binding;

      const SPIRType &type = compiler.get_type(image.base_type_id);
      if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
         out->sets[set].fp_mask |= 1u << binding;
   }

   for (Resource &buffer : resources.uniform_buffers)
   {
      uint32_t set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
      out->sets[set].uniform_buffer_mask |= 1u << binding;
   }

   for (Resource &buffer : resources.storage_buffers)
   {
      uint32_t set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
      uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
      out->sets[set].storage_buffer_mask |= 1u << binding;
   }

   for (Resource &attrib : resources.stage_inputs)
   {
      uint32_t location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
      out->input_mask |= 1u << location;
   }

   for (Resource &attrib : resources.stage_outputs)
   {
      uint32_t location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
      out->output_mask |= 1u << location;
   }

   if (!resources.push_constant_buffers.empty())
   {
      /* Don't bother trying to extract which part of a push constant block we're
       * using. Just assume we're accessing everything. */
      out->push_constant_size =
         (uint32_t)compiler.get_declared_struct_size(
               compiler.get_type(resources.push_constant_buffers.front().base_type_id));
   }

   std::vector<SpecializationConstant> spec_constants = compiler.get_specialization_constants();
   for (SpecializationConstant &c : spec_constants)
   {
      if (c.constant_id >= RHI_SPIRV_VK_NUM_SPEC_CONSTANTS)
      {
         LOGE("Spec constant ID: %u is out of range, will be ignored.\n", c.constant_id);
         continue;
      }
      out->spec_constant_mask |= 1u << c.constant_id;
   }
}
