/* Display Transform constants and tables; see hdr_transform.h. Ported from
 * boom3's HDR present, with ACES 2.0 fed Rec.709 converted to AP0 rather
 * than read as AP0. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hdr_transform.h"
#include "hdr_aces2.h"
#include "hdr_filmic_contrast.h"
#include "hdr_filmic_desat.h"

static struct hdr_dt_settings dt_set = {
   HDR_DT_KNEE, 1, 3, 0, 1000.0f, 0, 0.22f, 0.40f, 3, 4.026068811f, 0, 0
};

static unsigned  dt_generation = 1;

static aces2Params_t *dt_aces2;
static uint16_t  dt_aces2_lut[HDR_DT_ACES2_LUT_WIDTH * 4];
static double    dt_aces2_scales[4];
static float     dt_aces2_peak;
static int       dt_aces2_gamut = -1;
static int       dt_aces2_surround = -1;
static uint16_t  dt_aces2_xy[8];

/* ACES 2.0 (full) baked from the double-precision reference: a cube of
 * output colours over the PQ code of the input light, built a few rows per
 * hdr_dt_fill and switched to once complete. Keyed by the tables it was
 * built from (dt_aces2_built bumps per rebuild) and paper white. */
static uint16_t *dt_cube;          /* half RGBA, CUBE^3 */
static unsigned  dt_cube_rows;     /* rows of CUBE done, of CUBE * CUBE */
static unsigned  dt_cube_key;      /* dt_aces2_built it was baked from */
static float     dt_cube_paper;
static unsigned  dt_aces2_built;

static uint16_t  dt_film_lut[HDR_DT_FILM_LUT_WIDTH];
static int       dt_film_contrast = -1;
static uint16_t *dt_film_desat;

/* Primaries as CTL chromaticities: red, green, blue, white xy. */
static const double DT_PRIMS_709[8]  = { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290 };
static const double DT_PRIMS_P3[8]   = { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290 };
static const double DT_PRIMS_2020[8] = { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290 };

/* ---- curves, normalised so paper white lands on paper white ---- */

static double dt_aces(double v)
{
   return v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14) * 1.2440945;
}

static double dt_hejl(double v)
{
   double x = v > 0.004 ? v - 0.004 : 0.0;
   double n = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
   return pow(n, 2.2) * 1.4629683;
}

static void dt_gt_params(double m, double l, double *S0, double *S1, double *C2)
{
   double l0 = (1.0 - m) * l;
   *S0 = m + l0;
   *S1 = m + l0;
   *C2 = 1.0 / (1.0 - *S1);
}

static double dt_gt_at(double v, double m, double l)
{
   double S0, S1, C2, t, w0, w1, w2, T, S;
   dt_gt_params(m, l, &S0, &S1, &C2);
   t  = v / m;
   if (t > 1.0)
      t = 1.0;
   else if (t < 0.0)
      t = 0.0;
   w0 = 1.0 - (t * t * (3.0 - 2.0 * t));
   w2 = (v >= S0) ? 1.0 : 0.0;
   w1 = 1.0 - w0 - w2;
   T  = m * pow((v > 0.0 ? v : 0.0) / m, 1.33);
   S  = 1.0 - (1.0 - S1) * exp(-C2 * (v - S0));
   return T * w0 + v * w1 + S * w2;
}

static double dt_gt_norm(void)
{
   double f = dt_gt_at(1.0, dt_set.gt_toe, dt_set.gt_linear);
   return f > 1e-6 ? 1.0 / f : 1.0;
}

static double dt_hable(double v)
{
   double n = ((v * (0.15 * v + 0.05) + 0.004) / (v * (0.15 * v + 0.50) + 0.06)) - 0.0666667;
   return n * 4.5319149;
}

static double dt_lottes(double v)
{
   if (v <= 0.0)
      return 0.0;
   return (pow(v, 1.6) / (pow(v, 1.5632) * 0.1674199 + 1.0730397)) * 1.2404596;
}

static double dt_drago(double v)
{
   double den;
   if (v <= 0.0)
      return 0.0;
   den = log(2.0 + 8.0 * pow(v / 8.0, 0.2344653));
   if (den <= 0.0)
      return 0.0;
   return (log(1.0 + v) / den) / 0.9542425 * 2.6616800;
}

static double dt_unreal(double v)
{
   return pow(v / (v + 0.155) * 1.019, 2.2) * 1.3173380;
}

static double dt_filmicalu(double v)
{
   double x  = v > 0.0 ? v : 0.0;
   double va = 1.425 * x + 0.05;
   double f  = ((x * va + 0.004) / ((x * va + 0.055) + 0.0491)) - 0.0821;
   return pow(f > 0.0 ? f : 0.0, 2.2) * 1.4132626;
}

static double dt_hable2017(double v)
{
   double x = v > 0.0 ? v : 0.0;
   double f;
   if (x < 0.25)
      f = 1.1313708 * pow(x, 1.25);
   else if (x < 0.70)
      f = 0.20 + (x - 0.25);
   else
      f = 0.65 + 0.35 * (x - 0.70) / ((x - 0.70) + 0.35);
   return f * 1.2322275;
}

static double dt_aces2_ts(double v)
{
   double x = v > 0.0 ? v : 0.0;
   double f = 1.0471038 * pow(x / (x + 0.9198583), 1.15);
   return (f * f / (f + 0.04)) * 2.1854781;
}

/* The curve below paper white, before any shoulder. */
static double dt_base(int curve, double v)
{
   double x = v > 0.0 ? v : 0.0;
   switch (curve)
   {
      case HDR_DT_HEJL:      return dt_hejl(v);
      case HDR_DT_HABLE:     return dt_hable(v);
      case HDR_DT_LOTTES:    return dt_lottes(v);
      case HDR_DT_DRAGO:     return dt_drago(v);
      case HDR_DT_UNREAL:    return dt_unreal(v);
      case HDR_DT_FILMICALU: return dt_filmicalu(v);
      case HDR_DT_RPLAIN:    return (x / (1.0 + x)) * 2.0;
      case HDR_DT_REXT:      return (x * (1.0 + x / 16.0) / (1.0 + x)) * 1.8823529;
      case HDR_DT_EXPO:      return (1.0 - exp(-x)) * 1.5819767;
      case HDR_DT_HABLE2017: return dt_hable2017(v);
      case HDR_DT_ACES2:     return dt_aces2_ts(v);
      case HDR_DT_TUMBLIN:   return pow(x, 0.6075);
      case HDR_DT_WARD:      return x;
      case HDR_DT_SCHLICK:   return 2.3 * x / (2.3 * x - x + 1.0);
      case HDR_DT_DEVLIN:    return (x / (x + 0.3010864)) * 1.3010864;
      default:               break;
   }
   return dt_aces(v);
}

double hdr_dt_curve_cpu(int curve, double v, const struct hdr_dt_block *b)
{
   double e;
   if (curve == HDR_DT_GT)
   {
      double m = b->gt[0], t, w0, w1, w2, T, S;
      t  = v / m;
      if (t > 1.0)
         t = 1.0;
      else if (t < 0.0)
         t = 0.0;
      w0 = 1.0 - (t * t * (3.0 - 2.0 * t));
      w2 = (v >= b->gt[1]) ? 1.0 : 0.0;
      w1 = 1.0 - w0 - w2;
      T  = m * pow((v > 0.0 ? v : 0.0) / m, 1.33);
      S  = 1.0 - b->gt[2] * exp(-b->gt[3] * (v - b->gt[1]));
      return (T * w0 + v * w1 + S * w2) * b->sh[2];
   }
   if (v <= 1.0)
      return dt_base(curve, v);
   e = v - 1.0;
   return 1.0 + e * b->sh[0] * b->sh[1] / (e + b->sh[0]);
}

/* Every curve here flattens out below the display's headroom, so it runs
 * to paper white and a shoulder carries it to H, joined at its slope. */
static void dt_shoulder_params(int curve, float H, float *A, float *slope_out)
{
   const double d = 1e-4;
   double slope = (dt_base(curve, 1.0 + d) - dt_base(curve, 1.0 - d)) / (2.0 * d);
   if (H <= 1.0001f || slope <= 0.0)
   {
      *A         = 0.0f;
      *slope_out = 0.0f;
      return;
   }
   *A         = (float)(((double)H - 1.0) / slope);
   *slope_out = (float)slope;
}

/* ---- Filmic Log ---- */

static const float *dt_film_table(void)
{
   int c = dt_set.filmic_contrast;
   return filmic_contrast_tables[(c < 0 || c > 6) ? 3 : c];
}

/* Rescale from the cube's 25 stops to the curve's window, and the factor
 * that pins scene 1.0 to 1.0. */
static void dt_film_terms(float out[2])
{
   const double mn    = -12.4739312;
   const double span  = dt_set.filmic_max_ev - mn;
   const double pos   = ((0.0 - mn) / span) * (double)(FILMIC_CURVE_SIZE - 1);
   const int    i     = (int)pos < FILMIC_CURVE_SIZE - 2 ? (int)pos : FILMIC_CURVE_SIZE - 2;
   const float *tab   = dt_film_table();
   const double c     = tab[i] + (tab[i + 1] - tab[i]) * (pos - (double)i);
   out[0] = (float)(25.0 / span);
   out[1] = (float)(1.0 / pow(c > 1e-6 ? c : 1e-6, 2.2));
}

static void dt_film_build(void)
{
   int i;
   if (dt_film_contrast != dt_set.filmic_contrast)
   {
      const float *tab = dt_film_table();
      for (i = 0; i < HDR_DT_FILM_LUT_WIDTH; i++)
      {
         double v = tab[i];
         if (v < 0.0)
            v = 0.0;
         if (v > 1.0)
            v = 1.0;
         dt_film_lut[i] = (uint16_t)(v * 65535.0 + 0.5);
      }
      dt_film_contrast = dt_set.filmic_contrast;
      dt_generation++;
   }
   if (!dt_film_desat)
   {
      dt_film_desat = (uint16_t*)malloc(sizeof(uint16_t) * 4
            * HDR_DT_DESAT_WIDTH * HDR_DT_DESAT_HEIGHT);
      if (!dt_film_desat)
         return;
      for (i = 0; i < HDR_DT_DESAT_WIDTH * HDR_DT_DESAT_HEIGHT; i++)
      {
         dt_film_desat[i * 4 + 0] = filmic_desat[i * 3 + 0];
         dt_film_desat[i * 4 + 1] = filmic_desat[i * 3 + 1];
         dt_film_desat[i * 4 + 2] = filmic_desat[i * 3 + 2];
         dt_film_desat[i * 4 + 3] = 65535;
      }
      dt_generation++;
   }
}

/* ---- ACES 2.0 ---- */

static bool dt_panel_prims(const uint16_t xy[8], double out[8])
{
   bool known = false;
   int  i;
   for (i = 0; i < 6; i++)
      if (xy[i])
         known = true;
   if (!known)
      return false;
   for (i = 0; i < 6; i++)
      out[i] = xy[i] / 10000.0;
   /* The output is D65; a missing white is D65. */
   out[6] = xy[6] ? xy[6] / 10000.0 : 0.3127;
   out[7] = xy[7] ? xy[7] / 10000.0 : 0.3290;
   return true;
}

static const double *dt_limit_prims(const uint16_t panel_xy[8], double scratch[8])
{
   switch (dt_set.aces2_gamut)
   {
      case 0:  return DT_PRIMS_709;
      case 1:  return DT_PRIMS_P3;
      case 2:  return DT_PRIMS_2020;
      default: break;
   }
   if (dt_panel_prims(panel_xy, scratch))
   {
      double m[9];
      if (ACES2_PrimariesMatrix(scratch, DT_PRIMS_2020, m))
         return scratch;
   }
   return DT_PRIMS_P3;
}

static bool dt_aces2_build(float peak, const uint16_t panel_xy[8])
{
   double scratch[8];
   const double *prims;
   bool same_xy = (memcmp(dt_aces2_xy, panel_xy, sizeof(dt_aces2_xy)) == 0);

   /* Peak arrives every frame; a jittering last bit must not re-solve
    * the searched tables. */
   if (dt_aces2 && fabs((double)dt_aces2_peak - (double)peak) < 0.5
         && dt_aces2_gamut == dt_set.aces2_gamut
         && dt_aces2_surround == dt_set.aces2_surround
         && (dt_set.aces2_gamut != 3 || same_xy))
      return true;

   if (!dt_aces2)
   {
      dt_aces2 = (aces2Params_t*)calloc(1, sizeof(*dt_aces2));
      if (!dt_aces2)
         return false;
   }
   prims = dt_limit_prims(panel_xy, scratch);
   ACES2_SetSurround(dt_set.aces2_surround);
   ACES2_Init(prims, (double)peak, dt_aces2);
   ACES2_PackHueTables(dt_aces2, dt_aces2_lut, dt_aces2_scales);
   dt_aces2_built++;
   dt_aces2_peak     = peak;
   dt_aces2_gamut    = dt_set.aces2_gamut;
   dt_aces2_surround = dt_set.aces2_surround;
   memcpy(dt_aces2_xy, panel_xy, sizeof(dt_aces2_xy));
   dt_generation++;
   return true;
}

/* A CTL 3x3 (row vector times matrix, i.e. GL column-major) into std140. */
static void dt_mat3(float dst[12], const double m[9])
{
   int c;
   for (c = 0; c < 3; c++)
   {
      dst[c * 4 + 0] = (float)m[c * 3 + 0];
      dst[c * 4 + 1] = (float)m[c * 3 + 1];
      dst[c * 4 + 2] = (float)m[c * 3 + 2];
      dst[c * 4 + 3] = 0.0f;
   }
}

/* IEEE half, round to nearest even; the cube's storage. */
static uint16_t dt_half(float f)
{
   union { float f; uint32_t u; } v;
   uint32_t sign, mant;
   int      exp;
   v.f  = f;
   sign = (v.u >> 16) & 0x8000u;
   exp  = (int)((v.u >> 23) & 0xff) - 127 + 15;
   mant = v.u & 0x7fffffu;
   if (exp >= 31)
      return (uint16_t)(sign | 0x7bffu);
   if (exp <= 0)
   {
      if (exp < -10)
         return (uint16_t)sign;
      mant |= 0x800000u;
      {
         unsigned shift = (unsigned)(14 - exp);
         uint32_t h     = mant >> shift;
         uint32_t rem   = mant & ((1u << shift) - 1u);
         uint32_t half  = 1u << (shift - 1u);
         if (rem > half || (rem == half && (h & 1u)))
            h++;
         return (uint16_t)(sign | h);
      }
   }
   {
      uint32_t h = ((uint32_t)exp << 10) | (mant >> 13);
      uint32_t rem = mant & 0x1fffu;
      if (rem > 0x1000u || (rem == 0x1000u && (h & 1u)))
         h++;
      return (uint16_t)(sign | h);
   }
}

/* The shader's ACES 2.0 input stage on the CPU: Rec.709 scene light (1.0 =
 * paper white) to AP0 at paper/100 exposure, clamped through AP1 to the
 * tone scale's forward limit, then the reference transform. Out: the
 * limiting primaries, 1.0 = 100 nits - what dt_aces2() returns. */
void hdr_dt_aces2_cpu(const double v[3], float paper_white_nits, double out[3])
{
   static const double to_ap0[9] = {
      0.4397010, 0.3829780, 0.1773350,
      0.0897923, 0.8134230, 0.0967616,
      0.0175440, 0.1115440, 0.8707040 };
   static const double ap0_ap1[9] = {
       1.4514393161, -0.2365107469, -0.2149285693,
      -0.0765537733,  1.1762296998, -0.0996759265,
       0.0083161484, -0.0060324498,  0.9977163014 };
   static const double ap1_ap0[9] = {
      0.6954522414, 0.1406786965, 0.1638690622,
      0.0447945634, 0.8596711185, 0.0955343182,
     -0.0055258826, 0.0040252103, 1.0015006723 };
   double a[3], b[3], c[3];
   double exposure = (double)(float)(paper_white_nits / 100.0f);
   double limit;
   int    i;
   if (!dt_aces2)
   {
      out[0] = out[1] = out[2] = 0.0;
      return;
   }
   limit = (double)(float)dt_aces2->ts.forward_limit;
   for (i = 0; i < 3; i++)
      a[i] = (to_ap0[i * 3] * v[0] + to_ap0[i * 3 + 1] * v[1]
            + to_ap0[i * 3 + 2] * v[2]) * exposure;
   for (i = 0; i < 3; i++)
   {
      b[i] = ap0_ap1[i * 3] * a[0] + ap0_ap1[i * 3 + 1] * a[1] + ap0_ap1[i * 3 + 2] * a[2];
      if (b[i] < 0.0)
         b[i] = 0.0;
      if (b[i] > limit)
         b[i] = limit;
   }
   for (i = 0; i < 3; i++)
      c[i] = ap1_ap0[i * 3] * b[0] + ap1_ap0[i * 3 + 1] * b[1] + ap1_ap0[i * 3 + 2] * b[2];
   ACES2_OutputTransformFwd(c, dt_aces2, out);
}

/* Scene light of cube node i: the inverse of the shader's PQ index. */
static double dt_cube_light(unsigned i, float paper)
{
   const double m1 = 0.1593017578125, m2 = 78.84375;
   const double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
   double u = (double)i / (HDR_DT_ACES2_CUBE - 1);
   double p = pow(u, 1.0 / m2);
   double d = c2 - c3 * p;
   double y = (p > c1 && d > 0.0) ? pow((p - c1) / d, 1.0 / m1) : 0.0;
   return y * 10000.0 / paper;
}

/* Bake up to `rows` rows of the cube; true once it is complete. */
static bool dt_cube_bake(unsigned rows, float paper)
{
   const unsigned n = HDR_DT_ACES2_CUBE;
   if (!dt_aces2)
      return false;
   if (dt_cube_key != dt_aces2_built || dt_cube_paper != paper)
   {
      dt_cube_key   = dt_aces2_built;
      dt_cube_paper = paper;
      if (dt_cube_rows == n * n)
         dt_generation++;     /* the complete cube in use goes stale */
      dt_cube_rows  = 0;
   }
   if (dt_cube_rows == n * n)
      return true;
   if (!dt_cube)
   {
      dt_cube = (uint16_t*)malloc(sizeof(uint16_t) * 4 * n * n * n);
      if (!dt_cube)
         return false;
   }
   while (rows-- && dt_cube_rows < n * n)
   {
      unsigned row = dt_cube_rows++;
      unsigned g = row % n, b = row / n, r;
      for (r = 0; r < n; r++)
      {
         double    v[3], o[3];
         uint16_t *dst = dt_cube + 4 * (row * n + r);
         v[0] = dt_cube_light(r, paper);
         v[1] = dt_cube_light(g, paper);
         v[2] = dt_cube_light(b, paper);
         hdr_dt_aces2_cpu(v, paper, o);
         dst[0] = dt_half((float)o[0]);
         dst[1] = dt_half((float)o[1]);
         dst[2] = dt_half((float)o[2]);
         dst[3] = 0x3c00u;
      }
   }
   if (dt_cube_rows < n * n)
      return false;
   dt_generation++;
   return true;
}

void hdr_dt_aces2_cube_finish(float paper_white_nits)
{
   dt_cube_bake(HDR_DT_ACES2_CUBE * HDR_DT_ACES2_CUBE, paper_white_nits);
}

const uint16_t *hdr_dt_aces2_cube(void)
{
   return (dt_cube && dt_cube_rows == HDR_DT_ACES2_CUBE * HDR_DT_ACES2_CUBE)
         ? dt_cube : NULL;
}

/* ---- public ---- */

/* Bumped whenever the Display Transform or post-effect settings change. */
static unsigned dt_serial = 1;

void hdr_dt_configure(const struct hdr_dt_settings *s)
{
   struct hdr_dt_settings n = *s;
   if (n.curve < 0 || n.curve >= HDR_DT_COUNT)
      n.curve = HDR_DT_KNEE;
   if (memcmp(&n, &dt_set, sizeof(n)))
      dt_serial++;
   dt_set = n;
}

unsigned hdr_dt_serial(void)
{
   return dt_serial;
}

bool hdr_dt_enabled(void)
{
   return dt_set.curve != HDR_DT_KNEE;
}

void hdr_dt_fill(struct hdr_dt_block *b, float paper_white_nits,
      float peak_nits, float panel_peak_nits, const uint16_t panel_xy[8])
{
   int   curve = dt_set.curve;
   float paper = paper_white_nits > 0.0f ? paper_white_nits : 200.0f;
   float peak  = peak_nits;
   float H, A, slope, film[2];
   double S0, S1, C2;

   memset(b, 0, sizeof(*b));
   if (dt_set.peak_mode == 1 && panel_peak_nits > 0.0f)
      peak = panel_peak_nits;
   else if (dt_set.peak_mode == 2 && dt_set.peak_nits > 0.0f)
      peak = dt_set.peak_nits;
   if (!(peak > 0.0f))
      peak = 1000.0f;
   H = peak / paper;
   if (H < 1.0f)
      H = 1.0f;

   if (curve == HDR_DT_ACES2FULL && !dt_aces2_build(peak, panel_xy))
      curve = HDR_DT_ACES2;   /* no tables: the tone scale alone */

   b->mode[0] = (float)curve;
   b->mode[1] = H;
   b->mode[2] = (dt_set.hue_preserve && curve != HDR_DT_NEUTRAL
         && curve != HDR_DT_AGX && curve != HDR_DT_ACES2FULL) ? 1.0f : 0.0f;
   b->mode[3] = (float)dt_set.expansion;

   dt_shoulder_params(curve, H, &A, &slope);
   dt_gt_params(dt_set.gt_toe, dt_set.gt_linear, &S0, &S1, &C2);
   b->sh[0] = A;
   b->sh[1] = slope;
   b->sh[2] = (float)dt_gt_norm();
   b->sh[3] = dt_set.agx_punchy ? 1.0f : 0.0f;
   b->gt[0] = dt_set.gt_toe;
   b->gt[1] = (float)S0;
   b->gt[2] = (float)(1.0 - S1);
   b->gt[3] = (float)C2;

   if (curve == HDR_DT_FILMICLOG)
      dt_film_build();
   dt_film_terms(film);
   b->film[0] = film[0];
   b->film[1] = film[1];
   b->film[2] = 0.75f;           /* expansion knee */
   b->film[3] = paper / 100.0f;  /* ACES 2.0 exposure */

   if (curve == HDR_DT_ACES2FULL)
   {
      const aces2Params_t *p = dt_aces2;
      double scratch[8], m[9];
      int i;
      dt_mat3(b->in_rgb_to_cam,   p->input.RGB_to_CAM16_c);
      dt_mat3(b->in_cone_to_aab,  p->input.cone_to_Aab);
      dt_mat3(b->lim_aab_to_cone, p->limit.Aab_to_cone);
      dt_mat3(b->lim_cam_to_rgb,  p->limit.CAM16_c_to_RGB);
      if (!ACES2_PrimariesMatrix(dt_limit_prims(panel_xy, scratch), DT_PRIMS_2020, m))
         for (i = 0; i < 9; i++)
            m[i] = (i % 4 == 0) ? 1.0 : 0.0;
      dt_mat3(b->aces2_to_out, m);
      b->in_scalars[0]  = (float)p->input.F_L_n;
      b->in_scalars[1]  = (float)p->input.cz;
      b->in_scalars[2]  = (float)p->input.inv_cz;
      b->in_scalars[3]  = (float)p->input.A_w_J;
      b->lim_scalars[0] = (float)p->limit.F_L_n;
      b->lim_scalars[1] = (float)p->limit.cz;
      b->lim_scalars[2] = (float)p->limit.inv_cz;
      b->lim_scalars[3] = (float)p->limit.A_w_J;
      b->ts[0]  = (float)p->ts.m_2;
      b->ts[1]  = (float)p->ts.s_2;
      b->ts[2]  = (float)p->ts.g;
      b->ts[3]  = (float)p->ts.t_1;
      b->cc[0]  = (float)p->chroma.limit_J_max;
      b->cc[1]  = (float)p->chroma.model_gamma_inv;
      b->cc[2]  = (float)p->chroma.sat;
      b->cc[3]  = (float)p->chroma.sat_thr;
      b->cc2[0] = (float)p->chroma.compr;
      b->cc2[1] = (float)p->chroma.chroma_compress_scale;
      b->cc2[2] = (float)p->gamut.mid_J;
      b->cc2[3] = (float)p->gamut.focus_dist;
      b->gm[0]  = (float)p->gamut.lower_hull_gamma_inv;
      b->gm[1]  = (float)p->ts.forward_limit;
      /* Once the cube is baked the shader reads it instead of solving the
       * transform per pixel. Until then, a slice of it per frame. */
      b->gm[2]  = dt_cube_bake(HDR_DT_CUBE_ROWS_PER_FILL, paper) ? 1.0f : 0.0f;
      for (i = 0; i < 4; i++)
         b->lut_scale[i] = (float)dt_aces2_scales[i];
   }
}

const uint16_t *hdr_dt_aces2_lut(void)
{
   return dt_aces2 ? dt_aces2_lut : NULL;
}

const uint16_t *hdr_dt_film_lut(void)
{
   return dt_film_contrast >= 0 ? dt_film_lut : NULL;
}

const uint16_t *hdr_dt_film_desat(void)
{
   return dt_film_desat;
}

unsigned hdr_dt_generation(void)
{
   return dt_generation;
}

uint16_t hdr_dt_unorm16_to_half(uint16_t v)
{
   double   f = v / 65535.0;
   double   mant;
   int      e;
   uint32_t m;
   if (v == 0)
      return 0;
   if (v == 65535)
      return 0x3C00;
   if (f < 6.103515625e-05)             /* below the smallest normal half */
      return (uint16_t)(f * 16777216.0 + 0.5);
   mant = frexp(f, &e);                 /* f = mant * 2^e, mant in [0.5, 1) */
   m    = (uint32_t)((mant * 2.0 - 1.0) * 1024.0 + 0.5);
   e    = e - 1 + 15;
   if (m == 1024)
   {
      m = 0;
      e++;
   }
   return (uint16_t)((e << 10) | m);
}

/* ---- post effects ---- */

static struct hdr_post_settings post_set = { 0.0f, 0, 0, 1.0f, 0, 0 };

void hdr_post_configure(const struct hdr_post_settings *s)
{
   if (memcmp(s, &post_set, sizeof(*s)))
      dt_serial++;
   post_set = *s;
}

bool hdr_post_enabled(void)
{
   return post_set.bloom > 0.0f || post_set.chromatic > 0;
}

bool hdr_post_compact(void)
{
   return post_set.compact != 0;
}

/* The bright pass threshold. Classic is the 0.70 the bloom was tuned
 * against; the curve knee follows Neutral's start of compression or GT's
 * solved shoulder start, so the bloom picks the pixels the curve will
 * compress. Other curves have no clean knee and keep 0.70. */
static float post_threshold(void)
{
   if (post_set.bloom_knee)
   {
      if (dt_set.curve == HDR_DT_NEUTRAL)
         return 0.76f;
      if (dt_set.curve == HDR_DT_GT)
      {
         double S0, S1, C2;
         dt_gt_params(dt_set.gt_toe, dt_set.gt_linear, &S0, &S1, &C2);
         return (float)S0;
      }
   }
   return 0.70f;
}

void hdr_post_frame_params(struct hdr_post_frame *f, unsigned w, unsigned h)
{
   int n = 0;
   unsigned bw = w / 4, bh = h / 4;

   memset(f, 0, sizeof(*f));
   f->bloom = post_set.bloom > 0.0f && w >= 4 && h >= 4;
   if (post_set.convolution)
   {
      while (n < HDR_POST_CONV_MAX && bw >= 4 && bh >= 4)
      {
         n++;
         bw >>= 1;
         bh >>= 1;
      }
      if (n < 1)
         n = 1;
   }
   f->levels       = f->bloom ? n : 0;
   f->bright[0]    = post_threshold();
   f->bright[1]    = 0.25f;
   f->bright[2]    = post_set.firefly_knee;
   f->hal_bloom[0] = 1.0f;
   f->hal_bloom[1] = post_set.halation == 2 ? 0.72f : post_set.halation == 1 ? 0.86f : 1.0f;
   f->hal_bloom[2] = post_set.halation == 2 ? 0.45f : post_set.halation == 1 ? 0.70f : 1.0f;
   f->hal_bloom[3] = f->bloom ? post_set.bloom : 0.0f;
   /* Total bloom energy is the same either way: the pyramid spreads the
    * two bands' 0.36 over its levels. */
   f->band_w[0]    = f->levels ? 0.36f / (float)f->levels : 0.22f;
   f->band_w[1]    = f->levels ? 0.0f : 0.14f;
   f->ca           = post_set.chromatic >= 3 ? 0.0088400f
                   : post_set.chromatic == 2 ? 0.0044200f
                   : post_set.chromatic == 1 ? 0.0022100f : 0.0f;
}

void hdr_dt_free(void)
{
   free(dt_cube);
   dt_cube      = NULL;
   dt_cube_rows = 0;
   free(dt_aces2);
   dt_aces2 = NULL;
   dt_aces2_gamut = -1;
   dt_aces2_surround = -1;
   free(dt_film_desat);
   dt_film_desat = NULL;
   dt_film_contrast = -1;
   dt_generation++;
}
