/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __MDFN_AUDIOREADER_H
#define __MDFN_AUDIOREADER_H

#include <stdint.h>
#include <boolean.h>

#include "../cdstream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio-track decoder.
 *
 * Used to be a C++ AudioReader abstract base class with a single
 * concrete OggVorbisReader subclass.  Now a plain C struct: Vorbis
 * is the only format we support (CCD/CUE .wav-aliased-as-Ogg
 * tracks), so the vtable collapses to direct calls.  All state -
 * the Vorbis decoder + the last-read-position cursor used by the
 * frame_offset != LastReadPos seek shortcut - lives in the struct
 * defined in audioreader.c.
 *
 * AR_Open / AR_Close do NOT take ownership of the cdstream object;
 * the caller retains it and is responsible for freeing it.  The
 * AudioReader does assume exclusive access for its lifetime. */
typedef struct AudioReader AudioReader;

/* Returns NULL if the input stream is not a valid Ogg Vorbis file. */
AudioReader *AR_Open(cdstream *fp);

void    AR_Close     (AudioReader *r);

/* Read `frames` stereo s16 frames into `buffer`, starting at
 * `frame_offset`.  Returns the number of frames actually decoded
 * (may be less than `frames` at EOF).  Internally seeks only when
 * `frame_offset` doesn't match the cursor. */
int64_t AR_Read      (AudioReader *r, int64_t frame_offset,
                      int16_t *buffer, int64_t frames);

int64_t AR_FrameCount(AudioReader *r);

#ifdef __cplusplus
}
#endif

#endif
