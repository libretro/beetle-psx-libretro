/* Single translation unit for the vendored pl_mpeg decoder.
 *
 * pl_mpeg (c) Dominic Szablewski, MIT licence.
 * https://github.com/phoboslab/pl_mpeg
 *
 * Vendored rather than reimplemented: it is a complete MPEG-1 Program Stream
 * demuxer plus MPEG-1 video and MPEG-1 Layer II audio decoder in one header,
 * which is exactly and only what a Video CD contains.
 *
 * Drop pl_mpeg.h alongside this file. PLM_NO_STDIO keeps the FILE* helpers
 * out; the core feeds the decoder from CDIF sectors, never from a path.
 */

#define PLM_NO_STDIO 1
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
