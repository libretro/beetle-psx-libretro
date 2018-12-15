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
static const uint32_t resolve_msaa_to_scaled[] =
#include "resolve.msaa.scaled.comp.inc"
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
static const uint32_t resolve_to_unscaled_16[] =
#include "resolve.unscaled.16.comp.inc"
    ;

static const uint32_t opaque_flat_vert[] =
#include "opaque.flat.vert.inc"
    ;
static const uint32_t opaque_flat_frag[] =
#include "opaque.flat.frag.inc"
    ;
static const uint32_t opaque_flat_xbr_frag[] =
#include "opaque.flat.xbr.frag.inc"
    ;
static const uint32_t opaque_flat_sabr_frag[] =
#include "opaque.flat.sabr.frag.inc"
    ;
static const uint32_t opaque_flat_bilinear_frag[] =
#include "opaque.flat.bilinear.frag.inc"
    ;
static const uint32_t opaque_flat_3point_frag[] =
#include "opaque.flat.3point.frag.inc"
    ;
static const uint32_t opaque_flat_jinc2_frag[] =
#include "opaque.flat.jinc2.frag.inc"
    ;
static const uint32_t opaque_textured_vert[] =
#include "opaque.textured.vert.inc"
    ;
static const uint32_t opaque_textured_frag[] =
#include "opaque.textured.frag.inc"
    ;
static const uint32_t opaque_textured_xbr_frag[] =
#include "opaque.textured.xbr.frag.inc"
    ;
static const uint32_t opaque_textured_sabr_frag[] =
#include "opaque.textured.sabr.frag.inc"
    ;
static const uint32_t opaque_textured_bilinear_frag[] =
#include "opaque.textured.bilinear.frag.inc"
    ;
static const uint32_t opaque_textured_3point_frag[] =
#include "opaque.textured.3point.frag.inc"
    ;
static const uint32_t opaque_textured_jinc2_frag[] =
#include "opaque.textured.jinc2.frag.inc"
    ;
static const uint32_t opaque_semitrans_frag[] =
#include "semitrans.opaque.textured.frag.inc"
    ;
static const uint32_t opaque_semitrans_xbr_frag[] =
#include "semitrans.opaque.textured.xbr.frag.inc"
    ;
static const uint32_t opaque_semitrans_sabr_frag[] =
#include "semitrans.opaque.textured.sabr.frag.inc"
    ;
static const uint32_t opaque_semitrans_bilinear_frag[] =
#include "semitrans.opaque.textured.bilinear.frag.inc"
    ;
static const uint32_t opaque_semitrans_3point_frag[] =
#include "semitrans.opaque.textured.3point.frag.inc"
    ;
static const uint32_t opaque_semitrans_jinc2_frag[] =
#include "semitrans.opaque.textured.jinc2.frag.inc"
    ;
static const uint32_t semitrans_frag[] =
#include "semitrans.trans.textured.frag.inc"
    ;
static const uint32_t semitrans_xbr_frag[] =
#include "semitrans.trans.textured.xbr.frag.inc"
    ;
static const uint32_t semitrans_sabr_frag[] =
#include "semitrans.trans.textured.sabr.frag.inc"
    ;
static const uint32_t semitrans_bilinear_frag[] =
#include "semitrans.trans.textured.bilinear.frag.inc"
    ;
static const uint32_t semitrans_3point_frag[] =
#include "semitrans.trans.textured.3point.frag.inc"
    ;
static const uint32_t semitrans_jinc2_frag[] =
#include "semitrans.trans.textured.jinc2.frag.inc"
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

static const uint32_t feedback_msaa_add_frag[] =
#include "feedback.msaa.add.frag.inc"
    ;
static const uint32_t feedback_msaa_avg_frag[] =
#include "feedback.msaa.avg.frag.inc"
    ;
static const uint32_t feedback_msaa_sub_frag[] =
#include "feedback.msaa.sub.frag.inc"
    ;
static const uint32_t feedback_msaa_add_quarter_frag[] =
#include "feedback.msaa.add_quarter.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_add_frag[] =
#include "feedback.msaa.flat.add.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_avg_frag[] =
#include "feedback.msaa.flat.avg.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_sub_frag[] =
#include "feedback.msaa.flat.sub.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_add_quarter_frag[] =
#include "feedback.msaa.flat.add_quarter.frag.inc"
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
