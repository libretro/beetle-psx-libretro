#ifndef __MDFN_MAPPED_READAHEAD_H
#define __MDFN_MAPPED_READAHEAD_H

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>
#include <streams/file_stream.h>

/* Windows starts no read-ahead on a mapped view, so a disc image read
 * straight from one stalls the emulation thread on the disk at each new
 * page. A read that goes on from where the last one ended (within
 * MAPPED_RA_GAP) hints the MAPPED_RA_AHEAD bytes past it, again once
 * half of that is used. Elsewhere the kernel reads ahead by itself. */
#define MAPPED_RA_AHEAD (4u * 1024u * 1024u)
#define MAPPED_RA_GAP   (256u * 1024u)

typedef struct mapped_readahead
{
   uint64_t next;
   uint64_t last;
} mapped_readahead;

/* true: hint [pos, pos + len + MAPPED_RA_AHEAD) */
static INLINE bool mapped_readahead_step(mapped_readahead *ra,
      uint64_t pos, uint64_t len)
{
   uint64_t end = pos + len;
   bool     seq = pos >= ra->last && pos - ra->last <= MAPPED_RA_GAP;

   ra->last = end;
   if (!seq)
      ra->next = 0;
   if (!seq || end <= ra->next)
      return false;
   ra->next = end + MAPPED_RA_AHEAD / 2;
   return true;
}

static INLINE void mapped_readahead_note(mapped_readahead *ra, RFILE *fp,
      uint64_t pos, uint64_t len)
{
#ifdef _WIN32
   if (fp && mapped_readahead_step(ra, pos, len))
      filestream_prefetch(fp, pos, len + MAPPED_RA_AHEAD);
#else
   (void)ra;
   (void)fp;
   (void)pos;
   (void)len;
#endif
}

#endif
