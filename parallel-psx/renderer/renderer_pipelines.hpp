#pragma once

namespace PSX
{
static const uint32_t quad_vert[] =
#include "quad.vert.inc"
    ;
static const uint32_t scaled_quad_frag[] =
#include "scaled.quad.frag.inc"
    ;
static const uint32_t bpp24_quad_frag[] =
#include "bpp24.quad.frag.inc"
    ;
static const uint32_t unscaled_quad_frag[] =
#include "unscaled.quad.frag.inc"
    ;
static const uint32_t copy_vram_comp[] =
#include "copy_vram.comp.inc"
    ;
static const uint32_t copy_vram_masked_comp[] =
#include "copy_vram.masked.comp.inc"
    ;
static const uint32_t resolve_to_scaled[] =
#include "resolve.scaled.comp.inc"
    ;
static const uint32_t resolve_to_unscaled_2[] =
#include "resolve.unscaled.2.comp.inc"
    ;
static const uint32_t resolve_to_unscaled_4[] =
#include "resolve.unscaled.4.comp.inc"
    ;
static const uint32_t resolve_to_unscaled_8[] =
#include "resolve.unscaled.8.comp.inc"
    ;
static const uint32_t opaque_flat_vert[] =
#include "opaque.flat.vert.inc"
    ;
static const uint32_t opaque_flat_frag[] =
#include "opaque.flat.frag.inc"
    ;
static const uint32_t opaque_textured_vert[] =
#include "opaque.textured.vert.inc"
    ;
static const uint32_t opaque_textured_frag[] =
#include "opaque.textured.frag.inc"
    ;
static const uint32_t opaque_semitrans_frag[] =
#include "semitrans.opaque.textured.frag.inc"
    ;
static const uint32_t semitrans_frag[] =
#include "semitrans.trans.textured.frag.inc"
    ;
static const uint32_t blit_vram_unscaled_comp[] =
#include "blit_vram.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_scaled_comp[] =
#include "blit_vram.scaled.comp.inc"
    ;
static const uint32_t blit_vram_unscaled_masked_comp[] =
#include "blit_vram.masked.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_scaled_masked_comp[] =
#include "blit_vram.masked.scaled.comp.inc"
    ;

static const uint32_t blit_vram_cached_unscaled_comp[] =
#include "blit_vram.cached.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_comp[] =
#include "blit_vram.cached.scaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_unscaled_masked_comp[] =
#include "blit_vram.cached.masked.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_masked_comp[] =
#include "blit_vram.cached.masked.scaled.comp.inc"
    ;

static const uint32_t feedback_add_frag[] =
#include "feedback.add.frag.inc"
    ;
static const uint32_t feedback_avg_frag[] =
#include "feedback.avg.frag.inc"
    ;
static const uint32_t feedback_sub_frag[] =
#include "feedback.sub.frag.inc"
    ;
static const uint32_t feedback_add_quarter_frag[] =
#include "feedback.add_quarter.frag.inc"
    ;
static const uint32_t feedback_flat_add_frag[] =
#include "feedback.flat.add.frag.inc"
    ;
static const uint32_t feedback_flat_avg_frag[] =
#include "feedback.flat.avg.frag.inc"
    ;
static const uint32_t feedback_flat_sub_frag[] =
#include "feedback.flat.sub.frag.inc"
    ;
static const uint32_t feedback_flat_add_quarter_frag[] =
#include "feedback.flat.add_quarter.frag.inc"
    ;
static const uint32_t mipmap_vert[] =
#include "mipmap.vert.inc"
    ;
static const uint32_t mipmap_shifted_vert[] =
#include "mipmap.shifted.vert.inc"
    ;
static const uint32_t mipmap_energy_first_frag[] =
#include "mipmap.energy.first.frag.inc"
    ;
static const uint32_t mipmap_resolve_frag[] =
#include "mipmap.resolve.frag.inc"
    ;
static const uint32_t mipmap_energy_frag[] =
#include "mipmap.energy.frag.inc"
    ;
static const uint32_t mipmap_energy_blur_frag[] =
#include "mipmap.energy.blur.frag.inc"
    ;
}
