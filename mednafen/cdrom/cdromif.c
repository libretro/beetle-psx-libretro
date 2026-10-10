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
 *     the SBMutex.  The read now targets the slot directly.
 *
 *   - cdromif_c.h's CDIF_* shim functions are now ordinary
 *     definitions in this file; cdromif_c.h is gone.
 *
 * Lock-free MT path (no mutex anywhere on it):
 *
 *   - The two message queues were mutex + condvar rings.  They are
 *     strictly single-producer / single-consumer (emu thread -> read
 *     thread, read thread -> emu thread), so each is now a
 *     retro_waitable_spsc: a lock-free byte ring with an eventcount on
 *     each end for the sleeping side.  A write with nobody parked is a
 *     release store; the old queue silently dropped a message when its
 *     16 slots were full, this one waits for space instead.
 *
 *   - The sector ring was published under SBMutex and scanned under it,
 *     striding through 256 slots 2448 bytes apart to compare each
 *     slot's lba.  Each slot is now a seqlock: a compact array of
 *     per-slot atomic words (lba + 1 while the slot holds that sector,
 *     0 while it is being rewritten) is the only thing the emu thread
 *     scans - 1 KiB, sixteen cache lines - and a copy is validated by
 *     re-reading the word after the memcpy.  The read thread publishes
 *     with a release store; the emu thread parks on an eventcount
 *     (retro_eventcount: futex on Linux/Android, no lock on the
 *     notifier's side anywhere) only when the sector is not there yet.
 *
 *   - ra_lba / ra_count / SBWritePos / last_read_lba are read-thread
 *     private.  disc_toc is written by the read thread only while the
 *     emu thread is blocked in CDIF_Eject / CDIF_Open waiting for the
 *     DONE ack, which the queue orders; the same holds for the slot
 *     invalidation an eject performs.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <boolean.h>
#include <retro_miscellaneous.h>
#include <libretro.h>
#if HAVE_THREADS
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <retro_atomic.h>
#include <retro_waitable_spsc.h>
#include <features/features_cpu.h>
#endif

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

#if HAVE_THREADS
/* ------------------------------------------------------------------
 * CDIF_Queue - single-producer / single-consumer message ring.
 * Lock-free (retro_waitable_spsc); a full queue makes the producer
 * wait rather than drop, a message is never split.
 * ------------------------------------------------------------------ */

#define CDIF_QUEUE_SIZE 16
#define CDIF_MSG_BYTES  (sizeof(CDIF_Message))

typedef retro_waitable_spsc_t CDIF_Queue;

static bool CDIF_Queue_Init(CDIF_Queue *q)
{
   return retro_waitable_spsc_init(q, CDIF_QUEUE_SIZE * CDIF_MSG_BYTES);
}

static void CDIF_Queue_Free(CDIF_Queue *q)
{
   retro_waitable_spsc_free(q);
}

/* blocking: sleep until a message arrives (or the queue is cancelled).
 * non-blocking: take one if present. */
static bool CDIF_Queue_Read(CDIF_Queue *q, CDIF_Message *out, bool blocking)
{
   if (blocking)
   {
      while (!retro_waitable_spsc_wait_readable(q, CDIF_MSG_BYTES, 1000000))
         if (retro_waitable_spsc_cancelled(q))
            return false;
   }
   else if (retro_spsc_read_avail(&q->queue) < CDIF_MSG_BYTES)
      return false;

   return retro_waitable_spsc_read(q, out, CDIF_MSG_BYTES) == CDIF_MSG_BYTES;
}

static void CDIF_Queue_Write(CDIF_Queue *q, const CDIF_Message *msg)
{
   /* SPSC: nobody else can take the space we are told is free. */
   while (!retro_waitable_spsc_wait_writable(q, CDIF_MSG_BYTES, 1000000))
      if (retro_waitable_spsc_cancelled(q))
         return;
   retro_waitable_spsc_write(q, msg, CDIF_MSG_BYTES);
}
#endif

/* ------------------------------------------------------------------
 * Sector-ring slot.
 * ------------------------------------------------------------------ */

#define SBSIZE 256
#define SECTOR_RAW_BYTES (2352 + 96)

typedef struct CDIF_Sector_Buffer
{
   bool     error;
   uint8_t  data[SECTOR_RAW_BYTES];
} CDIF_Sector_Buffer;

/* Per-slot published state, kept apart from the slot data so the emu
 * thread's lookup touches 1 KiB instead of 256 x 2448 bytes:
 *   0        the slot is empty or being rewritten
 *   lba + 1  the slot holds sector lba, complete
 * Doubles as the slot's seqlock word: a reader that finds lba + 1,
 * copies, and finds the same value afterwards copied a stable sector. */
#define CDIF_SLOT_EMPTY 0
#define CDIF_SLOT_TAG(lba) ((int)((lba) + 1u))

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

#if HAVE_THREADS
   /* MT-only */
   sthread_t *CDReadThread;
   CDIF_Queue ReadThreadQueue;   /* emu thread -> read thread */
   CDIF_Queue EmuThreadQueue;    /* read thread -> emu thread (DONE acks) */

   CDIF_Sector_Buffer SectorBuffers[SBSIZE];
   retro_atomic_int_t SlotTag[SBSIZE];    /* see CDIF_SLOT_TAG */
   retro_eventcount_t SBEvent;            /* "a sector was published" */
   uint32_t   SBWritePos;                 /* read-thread private */

   uint32_t   ra_lba;                     /* read-thread private */
   int        ra_count;                   /* read-thread private */
   uint32_t   last_read_lba;              /* read-thread private */
#endif
};

#if HAVE_THREADS

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
      /* The emu thread is blocked waiting for our DONE ack (or, at
       * start-up, for the first one), so nobody is reading the ring. */
      for (i = 0; i < SBSIZE; i++)
         retro_atomic_store_relaxed_int(&cdif->SlotTag[i], CDIF_SLOT_EMPTY);
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
         uint32_t            pos  = cdif->SBWritePos;
         CDIF_Sector_Buffer *slot = &cdif->SectorBuffers[pos];

         /* Seqlock write side. Empty the slot's tag, then a full fence
          * so the tag store is ordered before the data writes: a reader
          * that sees any of the new bytes has, on re-reading the tag,
          * seen it change (to EMPTY or to the new tag). */
         retro_atomic_store_relaxed_int(&cdif->SlotTag[pos], CDIF_SLOT_EMPTY);
         retro_atomic_thread_fence_seq_cst();

         /* Read directly into the slot - saves a 2448-byte memcpy. */
         cdif->disc_cdaccess->Read_Raw_Sector(cdif->disc_cdaccess, slot->data,
               cdif->ra_lba);
         slot->error = false;

         /* Publish: the release orders the data before the tag. */
         retro_atomic_store_release_int(&cdif->SlotTag[pos],
               CDIF_SLOT_TAG(cdif->ra_lba));
         cdif->SBWritePos = (pos + 1) % SBSIZE;
         retro_eventcount_notify(&cdif->SBEvent);

         cdif->ra_lba++;
         cdif->ra_count--;
      }
   }

   return 1;
}

/* Seqlock read side: copy sector lba out of the ring if some slot holds
 * it. A slot whose tag changes underneath the copy is rejected and the
 * scan continues (the read thread rewrites a given slot once per 256
 * sectors, so this practically never repeats). */
static bool CDIF_MT_FindSector(CDIF *cdif, uint8_t *buf, uint32_t lba,
      bool *error)
{
   const int tag = CDIF_SLOT_TAG(lba);
   int i;

   for (i = 0; i < SBSIZE; i++)
   {
      CDIF_Sector_Buffer *slot;

      if (retro_atomic_load_acquire_int(&cdif->SlotTag[i]) != tag)
         continue;

      slot = &cdif->SectorBuffers[i];
      memcpy(buf, slot->data, SECTOR_RAW_BYTES);
      *error = slot->error;

      /* Order the copy's loads before the re-check of the tag. */
      retro_atomic_thread_fence_acquire();
      if (retro_atomic_load_relaxed_int(&cdif->SlotTag[i]) == tag)
         return true;
   }

   return false;
}
#endif /* HAVE_THREADS */

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
#if HAVE_THREADS
   if (cdif->is_mt)
   {
      CDIF_Message msg;
      msg.message = CDIF_MSG_READ_SECTOR;
      msg.args[0] = lba;
      msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);
   }
#endif
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
   {
      /* Read past the lead-out (e.g. CD-DA playback running off the end
       * of the last track as it loops). Zero the buffer before
       * returning failure: callers - notably the CDC's CD-DA play path -
       * decode buf regardless of the return value, so leaving the
       * previous sector's contents here plays back as corrupted audio
       * when the final audio track loops (issue #924). Matches upstream
       * and the PW-only reader below. */
      memset(buf, 0, SECTOR_RAW_BYTES);
      return false;
   }

#if HAVE_THREADS
   if (cdif->is_mt)
   {
      CDIF_Message msg;
      bool error_condition = false;
      retro_time_t deadline = 0;

      msg.message = CDIF_MSG_READ_SECTOR;
      msg.args[0] = lba;
      msg.args[1] = msg.args[2] = msg.args[3] = 0;
      CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);

      if (timeout_us >= 0)
         deadline = cpu_features_get_time_usec() + (retro_time_t)timeout_us;

      /* Fast path: the read-ahead has usually already landed the sector,
       * and the lookup is a scan of the tag array plus one memcpy - no
       * lock, no syscall. Only when it is not there yet do we register
       * with the eventcount, re-check (so a publish between the two
       * scans cannot be missed), and park. */
      for (;;)
      {
         int key;
         int64_t remaining;

         if (CDIF_MT_FindSector(cdif, buf, lba, &error_condition))
            break;

         key = retro_eventcount_prepare_wait(&cdif->SBEvent);
         if (CDIF_MT_FindSector(cdif, buf, lba, &error_condition))
         {
            retro_eventcount_cancel_wait(&cdif->SBEvent);
            break;
         }

         if (timeout_us < 0)
         {
            retro_eventcount_commit_wait(&cdif->SBEvent, key);
            continue;
         }

         remaining = (int64_t)(deadline - cpu_features_get_time_usec());
         if (remaining <= 0)
         {
            retro_eventcount_cancel_wait(&cdif->SBEvent);
            error_condition = true;
            memset(buf, 0, SECTOR_RAW_BYTES);
            break;
         }
         retro_eventcount_commit_wait_timeout(&cdif->SBEvent, key, remaining);
      }

      return !error_condition;
   }
#endif
   (void)timeout_us;
   cdif->disc_cdaccess->Read_Raw_Sector(cdif->disc_cdaccess, buf, lba);
   return true;
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

   if (hint_fullread)
      CDIF_HintReadSector(cdif, lba); /* no-op on the ST path */

   return cdif->disc_cdaccess->Read_Raw_PW(cdif->disc_cdaccess, buf, lba);
}

bool CDIF_Eject(CDIF *cdif, bool eject_status)
{
   if (cdif->UnrecoverableError)
      return false;

#if HAVE_THREADS
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
#endif
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

#if HAVE_THREADS
   if (cdif->is_mt)
   {
      if (cdif->CDReadThread)
      {
         CDIF_Message msg;

         msg.message = CDIF_MSG_DIEDIEDIE;
         msg.args[0] = msg.args[1] = msg.args[2] = msg.args[3] = 0;
         CDIF_Queue_Write(&cdif->ReadThreadQueue, &msg);

         sthread_join(cdif->CDReadThread);
         cdif->CDReadThread = NULL;
      }

      retro_eventcount_free(&cdif->SBEvent);
      CDIF_Queue_Free(&cdif->ReadThreadQueue);
      CDIF_Queue_Free(&cdif->EmuThreadQueue);
   }
#endif

   if (cdif->disc_cdaccess)
      cdif->disc_cdaccess->destroy(cdif->disc_cdaccess);
   cdif->disc_cdaccess = NULL;

   free(cdif);
}

/* ------------------------------------------------------------------
 * Construction.
 * ------------------------------------------------------------------ */

#if HAVE_THREADS
static CDIF *CDIF_Open_MT(CDAccess *cda)
{
   CDIF        *cdif;
   CDIF_Message ack;
   unsigned     i;

   cdif = (CDIF *)calloc(1, sizeof(*cdif));
   if (!cdif)
      return NULL;

   cdif->is_mt              = true;
   cdif->UnrecoverableError = false;
   cdif->DiscEjected        = false;
   cdif->disc_cdaccess      = cda;
   TOC_Clear(&cdif->disc_toc);

   for (i = 0; i < SBSIZE; i++)
      retro_atomic_int_init(&cdif->SlotTag[i], CDIF_SLOT_EMPTY);

   if (!CDIF_Queue_Init(&cdif->ReadThreadQueue) ||
       !CDIF_Queue_Init(&cdif->EmuThreadQueue) ||
       !retro_eventcount_init(&cdif->SBEvent))
   {
      cdif->UnrecoverableError = true;
      return cdif; /* CDIF_Close frees what was set up */
   }

   cdif->CDReadThread = sthread_create(
         (void (*)(void *))CDIF_ReadThread, cdif);
   if (!cdif->CDReadThread)
   {
      cdif->UnrecoverableError = true;
      return cdif;
   }

   /* Wait for the read thread to finish initial TOC parsing. */
   CDIF_Queue_Read(&cdif->EmuThreadQueue, &ack, true);

   return cdif;
}
#endif

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

