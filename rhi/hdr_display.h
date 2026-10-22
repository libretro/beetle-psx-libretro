/* Display-side inputs to the HDR10 encode: where the panel's black sits and
 * which gamut it can actually show. Both come from the frontend's
 * GET_DISPLAY_METADATA / GET_HDR_METADATA answers and are reduced here to the
 * two values the shaders consume, so nothing per pixel has to derive them. */
#ifndef HDR_DISPLAY_H
#define HDR_DISPLAY_H

#include <stdint.h>
#include <boolean.h>

/* SMPTE ST 2084 encode of an absolute luminance, 0..1 over 0..10000 nits. */
float hdr_display_pq(float nits);

/* Linear Rec.2020 -> linear panel RGB, column-major (to_panel[c * 3 + r]),
 * from the panel's CIE xy primaries in 1/10000 units (Rx,Ry,Gx,Gy,Bx,By, then
 * white, which is ignored: HDR10 is D65-referenced and the panel maps D65 to
 * its own white). Returns false and zeroes to_panel when the primaries are
 * unknown (all zero) or do not span a triangle; a zero matrix is what tells
 * the shaders to skip panel gamut compression. */
bool hdr_display_panel_matrix(const uint16_t primaries_xy[8], float to_panel[9]);

#endif
