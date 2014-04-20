#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

#if defined(WANT_32BPP)
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((r << RED_SHIFT) | (g << GREEN_SHIFT) | (b << BLUE_SHIFT) | (a << ALPHA_SHIFT))
#elif defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
/* 16bit color - RGB565 */
#define RED_MASK  0xf800
#define GREEN_MASK 0x7e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b, a) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#elif defined(WANT_16BPP) && !defined(FRONTEND_SUPPORTS_RGB565)
/* 16bit color - RGB555 */
#define RED_MASK  0x7c00
#define GREEN_MASK 0x3e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 3
#define BLUE_EXPAND 3
#define RED_SHIFT 10
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b, a) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#endif

struct MDFN_PaletteEntry
{
 uint8 r, g, b;
};

typedef struct
{
 int32 x, y, w, h;
} MDFN_Rect;

enum
{
 MDFN_COLORSPACE_RGB = 0,
 MDFN_COLORSPACE_YCbCr = 1,
 MDFN_COLORSPACE_YUV = 2, // TODO, maybe.
};

class MDFN_PixelFormat
{
 public:

 MDFN_PixelFormat();
 MDFN_PixelFormat(const unsigned int p_colorspace, const uint8 p_rs, const uint8 p_gs, const uint8 p_bs, const uint8 p_as);

 unsigned int bpp;
 unsigned int colorspace;

 union
 {
  uint8 Rshift;  // Bit position of the lowest bit of the red component
  uint8 Yshift;
 };

 union
 {
  uint8 Gshift;  // [...] green component
  uint8 Ushift;
  uint8 Cbshift;
 };

 union
 {
  uint8 Bshift;  // [...] blue component
  uint8 Vshift;
  uint8 Crshift;
 };

 uint8 Ashift;  // [...] alpha component.
 // Creates a color value for the surface corresponding to the 8-bit R/G/B/A color passed.
 INLINE uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a = 0) const
 {
    return MAKECOLOR(r, g, b, a);
 }

 // Gets the R/G/B/A values for the passed 32-bit surface pixel value
#if defined(WANT_32BPP)
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = (value >> RED_SHIFT) & 0xFF;
    g = (value >> GREEN_SHIFT) & 0xFF;
    b = (value >> BLUE_SHIFT) & 0xFF;
    a = (value >> ALPHA_SHIFT) & 0xFF;
 }
#elif defined(WANT_16BPP)
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = (value & BLUE_MASK) << RED_SHIFT;
    g = (value & GREEN_MASK) << GREEN_SHIFT;
    b = (value & RED_MASK);
 }
#endif

}; // MDFN_PixelFormat;

// Supports 32-bit RGBA
//  16-bit is WIP
class MDFN_Surface //typedef struct
{
 public:

 MDFN_Surface();
 MDFN_Surface(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf);

 ~MDFN_Surface();

 uint16 *pixels16;
 uint32 *pixels;

 // w, h, and pitch32 should always be > 0
 int32 w;
 int32 h;

 union
 {
  int32 pitch32; // In pixels, not in bytes.
  int32 pitchinpix;	// New name, new code should use this.
 };

 MDFN_PaletteEntry *palette;

 MDFN_PixelFormat format;

 void SetFormat(const MDFN_PixelFormat &new_format, bool convert);

 // Creates a value for the surface corresponding to the R/G/B/A color passed.
 INLINE uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a = 0) const
 {
    return MAKECOLOR(r, g, b, a);
 }

#if defined(WANT_32BPP)
 // Gets the R/G/B/A values for the passed 32-bit surface pixel value
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = (value >> RED_SHIFT) & 0xFF;
    g = (value >> GREEN_SHIFT) & 0xFF;
    b = (value >> BLUE_SHIFT) & 0xFF;
    a = (value >> ALPHA_SHIFT) & 0xFF;
 }

 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b) const
 {
    r = (value >> RED_SHIFT) & 0xFF;
    g = (value >> GREEN_SHIFT) & 0xFF;
    b = (value >> BLUE_SHIFT) & 0xFF;
 }
#elif defined(WANT_16BPP)

 // Gets the R/G/B/A values for the passed 32-bit surface pixel value
 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b, int &a) const
 {
    r = (value & BLUE_MASK) << RED_SHIFT;
    g = (value & GREEN_MASK) << GREEN_SHIFT;
    b = (value & RED_MASK);
 }

 INLINE void DecodeColor(uint32 value, int &r, int &g, int &b) const
 {
  r = (value & BLUE_MASK) << RED_SHIFT;
  g = (value & GREEN_MASK) << GREEN_SHIFT;
  b = (value & RED_MASK);
 }
#endif
 private:
 void Init(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf);
};

#endif
