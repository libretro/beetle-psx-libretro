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

/*
 * cdromif: CDIF "interface" layer between the PS1 emulated CD-ROM
 * controller (psx/cdc.c) and the disc-format-specific CDAccess
 * backends (CDAccess_Image / _CCD / _CHD / _PBP).
 *
 * Two flavours, selected at CDIF_Open time based on the
 * image_memcache flag:
 *
 *   MT (multi-threaded): a background thread reads ahead of the
 *      emulated drive, smoothing out disk-I/O latency.  Sectors
 *      are deposited into a 256-slot ring buffer; ReadRawSector
 *      blocks until the requested LBA appears in the buffer.
 *
 *   ST (single-threaded): synchronous; ReadRawSector calls into
 *      the CDAccess backend directly on the emu thread.  Used when
 *      image_memcache is true (PBP / fully-cached images).
 *
 * Historical baggage removed in the C conversion:
 *
 *   - class CDIF (abstract base) + CDIF_MT / CDIF_ST (concrete).
 *     Replaced with a single struct CDIF tagged by `is_mt`.  Only
 *     one of the two field-sets is live per instance; `is_mt`
 *     selects.  Removes the vtable indirection and the heap class
 *     hierarchy without losing any functionality.
 *
 *   - CDIF_Message had three constructors including one taking a
 *     std::string (for FATAL_ERROR diagnostic text).  No code path
 *     ever constructed a string-bearing message; the CDIF_MSG_INFO
 *     and CDIF_MSG_FATAL_ERROR codes were dead.  Constructor and
 *     std::string field both gone.  Message is a 5-uint32 POD.
 *
 *   - CDIF_Queue was std::queue<CDIF_Message>.  In practice the
 *     queue depth is 1-3 messages in flight (DIE / EJECT / one
 *     in-flight READ_SECTOR per HintReadSector).  Replaced with
 *     a fixed-size 16-slot ring buffer; same producer/consumer
 *     semantics, no heap allocation per push, no dynamic resizing.
 *
 *   - The read thread used to read each sector into a 2448-byte
 *     stack tmpbuf and then memcpy it into the ring slot under
 *     the SBMutex.  The sector buffer's `valid` flag is now flipped
 *     to false outside the lock, the read targets the slot directly,
 *     and `valid` flips back to true (with the lba update and a
 *     wake) under the lock.  Saves one 2448-byte memcpy per sector.
 *
 *   - cdromif_c.h's CDIF_* shim functions are now ordinary
 *     definitions in this file; cdromif_c.h is gone.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <boolean.h>
#include <rthreads/rthreads.h>
#include <retro_miscellaneous.h>
#include <libretro.h>

#include "../mednafen.h"
#include "../error.h"
#include "CDUtility.h"
#include "CDAccess.h"
#include "cdromif.h"

extern retro_log_printf_t log_cb;

/* ------------------------------------------------------------------
 * CDIF_Message - read-thread protocol message.
 * ------------------------------------------------------------------ */

enum
{
   CDIF_MSG_DONE = 0,
   CDIF_MSG_DIEDIEDIE,
   CDIF_MSG_READ_SECTOR,
   CDIF_MSG_EJECT
};

typedef struct CDIF_Message
{
   unsigned message;
   uint32_t args[4];
} CDIF_Message;

/* ------------------------------------------------------------------
 * CDIF_Queue - fixed-capacity ring buffer of CDIF_Message.
 * Single-producer, single-consumer; mutex protects head/tail/count.
 * ------------------------------------------------------------------ */

#define CDIF_QUEUE_SIZE 16

typedef struct CDIF_Queue
{
   CDIF_Message ring[CDIF_QUEUE_SIZE];
   unsigned     head;
   unsigned     tail;
   unsigned     count;
   slock_t     *mutex;
   scond_t     *cond;
} CDIF_Queue;

static void CDIF_Queue_Init(CDIF_Queue *q)
{
   q->head  = 0;
   q->tail  = 0;
   q->count = 0;
   q->mutex = slock_new();
   q->cond  = scond_new();
}

static void CDIF_Queue_Free(CDIF_Queue *q)
{
   if (q->mutex)
      slock_free(q->mutex);
   if (q->cond)
      scond_free(q->cond);
   q->mutex = NULL;
   q->cond  = NULL;
}

static bool CDIF_Queue_Read(CDIF_Queue *q, CDIF_Message *out, bool blocking)
{
   bool ret = true;

   slock_lock(q->mutex);

   if (blocking)
   {
      while (q->count == 0)
         scond_wait(q->cond, q->mutex);
   }

   if (q->count == 0)
      ret = false;
   else
   {
      *out = q->ring[q->head];
      q->head = (q->head + 1) & (CDIF_QUEUE_SIZE - 1);
      q->count--;
   }

   slock_unlock(q->mutex);

   return ret;
}

static void CDIF_Queue_Write(CDIF_Queue *q, const CDIF_Message *msg)
{
   slock_lock(q->mutex);

   if (q->count < CDIF_QUEUE_SIZE)
   {
      q->ring[q->tail] = *msg;
      q->tail = (q->tail + 1) & (CDIF_QUEUE_SIZE - 1);
      q->count++;
   }

   scond_signal(q->cond);
   slock_unlock(q->mutex);
}

/* ------------------------------------------------------------------
 * Sector-ring slot.
 * ------------------------------------------------------------------ */

#define SBSIZE 256
#define SECTOR_RAW_BYTES (2352 + 96)

typedef struct CDIF_Sector_Buffer
{
   bool     valid;
   bool     error;
   uint32_t lba;
   uint8_t  data[SECTOR_RAW_BYTES];
} CDIF_Sector_Buffer;

/* ------------------------------------------------------------------
 * CDIF - one disc instance.
 * ------------------------------------------------------------------ */

struct CDIF
{
   bool       is_mt;
   bool       UnrecoverableError;
   bool       DiscEjected;
   TOC        disc_toc;
   CDAccess  *disc_cdaccess;

   /* MT-only */
   sthread_t *CDReadThread;
   CDIF_Queue ReadThreadQueue;
   CDIF_Queue EmuThreadQueue;

   CDIF_Sector_Buffer SectorBuffers[SBSIZE];
   uint32_t   SBWritePos;
   slock_t   *SBMutex;
   scond_t   *SBCond;

   uint32_t   ra_lba;
   int        ra_count;
   uint32_t   last_read_lba;
};

/* ------------------------------------------------------------------
 * MT read-thread implementation.
 * ------------------------------------------------------------------ */

static bool CDIF_RT_EjectDisc(CDIF *cdif, bool eject_status,
      bool skip_actual_eject)
{
   bool old_de = cdif->DiscEjected;

   cdif->DiscEjected = eject_status;

   if (old_de != cdif->DiscEjected)
   {
      unsigned i;

      if (!skip_actual_eject)
         cdif->disc_cdaccess->Eject(cdif->disc_cdaccess, eject_status);

      if (!eject_status)
      {
         cdif->disc_cdaccess->Read_TOC(cdif->disc_cdaccess, &cdif->disc_toc);

         if (cdif->disc_toc.first_track < 1
               || cdif->disc_toc.last_track > 99
               || cdif->disc_toc.first_track > cdif->disc_toc.last_track)
         {
            log_cb(RETRO_LOG_ERROR,
                  "TOC first(%d)/last(%d) track numbers bad.\n",
                  cdif->disc_toc.first_track, cdif->disc_toc.last_track);
            return false;
         }
      }

      cdif->SBWritePos    = 0;
      cdif->ra_lba        = 0;
      cdif->ra_count      = 0;
      cdif->last_read_lba = ~0U;
      for (i = 0; i < SBSIZE; i++)
         cdif->SectorBuffers[i].valid = false;
   }

   return true;
}

static int CDIF_ReadThread(void *v_arg)
{
   CDIF *cdif    = (CDIF *)v_arg;
   bool  Running = true;
   CDIF_Message done_msg;

   done_msg.message = CDIF_MSG_DONE;
   memset(done_msg.args, 0, sizeof(done_msg.args));

   cdif->DiscEjected   = true;
   cdif->SBWritePos    = 0;
   cdif->ra_lba        = 0;
   cdif->ra_count      = 0;
   cdif->last_read_lba = ~0U;

   if (!CDIF_RT_EjectDisc(cdif, false, true))
   {
      cdif->UnrecoverableError = true;
      CDIF_Queue_Write(&cdif->EmuThreadQueue, &done_msg);
      return 0;
   }

   CDIF_Queue_Write(&cdif->EmuThreadQueue, &done_msg);

   while (Running)
   {
      CDIF_Message msg;
      bool got_msg = CDIF_Queue_Read(&cdif->ReadThreadQueue, &msg,
            cdif->ra_count ? false : true);

      if (got_msg)
      {
         switch (msg.message)
         {
            case CDIF_MSG_DIEDIEDIE:
               Running = false;
               break;

            case CDIF_MSG_EJECT:
               CDIF_RT_EjectDisc(cdif, (bool)msg.args[0], false);
               CDIF_Queue_Write(&cdif->EmuThreadQueue, &done_msg);
               break;

            case CDIF_MSG_READ_SECTOR:
            {
               static const int       max_ra =  16;
               static const int   initial_ra =   1;
               static const int speedmult_ra =   2;
               uint32_t              new_lba = msg.args[0];

               if (cdif->last_read_lba != ~0U
                     && new_lba == cdif->last_read_lba + 1)
               {
                  int how_far_ahead = (int)(cdif->ra_lba - new_lba);

                  if (how_far_ahead <= max_ra)
                  {
                     int _v = 1 + max_ra - how_far_ahead;
                     cdif->ra_count = (speedmult_ra < _v) ? speedmult_ra : _v;
                  }
                  else
                     cdif->ra_count++;
               }
               else if (new_lba != cdif->last_read_lba)
               {
                  cdif->ra_lba   = new_lba;
                  cdif->ra_count = initial_ra;
               }

               cdif->last_read_lba = new_lba;
               break;
            }
         }
      }

      if (cdif->ra_count
            && cdif->ra_lba == cdif->disc_toc.tracks[100].lba)
         cdif->ra_count = 0;

      if (cdif->ra_count)
      {
         CDIF_Sector_Buffer *slot = &cdif->SectorBuffers[cdif->SBWritePos];

         /* Mark stale before reading so a concurrent ReadRawSector
          * never returns half-written data from this slot. */
         slock_lock(cdif->SBMutex);
         slot->valid = false;
         slock_unlock(cdif->SBMutex);

         /* Read directly into the slot - saves a 2448-byte memcpy. */
         cdif->disc_cdaccess->Read_Raw_Sector(cdif->disc_cdaccess, slot->data,
               cdif->ra_lba);

         slock_lock(cdif->SBMutex);
         slot->lba   = cdif->ra_lba;
         slot->error = false;
         slot->valid = true;
         cdif->SBWritePos = (cdif->SBWritePos + 1) % SBSIZE;
         scond_signal(cdif->SBCond);
         slock_unlock(cdif->SBMutex);

         cdif->ra_lba++;
         cdif->ra_count--;
      }
   }

   return 1;
}

/* ------------------------------------------------------------------
 * Public API.
 * ------------------------------------------------------------------ */

void CDIF_ReadTOC(CDIF *cdif, TOC *out)
{
   *out = cdif->disc_toc;
}

void CDIF_HintReadSector(CDIF *cdif, uint32_t lba)
{
   if (cdif->UnrecoverableError)
      return;
   if (cdif->is_mt)
   {
      CDIF_Message msg;
      msg.message = CDIF_MSG_READ_SECTOR;
      msg.args[0] = lba;
      msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);
   }
}

bool CDIF_ReadRawSector(CDIF *cdif, uint8_t *buf, uint32_t lba,
      int64_t timeout_us)
{
   if (cdif->UnrecoverableError)
   {
      memset(buf, 0, SECTOR_RAW_BYTES);
      return false;
   }

   if (lba >= cdif->disc_toc.tracks[100].lba)
      return false;

   if (!cdif->is_mt)
   {
      (void)timeout_us;
      cdif->disc_cdaccess->Read_Raw_Sector(cdif->disc_cdaccess, buf, lba);
      return true;
   }
   else
   {
      CDIF_Message msg;
      bool found           = false;
      bool error_condition = false;

      msg.message = CDIF_MSG_READ_SECTOR;
      msg.args[0] = lba;
      msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);

      slock_lock(cdif->SBMutex);

      do
      {
         int i;
         for (i = 0; i < SBSIZE; i++)
         {
            CDIF_Sector_Buffer *slot = &cdif->SectorBuffers[i];
            if (slot->valid && slot->lba == lba)
            {
               error_condition = slot->error;
               memcpy(buf, slot->data, SECTOR_RAW_BYTES);
               found = true;
               break;
            }
         }

         if (!found)
         {
            if (timeout_us >= 0)
            {
               if (!scond_wait_timeout(cdif->SBCond, cdif->SBMutex,
                        timeout_us))
               {
                  error_condition = true;
                  memset(buf, 0, SECTOR_RAW_BYTES);
                  break;
               }
            }
            else
               scond_wait(cdif->SBCond, cdif->SBMutex);
         }
      } while (!found);

      slock_unlock(cdif->SBMutex);

      return !error_condition;
   }
}

bool CDIF_ReadRawSectorPWOnly(CDIF *cdif, uint8_t *buf, uint32_t lba,
      bool hint_fullread)
{
   if (cdif->UnrecoverableError)
   {
      memset(buf, 0, 96);
      return false;
   }

   if (lba >= cdif->disc_toc.tracks[100].lba)
   {
      memset(buf, 0, 96);
      return false;
   }

   if (cdif->is_mt && hint_fullread)
      CDIF_HintReadSector(cdif, lba);

   return cdif->disc_cdaccess->Read_Raw_PW(cdif->disc_cdaccess, buf, lba);
}

bool CDIF_Eject(CDIF *cdif, bool eject_status)
{
   if (cdif->UnrecoverableError)
      return false;

   if (cdif->is_mt)
   {
      CDIF_Message msg;
      CDIF_Message ack;
      msg.message = CDIF_MSG_EJECT;
      msg.args[0] = eject_status ? 1u : 0u;
      msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);
      CDIF_Queue_Read(&cdif->EmuThreadQueue, &ack, true);
      return true;
   }
   else
   {
      bool old_de = cdif->DiscEjected;

      cdif->DiscEjected = eject_status;

      if (old_de != cdif->DiscEjected)
      {
         cdif->disc_cdaccess->Eject(cdif->disc_cdaccess, eject_status);

         if (!eject_status)
         {
            cdif->disc_cdaccess->Read_TOC(cdif->disc_cdaccess, &cdif->disc_toc);

            if (cdif->disc_toc.first_track < 1
                  || cdif->disc_toc.last_track > 99
                  || cdif->disc_toc.first_track > cdif->disc_toc.last_track)
            {
               log_cb(RETRO_LOG_ERROR,
                     "TOC first(%d)/last(%d) track numbers bad.\n",
                     cdif->disc_toc.first_track,
                     cdif->disc_toc.last_track);
               return false;
            }
         }
      }

      return true;
   }
}

bool CDIF_ValidateRawSector(uint8_t *buf)
{
   int mode = buf[12 + 3];

   if (mode != 0x1 && mode != 0x2)
      return false;

   if (!edc_lec_check_and_correct(buf, mode == 2))
      return false;

   return true;
}

int CDIF_ReadSector(CDIF *cdif, uint8_t *pBuf, uint32_t lba, uint32_t nSectors)
{
   /* Stack scratch for the raw 2352+96 sector. Hoisted above the
    * loop to make the intent (one reusable buffer across all
    * iterations) explicit; gcc already frame-allocated this once
    * per function call regardless of declaration position. */
   uint8_t tmpbuf[SECTOR_RAW_BYTES];
   int ret = 0;

   if (cdif->UnrecoverableError)
      return 0;

   while (nSectors--)
   {
      int     mode;

      if (!CDIF_ReadRawSector(cdif, tmpbuf, lba, -1))
         return 0;

      if (!CDIF_ValidateRawSector(tmpbuf))
         return 0;

      mode = tmpbuf[12 + 3];

      if (!ret)
         ret = mode;

      switch (mode)
      {
         case 1:
            memcpy(pBuf, &tmpbuf[12 + 4], 2048);
            break;
         case 2:
            memcpy(pBuf, &tmpbuf[12 + 4 + 8], 2048);
            break;
         default:
            return 0;
      }

      pBuf += 2048;
      lba++;
   }

   return ret;
}

void CDIF_Close(CDIF *cdif)
{
   if (!cdif)
      return;

   if (cdif->is_mt)
   {
      CDIF_Message msg;

      msg.message = CDIF_MSG_DIEDIEDIE;
      msg.args[0] = msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);

      sthread_join(cdif->CDReadThread);

      if (cdif->SBMutex)
         slock_free(cdif->SBMutex);
      if (cdif->SBCond)
         scond_free(cdif->SBCond);
      cdif->SBMutex = NULL;
      cdif->SBCond  = NULL;

      CDIF_Queue_Free(&cdif->ReadThreadQueue);
      CDIF_Queue_Free(&cdif->EmuThreadQueue);
   }

   if (cdif->disc_cdaccess)
      cdif->disc_cdaccess->destroy(cdif->disc_cdaccess);
   cdif->disc_cdaccess = NULL;

   free(cdif);
}

/* ------------------------------------------------------------------
 * Construction.
 * ------------------------------------------------------------------ */

static CDIF *CDIF_Open_MT(CDAccess *cda)
{
   CDIF        *cdif;
   CDIF_Message ack;

   cdif = (CDIF *)calloc(1, sizeof(*cdif));
   if (!cdif)
      return NULL;

   cdif->is_mt              = true;
   cdif->UnrecoverableError = false;
   cdif->DiscEjected        = false;
   cdif->disc_cdaccess      = cda;
   TOC_Clear(&cdif->disc_toc);

   CDIF_Queue_Init(&cdif->ReadThreadQueue);
   CDIF_Queue_Init(&cdif->EmuThreadQueue);

   cdif->SBMutex = slock_new();
   cdif->SBCond  = scond_new();

   cdif->CDReadThread = sthread_create(
         (void (*)(void *))CDIF_ReadThread, cdif);

   /* Wait for the read thread to finish initial TOC parsing. */
   CDIF_Queue_Read(&cdif->EmuThreadQueue, &ack, true);

   return cdif;
}

static CDIF *CDIF_Open_ST(CDAccess *cda)
{
   CDIF *cdif = (CDIF *)calloc(1, sizeof(*cdif));

   if (!cdif)
      return NULL;

   cdif->is_mt              = false;
   cdif->UnrecoverableError = false;
   cdif->DiscEjected        = false;
   cdif->disc_cdaccess      = cda;
   TOC_Clear(&cdif->disc_toc);

   if (!cda)
   {
      cdif->UnrecoverableError = true;
      return cdif;
   }

   cda->Read_TOC(cda, &cdif->disc_toc);

   if (cdif->disc_toc.first_track < 1
         || cdif->disc_toc.last_track > 99
         || cdif->disc_toc.first_track > cdif->disc_toc.last_track)
   {
      log_cb(RETRO_LOG_ERROR,
            "TOC first(%d)/last(%d) track numbers bad.\n",
            cdif->disc_toc.first_track, cdif->disc_toc.last_track);
      cdif->UnrecoverableError = true;
   }

   return cdif;
}

CDIF *CDIF_Open(bool *success, const char *path,
      const bool is_device, bool image_memcache)
{
   CDAccess *cda;
   CDIF     *cdif;

   (void)is_device;

   *success = true;
   cda = cdaccess_open_image(success, path, image_memcache);

   if (!*success)
   {
      if (cda)
         cda->destroy(cda);
      return NULL;
   }

#if HAVE_THREADS
   if (!image_memcache)
   {
      cdif = CDIF_Open_MT(cda);
      if (!cdif || cdif->UnrecoverableError)
      {
         *success = false;
         CDIF_Close(cdif);
         return NULL;
      }
      return cdif;
   }
#endif

   cdif = CDIF_Open_ST(cda);
   if (!cdif || cdif->UnrecoverableError)
   {
      *success = false;
      CDIF_Close(cdif);
      return NULL;
   }
   return cdif;
}

