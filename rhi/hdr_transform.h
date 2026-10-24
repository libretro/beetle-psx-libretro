#ifndef HDR_TRANSFORM_H__
#define HDR_TRANSFORM_H__

/* Display Transform for the HDR outputs: tone curves, the full ACES 2.0
 * output transform, HDR expansion and highlight hue preservation. Builds
 * the constant block and lookup tables rhi/shaders_vulkan/hdr_transform.h
 * reads, for the Vulkan and GL backends alike. */

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Curve numbering is shared with the shaders. 0 keeps hdr.h's soft knee. */
enum hdr_dt_curve
{
   HDR_DT_KNEE = 0,
   HDR_DT_ACES,
   HDR_DT_HEJL,
   HDR_DT_GT,
   HDR_DT_HABLE,
   HDR_DT_LOTTES,
   HDR_DT_DRAGO,
   HDR_DT_UNREAL,
   HDR_DT_FILMICALU,
   HDR_DT_RPLAIN,
   HDR_DT_REXT,
   HDR_DT_EXPO,
   HDR_DT_HABLE2017,
   HDR_DT_ACES2,
   HDR_DT_TUMBLIN,
   HDR_DT_WARD,
   HDR_DT_SCHLICK,
   HDR_DT_DEVLIN,
   HDR_DT_FILMICLOG,
   HDR_DT_JODIE,
   HDR_DT_ACESFIT,
   HDR_DT_NEUTRAL,
   HDR_DT_AGX,
   HDR_DT_ACES2FULL,
   HDR_DT_COUNT
};

struct hdr_dt_settings
{
   int   curve;            /* enum hdr_dt_curve */
   int   aces2_surround;   /* 0 dark, 1 dim, 2 average */
   int   aces2_gamut;      /* 0 Rec.709, 1 P3-D65, 2 Rec.2020, 3 the display's */
   int   peak_mode;        /* 0 frontend, 1 the panel's own, 2 peak_nits */
   float peak_nits;
   int   agx_punchy;
   float gt_toe;
   float gt_linear;
   int   filmic_contrast;  /* 0 Very Low .. 6 Very High */
   float filmic_max_ev;
   int   hue_preserve;
   int   expansion;        /* 0 none, 1 per channel, 2 hue preserving */
};

/* std140, matching the DisplayTransform block. */
struct hdr_dt_block
{
   float in_rgb_to_cam[12];
   float in_cone_to_aab[12];
   float lim_aab_to_cone[12];
   float lim_cam_to_rgb[12];
   float aces2_to_out[12];
   float mode[4];
   float sh[4];
   float gt[4];
   float film[4];
   float in_scalars[4];
   float lim_scalars[4];
   float ts[4];
   float cc[4];
   float cc2[4];
   float gm[4];
   float lut_scale[4];
};

#define HDR_DT_ACES2_LUT_WIDTH 2048
#define HDR_DT_FILM_LUT_WIDTH  256
#define HDR_DT_DESAT_WIDTH     1089
#define HDR_DT_DESAT_HEIGHT    33

void hdr_dt_configure(const struct hdr_dt_settings *s);
bool hdr_dt_enabled(void);

/* Fills the block for one frame. panel_xy (CIE xy x 10000, all zero when
 * unknown) and panel_peak feed the ACES 2.0 Auto gamut and Display peak. */
void hdr_dt_fill(struct hdr_dt_block *b, float paper_white_nits,
      float peak_nits, float panel_peak_nits, const uint16_t panel_xy[8]);

/* Lookup tables, unorm16. The ACES 2.0 one is RGBA and valid once
 * hdr_dt_fill ran with that curve; the film curve is one channel; the
 * desaturation atlas is RGBA. Rebuilds bump the generation. */
const uint16_t *hdr_dt_aces2_lut(void);
const uint16_t *hdr_dt_film_lut(void);
const uint16_t *hdr_dt_film_desat(void);
unsigned hdr_dt_generation(void);

/* IEEE half from a unorm16, for GLES, which has no 16-bit unorm. */
uint16_t hdr_dt_unorm16_to_half(uint16_t v);

/* CPU twin of a per-channel curve, scene 1.0 = paper white; for tests. */
double hdr_dt_curve_cpu(int curve, double v, const struct hdr_dt_block *b);

void hdr_dt_free(void);

/* ---- HDR post effects: bloom, halation, chromatic aberration ----
 *
 * Run between the display quad and the encode: the quad writes scene-linear
 * light (1.0 = paper white) into a half-float image, a bright pass and blur
 * chain build the bloom from it, and a composite applies the chromatic
 * aberration, adds the (halation-tinted) bloom and then runs the same grade
 * and encode the quad would have. Off by default, in which case none of it
 * runs and the quad encodes directly as before. */
#define HDR_POST_CONV_MAX 6

struct hdr_post_settings
{
   float bloom;         /* amount, 0 off */
   int   bloom_knee;    /* 1: threshold from the active curve's knee */
   int   convolution;   /* 1: six-octave pyramid instead of two bands */
   float firefly_knee;  /* 1 standard, 0.25 wide, 0.0625 widest */
   int   halation;      /* 0 off, 1 subtle, 2 strong */
   int   chromatic;     /* 0 off, 1 subtle, 2 strong, 3 heavy */
};

/* Per frame, from the settings and the scene size. */
struct hdr_post_frame
{
   float bright[4];     /* threshold, knee, firefly knee, unused */
   float hal_bloom[4];  /* halation tint, bloom amount (0 when off) */
   float band_w[2];     /* tight band, wide band (0 under the pyramid) */
   float ca;            /* chromatic aberration, 0 off */
   int   bloom;         /* run the bloom passes */
   int   levels;        /* pyramid levels, 0 for the two-band chain */
};

void hdr_post_configure(const struct hdr_post_settings *s);
bool hdr_post_enabled(void);
void hdr_post_frame_params(struct hdr_post_frame *f, unsigned w, unsigned h);

#ifdef __cplusplus
}
#endif

#endif
