/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>

#include <retro_assert.h>
#include <compat/msvc.h>
#include <compat/strl.h>
#include <retro_inline.h>

#include "state.h"

#define SSEEK_END	2
#define SSEEK_CUR	1
#define SSEEK_SET	0

#define RLSB 		MDFNSTATE_RLSB	/* 0x80000000 */

/* Forward declaration */
int StateAction(StateMem *sm, int load, int data_only);

/* Fast Save States exclude string labels from 
 * variables in the savestate, and are at least 20% faster.
 * Only used for internal savestates which will not be written to a file. */

bool FastSaveStates = false;

static INLINE void MDFN_en32lsb_(uint8_t *buf, uint32_t morp)
{
   buf[0]=morp;
   buf[1]=morp>>8;
   buf[2]=morp>>16;
   buf[3]=morp>>24;
}

static INLINE uint32_t MDFN_de32lsb_(const uint8_t *morp)
{
   return(morp[0]|(morp[1]<<8)|(morp[2]<<16)|(morp[3]<<24));
}

/* Read `len` bytes from the state stream into `buffer`. Returns the
 * number of bytes read (== len on success, 0 on failure / short stream).
 *
 * Defensive against malformed savestates: if (len + st->loc) overflows
 * uint32_t, the resulting wraparound would compare smaller than st->len
 * and silently read out-of-bounds. The check is restructured to
 * subtract before adding, which can't overflow since loc <= len by the
 * stream's invariant. */
static int32_t smem_read(StateMem *st, void *buffer, uint32_t len)
{
   if (st->loc > st->len || len > (st->len - st->loc))
      return 0;

   memcpy(buffer, st->data + st->loc, len);
   st->loc += len;

   return(len);
}

/* Append `len` bytes from `buffer` to the state stream, growing
 * st->data with realloc as needed. Returns the number of bytes
 * actually written - on realloc failure or integer overflow, this is
 * 0 and the original buffer is preserved (realloc returns NULL but
 * does not free the old pointer, so we capture the new pointer in a
 * temporary first instead of overwriting st->data with NULL). */
static int32_t smem_write(StateMem *st, void *buffer, uint32_t len)
{
   /* Overflow guard: len + st->loc must not wrap. Re-arrange as a
    * subtraction-from-UINT32_MAX so we can compare safely. */
   if (len > UINT32_MAX - st->loc)
      return 0;

   if ((len + st->loc) > st->malloced)
   {
      uint8_t *new_data;
      uint32_t target  = len + st->loc;
      uint32_t newsize = (st->malloced >= 32768) ? st->malloced : (st->initial_malloc ? st->initial_malloc : 32768);

      while (newsize < target)
      {
         /* The doubling can overflow uint32_t for huge writes; cap at
          * UINT32_MAX so we still attempt the realloc with a sane
          * size. realloc() will either succeed or return NULL, both
          * of which we handle. */
         if (newsize > UINT32_MAX / 2)
         {
            newsize = UINT32_MAX;
            break;
         }
         newsize *= 2;
      }

      if (newsize < target)
         return 0;

      new_data = (uint8_t *)realloc(st->data, newsize);
      if (!new_data)
         return 0;

      st->data     = new_data;
      st->malloced = newsize;
   }

   memcpy(st->data + st->loc, buffer, len);
   st->loc += len;

   if (st->loc > st->len)
      st->len = st->loc;

   return (int32_t)len;
}

/* Seek the state stream. Returns 0 on success, -1 on failure (in
 * which case st->loc is clamped to a sane value).
 *
 * The original implementation computed the new location with raw
 * addition/subtraction and then post-checked for st->loc > st->len:
 *
 *   - SSEEK_END with offset > st->len underflowed to a huge value.
 *     The post-check did clamp it, but only to st->len (when it
 *     should have stayed at whatever the caller requested before
 *     the underflow - i.e. an unrepresentable position).
 *   - SSEEK_CUR with offset large enough could wrap st->loc past
 *     UINT32_MAX back to a small "valid" value.
 *   - SSEEK_SET with offset > st->len silently clamped without
 *     signalling failure.
 *
 * Now we compute into a candidate position with explicit overflow
 * checks, and only commit on success. */
static int32_t smem_seek(StateMem *st, uint32_t offset, int whence)
{
   uint32_t new_loc;

   switch(whence)
   {
      case SSEEK_SET:
         new_loc = offset;
         break;
      case SSEEK_END:
         if (offset > st->len)
         {
            st->loc = 0;
            return -1;
         }
         new_loc = st->len - offset;
         break;
      case SSEEK_CUR:
         if (offset > UINT32_MAX - st->loc)
         {
            st->loc = st->len;
            return -1;
         }
         new_loc = st->loc + offset;
         break;
      default:
         return -1;
   }

   if (new_loc > st->len)
   {
      st->loc = st->len;
      return -1;
   }

   st->loc = new_loc;
   return 0;
}

static int smem_write32le(StateMem *st, uint32_t b)
{
   uint8_t s[4];
   s[0]=b;
   s[1]=b>>8;
   s[2]=b>>16;
   s[3]=b>>24;
   return((smem_write(st, s, 4)<4)?0:4);
}

static int smem_read32le(StateMem *st, uint32_t *b)
{
   uint8_t s[4];

   if(smem_read(st, s, 4) < 4)
      return(0);

   *b = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);

   return(4);
}

#ifdef MSB_FIRST
static void FlipByteOrder(uint8_t *src, uint32_t count)
{
   uint8_t *start = src;
   uint8_t *end;

   if ((count & 1) || !count)
      return;     /* This shouldn't happen. */

   end = src + count - 1;
   /* Iterate while start < end, not 'count' times: the original loop
    * over-iterated and effectively performed a no-op on every even count.
    * That broke savestate portability across LE<->BE for every
    * MDFNSTATE_RLSB-marked field. */
   while (start < end)
   {
      uint8_t tmp;

      tmp = *end;
      *end = *start;
      *start = tmp;
      end--;
      start++;
   }
}
#endif

static bool SubWrite(StateMem *st, SFORMAT *sf)
{
   while(sf->size || sf->name)	/* Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct. */
   {
      if(!sf->size || !sf->v)
      {
         sf++;
         continue;
      }

      if(sf->size == (uint32_t)~0)		/* Link to another struct.	*/
      {
         if(!SubWrite(st, (SFORMAT *)sf->v))
            return(0);

         sf++;
         continue;
      }

      int32_t bytesize = sf->size;

      /* exclude text labels from fast savestates */
      if (!FastSaveStates)
      {
         char nameo[1 + 256];
         int slen      = strlcpy(nameo + 1, sf->name, 255);
         nameo[256]    = 0;
         nameo[0]      = slen;

         /* Bail out on the first short write - subsequent writes
          * would land at the wrong offset (loc didn't advance) and
          * the resulting state would be unparseable. */
         if (smem_write(st, nameo, 1 + nameo[0]) != 1 + nameo[0])
            return false;
      }
      if (smem_write32le(st, bytesize) != 4)
         return false;

#ifdef MSB_FIRST
      /* Flip the byte order... */
      if(sf->flags & MDFNSTATE_BOOL) { }
      else if(sf->flags & MDFNSTATE_RLSB64)
         Endian_A64_Swap(sf->v, bytesize / sizeof(uint64_t));
      else if(sf->flags & MDFNSTATE_RLSB32)
         Endian_A32_Swap(sf->v, bytesize / sizeof(uint32_t));
      else if(sf->flags & MDFNSTATE_RLSB16)
         Endian_A16_Swap(sf->v, bytesize / sizeof(uint16_t));
      else if(sf->flags & RLSB)
         FlipByteOrder((uint8_t*)sf->v, bytesize);
#endif

      /* Special case for the evil bool type, 
       * to convert bool to 1-byte elements.
       * Don't do it if we're only saving the raw data. */
      if(sf->flags & MDFNSTATE_BOOL)
      {
         for(int32_t bool_monster = 0; bool_monster < bytesize; bool_monster++)
         {
            uint8_t tmp_bool = ((bool *)sf->v)[bool_monster];
            if (smem_write(st, &tmp_bool, 1) != 1)
            {
#ifdef MSB_FIRST
               /* Restore the byte order before bailing so we don't
                * leave the live emulator state byte-flipped. */
               if(sf->flags & MDFNSTATE_RLSB64)
                  Endian_A64_LE_to_NE(sf->v, bytesize / sizeof(uint64_t));
               else if(sf->flags & MDFNSTATE_RLSB32)
                  Endian_A32_LE_to_NE(sf->v, bytesize / sizeof(uint32_t));
               else if(sf->flags & MDFNSTATE_RLSB16)
                  Endian_A16_LE_to_NE(sf->v, bytesize / sizeof(uint16_t));
               else if(sf->flags & RLSB)
                  FlipByteOrder((uint8_t*)sf->v, bytesize);
#endif
               return false;
            }
         }
      }
      else
      {
         if (smem_write(st, (uint8_t *)sf->v, bytesize) != bytesize)
         {
#ifdef MSB_FIRST
            if(sf->flags & MDFNSTATE_BOOL) { }
            else if(sf->flags & MDFNSTATE_RLSB64)
               Endian_A64_LE_to_NE(sf->v, bytesize / sizeof(uint64_t));
            else if(sf->flags & MDFNSTATE_RLSB32)
               Endian_A32_LE_to_NE(sf->v, bytesize / sizeof(uint32_t));
            else if(sf->flags & MDFNSTATE_RLSB16)
               Endian_A16_LE_to_NE(sf->v, bytesize / sizeof(uint16_t));
            else if(sf->flags & RLSB)
               FlipByteOrder((uint8_t*)sf->v, bytesize);
#endif
            return false;
         }
      }

#ifdef MSB_FIRST
      /* Now restore the original byte order. */
      if(sf->flags & MDFNSTATE_BOOL) { }
      else if(sf->flags & MDFNSTATE_RLSB64)
         Endian_A64_LE_to_NE(sf->v, bytesize / sizeof(uint64_t));
      else if(sf->flags & MDFNSTATE_RLSB32)
         Endian_A32_LE_to_NE(sf->v, bytesize / sizeof(uint32_t));
      else if(sf->flags & MDFNSTATE_RLSB16)
         Endian_A16_LE_to_NE(sf->v, bytesize / sizeof(uint16_t));
      else if(sf->flags & RLSB)
         FlipByteOrder((uint8_t*)sf->v, bytesize);
#endif
      sf++; 
   }

   return true;
}

static int WriteStateChunk(StateMem *st, const char *sname, SFORMAT *sf)
{
   int32_t data_start_pos;
   int32_t end_pos;
   uint8_t sname_tmp[32];
   size_t sname_len = strlen(sname);

   memset(sname_tmp, 0, sizeof(sname_tmp));
   memcpy((char *)sname_tmp, sname, (sname_len < 32) ? sname_len : 32);

   if (smem_write(st, sname_tmp, 32) != 32)
      return 0;
   /* We'll come back and write this later. */
   if (smem_write32le(st, 0) != 4)
      return 0;

   data_start_pos = st->loc;

   if(!SubWrite(st, sf))
      return(0);

   end_pos = st->loc;

   /* The two seeks below should always succeed (we're seeking to
    * positions we already wrote), but propagate failure rather than
    * silently producing a state with a wrong size header. */
   if (smem_seek(st, data_start_pos - 4, SSEEK_SET) < 0)
      return 0;
   if (smem_write32le(st, end_pos - data_start_pos) != 4)
      return 0;
   if (smem_seek(st, end_pos, SSEEK_SET) < 0)
      return 0;

   return(end_pos - data_start_pos);
}

static SFORMAT *FindSF(const char *name, SFORMAT *sf)
{
   /* Size can sometimes be zero, so also check for the text name.  These two should both be zero only at the end of a struct. */
   while(sf->size || sf->name) 
   {
      if(!sf->size || !sf->v)
      {
         sf++;
         continue;
      }

      if (sf->size == (uint32_t)~0) /* Link to another SFORMAT structure. */
      {
         SFORMAT *temp_sf = FindSF(name, (SFORMAT*)sf->v);
         if (temp_sf)
            return temp_sf;
      }
      else
      {
         /* for fast savestates, we no longer have the 
          * text label in the state, and need to assume 
          * that it is the correct one. */
         if (FastSaveStates)
            return sf;
         assert(sf->name);
         if (!strcmp(sf->name, name))
            return sf;
      }

      sf++;
   }

   return NULL;
}

static int ReadStateChunk(StateMem *st, SFORMAT *sf, int size)
{
   int temp = st->loc;

   uint32_t recorded_size;  /* In bytes */
   uint8_t toa[1 + 256];    /* Don't change to char unless 
                               cast toa[0] to unsigned to 
                               smem_read() and other places. */
   toa[0] = 0;
   toa[1] = 0;

   while (st->loc < (uint32_t)(temp + size))
   {
      /* exclude text labels from fast savestates */
      if (!FastSaveStates)
      {
         if (smem_read(st, toa, 1) != 1)
            return(0);

         if (smem_read(st, toa + 1, toa[0]) != toa[0])
            return 0;

         toa[1 + toa[0]] = 0;
      }

      /* Defensive: if we couldn't read the recorded-size word, the
       * stream is truncated - bail rather than dispatching on an
       * uninitialized recorded_size that smem_read32le left untouched
       * on its short-read path. */
      if (smem_read32le(st, &recorded_size) != 4)
         return 0;

      SFORMAT *tmp = FindSF((char*)toa + 1, sf);
      /* Fix for unnecessary name checks, when we find 
       * it in the first slot, don't recheck that slot again.
       * Also necessary for fast savestates to work. */
      if (tmp == sf)
         sf++;

      if(tmp)
      {
         uint32_t expected_size = tmp->size;	/* In bytes */

         if(recorded_size != expected_size)
         {
            if(smem_seek(st, recorded_size, SSEEK_CUR) < 0)
               return(0);
         }
         else
         {
            /* Refuse to load a partial subsystem region. A short
             * read here would leave tmp->v half-old / half-new and
             * silently break determinism for the entire run. */
            if (smem_read(st, (uint8_t *)tmp->v, expected_size) != expected_size)
               return 0;

            if(tmp->flags & MDFNSTATE_BOOL)
            {
               int32_t bool_monster;
               /*  Converting downwards is necessary for 
                *  the case of sizeof(bool) > 1 */
               for(bool_monster = expected_size - 1; bool_monster >= 0; bool_monster--)
                  ((bool *)tmp->v)[bool_monster] = ((uint8_t *)tmp->v)[bool_monster];
            }
#ifdef MSB_FIRST
            if(tmp->flags & MDFNSTATE_RLSB64)
               Endian_A64_LE_to_NE(tmp->v, expected_size / sizeof(uint64_t));
            else if(tmp->flags & MDFNSTATE_RLSB32)
               Endian_A32_LE_to_NE(tmp->v, expected_size / sizeof(uint32_t));
            else if(tmp->flags & MDFNSTATE_RLSB16)
               Endian_A16_LE_to_NE(tmp->v, expected_size / sizeof(uint16_t));
            else if(tmp->flags & RLSB)
               FlipByteOrder((uint8_t*)tmp->v, expected_size);
#endif
         }
      }
      else
      {
         if(smem_seek(st, recorded_size, SSEEK_CUR) < 0)
            return(0);
      }
   }

   assert(st->loc == (uint32_t)(temp + size));
   return 1;
}

/* This function is called by the game driver(NES, GB, GBA) to save a state. */
static int MDFNSS_StateAction_internal(void *st_p, int load, int data_only, struct SSDescriptor *section)
{
   StateMem *st = (StateMem*)st_p;

   if(load)
   {
      char sname[32];

      int found = 0;
      uint32_t tmp_size;
      /* Capture the position before the section search so we can
       * restore it absolutely at the end - the original code did
       * 'smem_seek(st, -total, SSEEK_CUR)' which only worked on
       * uint32 underflow, and post-hardening smem_seek that idiom
       * is rejected by the overflow guard. */
      uint32_t entry_loc = st->loc;

      while(smem_read(st, (uint8_t *)sname, 32) == 32)
      {
         if(smem_read32le(st, &tmp_size) != 4)
            return(0);

         /* Yay, we found the section */
         if(!strncmp(sname, section->name, 32))
         {
            if(!ReadStateChunk(st, section->sf, tmp_size))
               return(0);
            found = 1;
            break;
         } 
         else
         {
            if(smem_seek(st, tmp_size, SSEEK_CUR) < 0)
               return(0);
         }
      }
      if(smem_seek(st, entry_loc, SSEEK_SET) < 0)
         return(0);
      if(!found && !section->optional) /* Not found.  We are sad! */
         return(0);
   }
   else
   {
      if(!WriteStateChunk(st, section->name, section->sf))
         return(0);
   }

   return(1);
}

int MDFNSS_StateAction(
      void *st_p, int load, int data_only, SFORMAT *sf, const char *name)
{
   struct SSDescriptor love;
   StateMem *st      = (StateMem*)st_p;

   love.sf           = sf;
   love.name         = name;
   love.optional     = false;

   return(MDFNSS_StateAction_internal(st, load, 0, &love));
}

int MDFNSS_SaveSM(void *st_p, int a, int b, const void *c, const void *d,
      const void *e)
{
   uint32_t sizy;
   uint8_t header[32];
   StateMem *st = (StateMem*)st_p;
   static const char *header_magic = "MDFNSVST";
   int neowidth = 0, neoheight = 0;

   memset(header, 0, sizeof(header));
   memcpy(header, header_magic, 8);

   MDFN_en32lsb_(header + 16, MEDNAFEN_VERSION_NUMERIC);
   MDFN_en32lsb_(header + 24, neowidth);
   MDFN_en32lsb_(header + 28, neoheight);

   /* If the initial header write fails (out of memory), fail fast
    * rather than letting StateAction run and produce a corrupt
    * partial state. */
   if (smem_write(st, header, 32) != 32)
      return 0;

   if(!StateAction(st, 0, 0))
      return(0);

   sizy = st->loc;
   /* Patch the total size into the header. As with WriteStateChunk
    * these should always succeed, but propagate failure for
    * consistency rather than producing a state with a zero size
    * field. */
   if (smem_seek(st, 16 + 4, SSEEK_SET) < 0)
      return 0;
   if (smem_write32le(st, sizy) != 4)
      return 0;

   return(1);
}

int MDFNSS_LoadSM(void *st_p, int a, int b)
{
   uint8_t header[32];
   uint32_t stateversion;
   StateMem *st = (StateMem*)st_p;

   /* Zero the header buffer first - smem_read can return 0 on a
    * truncated input, leaving header uninitialized. The subsequent
    * memcmp against magic strings would then match against stack
    * garbage and we'd attempt to load whatever followed. */
   memset(header, 0, sizeof(header));

   if (smem_read(st, header, 32) != 32)
      return 0;

   if(memcmp(header, "MEDNAFENSVESTATE", 16) 
         && memcmp(header, "MDFNSVST", 8))
      return(0);

   stateversion = MDFN_de32lsb_(header + 16);

   return(StateAction(st, stateversion, 0));
}
