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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>

#include <retro_assert.h>
#include <compat/msvc.h>
#include <compat/strl.h>
#include <retro_inline.h>

#include "state.h"

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

static int32_t smem_read(StateMem *st, void *buffer, uint32_t len)
{
   if ((len + st->loc) > st->len)
      return 0;

   memcpy(buffer, st->data + st->loc, len);
   st->loc += len;

   return(len);
}

static int32_t smem_write(StateMem *st, void *buffer, uint32_t len)
{
   if ((len + st->loc) > st->malloced)
   {
      uint32_t newsize = (st->malloced >= 32768) ? st->malloced : (st->initial_malloc ? st->initial_malloc : 32768);

      while(newsize < (len + st->loc))
         newsize  *= 2;
      st->data     = (uint8_t *)realloc(st->data, newsize);
      st->malloced = newsize;
   }
   memcpy(st->data + st->loc, buffer, len);
   st->loc += len;

   if (st->loc > st->len)
      st->len = st->loc;

   return(len);
}

static int32_t smem_seek(StateMem *st, uint32_t offset, int whence)
{
   switch(whence)
   {
      case SEEK_SET:
         st->loc = offset;
         break;
      case SEEK_END:
         st->loc = st->len - offset;
         break;
      case SEEK_CUR:
         st->loc += offset;
         break;
   }

   if(st->loc > st->len)
   {
      st->loc = st->len;
      return -1;
   }

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

static bool SubWrite(StateMem *st, SFORMAT *sf, const char *name_prefix)
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
         if(!SubWrite(st, (SFORMAT *)sf->v, name_prefix))
            return(0);

         sf++;
         continue;
      }

      int32_t bytesize = sf->size;

      /* exclude text labels from fast savestates */
      if (!FastSaveStates)
      {
         char nameo[1 + 256];
         int slen;

         if (name_prefix)
            slen       = snprintf(
                  nameo + 1, 256, "%s%s", name_prefix, sf->name);
         else
         {
            slen       = strlcpy(nameo + 1, sf->name, 255);
            nameo[256] = 0;
         }
         nameo[0]      = slen;

         smem_write(st, nameo, 1 + nameo[0]);
      }
      smem_write32le(st, bytesize);

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
            smem_write(st, &tmp_bool, 1);
         }
      }
      else
         smem_write(st, (uint8_t *)sf->v, bytesize);

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

   smem_write(st, sname_tmp, 32);
   /* We'll come back and write this later. */
   smem_write32le(st, 0);                

   data_start_pos = st->loc;

   if(!SubWrite(st, sf, NULL))
      return(0);

   end_pos = st->loc;

   smem_seek(st, data_start_pos - 4, SEEK_SET);
   smem_write32le(st, end_pos - data_start_pos);
   smem_seek(st, end_pos, SEEK_SET);

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

   while (st->loc < (temp + size))
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

      smem_read32le(st, &recorded_size);

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
            if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
               return(0);
         }
         else
         {
            smem_read(st, (uint8_t *)tmp->v, expected_size);

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
         if(smem_seek(st, recorded_size, SEEK_CUR) < 0)
            return(0);
      }
   }

   assert(st->loc == (temp + size));
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
      uint32_t total = 0;

      while(smem_read(st, (uint8_t *)sname, 32) == 32)
      {
         if(smem_read32le(st, &tmp_size) != 4)
            return(0);

         total += tmp_size + 32 + 4;

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
            if(smem_seek(st, tmp_size, SEEK_CUR) < 0)
               return(0);
         }
      }
      if(smem_seek(st, -total, SEEK_CUR) < 0)
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
   smem_write(st, header, 32);

   if(!StateAction(st, 0, 0))
      return(0);

   sizy = st->loc;
   smem_seek(st, 16 + 4, SEEK_SET);
   smem_write32le(st, sizy);

   return(1);
}

int MDFNSS_LoadSM(void *st_p, int a, int b)
{
   uint8_t header[32];
   uint32_t stateversion;
   StateMem *st = (StateMem*)st_p;

   smem_read(st, header, 32);

   if(memcmp(header, "MEDNAFENSVESTATE", 16) 
         && memcmp(header, "MDFNSVST", 8))
      return(0);

   stateversion = MDFN_de32lsb_(header + 16);

   return(StateAction(st, stateversion, 0));
}
