#ifndef BEETLE_PSX_GPU_UV_PHASE_H__
#define BEETLE_PSX_GPU_UV_PHASE_H__

/* Software rasteriser U/V fixed point: seed phase and per-pixel slope.
 * Shared with tools/uv_phase, which checks it against native sampling. */

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING 12

/* Slope of a CALCIS cross product over the triangle area. Above 1x the
 * native-sized numerator is over an upscaled area: widen and keep
 * upscale_shift more fraction bits so the native precision survives. */
static INLINE uint32_t gpu_uv_delta(int32_t cis, int32_t denom,
      unsigned upscale_shift)
{
   if (!upscale_shift)
      return (uint32_t)(cis * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
   return (uint32_t)((int64_t)cis * ((int64_t)1 << (COORD_FBS + upscale_shift))
         / denom) << (COORD_POST_PADDING - upscale_shift);
}

/* Sub-pixels sample at their top-left corner. 2D keeps every sub-pixel of
 * a native pixel on that pixel's texel: half an upscaled texel, plus a
 * near-texel advance for flipped axes. 3D keeps the native half-texel
 * rounding at every scale, so magnified texels stay in native phase. */
static INLINE uint32_t gpu_uv_seed(int32_t uv, unsigned upscale_shift,
      bool sprite_phase, bool flipped)
{
   uint32_t seed;
   if (!upscale_shift || !sprite_phase)
      return (uint32_t)(COORD_MF_INT(uv) + (1 << (COORD_FBS - 1)))
            << COORD_POST_PADDING;
   seed = (uint32_t)(COORD_MF_INT(uv) + (1 << (COORD_FBS - 1 - upscale_shift)));
   if (flipped)
      seed += (uint32_t)(COORD_MF_INT(1) - (1 << (COORD_FBS - upscale_shift)));
   return seed << COORD_POST_PADDING;
}

/* Perspective-correct counterpart of the seed bias, in texels. */
static INLINE float gpu_uv_pct_bias(unsigned upscale_shift, bool sprite_phase)
{
   return sprite_phase ? 0.5f / (float)(1 << upscale_shift) : 0.5f;
}

#endif
