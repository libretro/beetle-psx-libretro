#pragma once

namespace PSX
{
static const uint32_t quad_vert[] =
#include "quad.vert.inc"
    ;
static const uint32_t scaled_quad_frag[] =
#include "scaled.quad.frag.inc"
    ;
static const uint32_t scaled_dither_quad_frag[] =
#include "scaled.dither.quad.frag.inc"
    ;
static const uint32_t bpp24_quad_frag[] =
#include "bpp24.quad.frag.inc"
    ;
static const uint32_t bpp24_yuv_quad_frag[] =
#include "bpp24.yuv.quad.frag.inc"
    ;
static const uint32_t unscaled_quad_frag[] =
#include "unscaled.quad.frag.inc"
    ;
static const uint32_t unscaled_dither_quad_frag[] =
#include "unscaled.dither.quad.frag.inc"
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
static const uint32_t resolve_to_msaa_scaled[] =
#include "resolve.msaa.scaled.comp.inc"
    ;
static const uint32_t resolve_to_unscaled[] =
#include "resolve.unscaled.comp.inc"
    ;
static const uint32_t resolve_msaa_to_unscaled[] =
#include "resolve.msaa.unscaled.comp.inc"
    ;

static const uint32_t flat_vert[] =
#include "flat.vert.inc"
    ;
static const uint32_t flat_frag[] =
#include "flat.frag.inc"
    ;
static const uint32_t textured_vert[] =
#include "textured.vert.inc"
    ;
static const uint32_t textured_unscaled_vert[] =
#include "textured.unscaled.vert.inc"
    ;
static const uint32_t textured_frag[] =
#include "textured.frag.inc"
    ;
static const uint32_t textured_unscaled_frag[] =
#include "textured.unscaled.frag.inc"
    ;
static const uint32_t textured_msaa_frag[] =
#include "textured.msaa.frag.inc"
    ;
static const uint32_t textured_msaa_unscaled_frag[] =
#include "textured.msaa.unscaled.frag.inc"
    ;

static const uint32_t blit_vram_scaled_comp[] =
#include "blit_vram.scaled.comp.inc"
    ;
static const uint32_t blit_vram_scaled_masked_comp[] =
#include "blit_vram.masked.scaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_comp[] =
#include "blit_vram.cached.scaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_masked_comp[] =
#include "blit_vram.cached.masked.scaled.comp.inc"
    ;

static const uint32_t blit_vram_msaa_scaled_comp[] =
#include "blit_vram.msaa.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_scaled_masked_comp[] =
#include "blit_vram.msaa.masked.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_cached_scaled_comp[] =
#include "blit_vram.msaa.cached.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_cached_scaled_masked_comp[] =
#include "blit_vram.msaa.cached.masked.scaled.comp.inc"
    ;

static const uint32_t blit_vram_unscaled_comp[] =
#include "blit_vram.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_unscaled_masked_comp[] =
#include "blit_vram.masked.unscaled.comp.inc"
    ;

static const uint32_t blit_vram_cached_unscaled_comp[] =
#include "blit_vram.cached.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_unscaled_masked_comp[] =
#include "blit_vram.cached.masked.unscaled.comp.inc"
    ;

static const uint32_t feedback_frag[] =
#include "feedback.frag.inc"
    ;
static const uint32_t feedback_unscaled_frag[] =
#include "feedback.unscaled.frag.inc"
    ;
static const uint32_t feedback_flat_frag[] =
#include "feedback.flat.frag.inc"
    ;

static const uint32_t feedback_msaa_frag[] =
#include "feedback.msaa.frag.inc"
    ;
static const uint32_t feedback_msaa_unscaled_frag[] =
#include "feedback.msaa.unscaled.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_frag[] =
#include "feedback.msaa.flat.frag.inc"
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
static const uint32_t mipmap_dither_resolve_frag[] =
#include "mipmap.dither.resolve.frag.inc"
    ;
static const uint32_t mipmap_energy_frag[] =
#include "mipmap.energy.frag.inc"
    ;
static const uint32_t mipmap_energy_blur_frag[] =
#include "mipmap.energy.blur.frag.inc"
    ;
}
