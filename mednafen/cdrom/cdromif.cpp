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

#include "../mednafen.h"
#include "../error.h"
#include <string.h>
#include <sys/types.h>
#include "cdromif.h"
#include "CDAccess.h"
#include "../general.h"

#include <algorithm>
#include <queue>

#include <boolean.h>
#include <rthreads/rthreads.h>
#include <retro_miscellaneous.h>

#include <libretro.h>

extern retro_log_printf_t log_cb;

enum
{
   // Status/Error messages
   CDIF_MSG_DONE = 0,
   CDIF_MSG_INFO,
   CDIF_MSG_FATAL_ERROR,

   // Command messages.
   CDIF_MSG_DIEDIEDIE,
   CDIF_MSG_READ_SECTOR,
   CDIF_MSG_EJECT
};

class CDIF_Message
{
   public:

      CDIF_Message();
      CDIF_Message(unsigned int message_, uint32 arg0 = 0, uint32 arg1 = 0, uint32 arg2 = 0, uint32 arg3 = 0);
      CDIF_Message(unsigned int message_, const std::string &str);
      ~CDIF_Message();

      unsigned int message;
      uint32 args[4];
      void *parg;
      std::string str_message;
};

#if HAVE_THREADS

class CDIF_Queue
{
   public:

      CDIF_Queue();
      ~CDIF_Queue();

      bool Read(CDIF_Message *message, bool blocking = true);

      void Write(const CDIF_Message &message);

   private:
      std::queue<CDIF_Message> ze_queue;
      slock_t *ze_mutex;
      scond_t *ze_cond;
};


typedef struct
{
   bool valid;
   bool error;
   uint32 lba;
   uint8 data[2352 + 96];
} CDIF_Sector_Buffer;

/* TODO: prohibit copy constructor */
class CDIF_MT : public CDIF
{
   public:

      CDIF_MT(CDAccess *cda);
      virtual ~CDIF_MT();

      virtual void HintReadSector(uint32 lba);
      virtual bool ReadRawSector(uint8 *buf, uint32 lba, int64 timeout_us);
      virtual bool ReadRawSectorPWOnly(uint8 *buf, uint32 lba, bool hint_fullread);

      // Return true if operation succeeded or it was a NOP(either due to not being implemented, or the current status matches eject_status).
      // Returns false on failure(usually drive error of some kind; not completely fatal, can try again).
      virtual bool Eject(bool eject_status);

      // FIXME: Semi-private:
      int ReadThreadStart(void);

   private:

      CDAccess  *disc_cdaccess;
      sthread_t *CDReadThread;

      // Queue for messages to the read thread.
      CDIF_Queue ReadThreadQueue;

      // Queue for messages to the emu thread.
      CDIF_Queue EmuThreadQueue;


      enum { SBSize = 256 };
      CDIF_Sector_Buffer SectorBuffers[SBSize];

      uint32 SBWritePos;

      slock_t *SBMutex;
      scond_t *SBCond;

      //
      // Read-thread-only:
      //
      bool RT_EjectDisc(bool eject_status, bool skip_actual_eject = false);

      uint32 ra_lba;
      int ra_count;
      uint32 last_read_lba;
};

#endif /* HAVE_THREAD */

/* TODO: prohibit copy constructor */
class CDIF_ST : public CDIF
{
   public:

      CDIF_ST(CDAccess *cda);
      virtual ~CDIF_ST();

      virtual void HintReadSector(uint32 lba);
      virtual bool ReadRawSector(uint8 *buf, uint32 lba, int64 timeout_us);
      virtual bool ReadRawSectorPWOnly(uint8 *buf, uint32 lba, bool hint_fullread);
      virtual bool Eject(bool eject_status);

   private:
      CDAccess *disc_cdaccess;
};

CDIF::CDIF() : UnrecoverableError(false), DiscEjected(false)
{
   TOC_Clear(&disc_toc);
}

CDIF::~CDIF()
{

}


CDIF_Message::CDIF_Message()
{
   message = 0;

   memset(args, 0, sizeof(args));
}

CDIF_Message::CDIF_Message(unsigned int message_, uint32 arg0, uint32 arg1, uint32 arg2, uint32 arg3)
{
   message = message_;
   args[0] = arg0;
   args[1] = arg1;
   args[2] = arg2;
   args[3] = arg3;
}

CDIF_Message::CDIF_Message(unsigned int message_, const std::string &str)
{
   message = message_;
   str_message = str;
}

CDIF_Message::~CDIF_Message()
{

}

#if HAVE_THREADS

CDIF_Queue::CDIF_Queue()
{
   ze_mutex = slock_new();
   ze_cond  = scond_new();
}

CDIF_Queue::~CDIF_Queue()
{
   slock_free(ze_mutex);
   scond_free(ze_cond);
}

// Returns false if message not read, true if it was read.  Will always return true if "blocking" is set.
// Returns false if the read message code is CDIF_MSG_FATAL_ERROR (and logs the message).
bool CDIF_Queue::Read(CDIF_Message *message, bool blocking)
{
   bool ret = true;

   slock_lock((slock_t*)ze_mutex);

   if(blocking)
   {
      while(ze_queue.size() == 0)	// while, not just if.
         scond_wait((scond_t*)ze_cond, (slock_t*)ze_mutex);
   }

   if(ze_queue.size() == 0)
      ret = false;
   else
   {
      *message = ze_queue.front();
      ze_queue.pop();
   }  

   slock_unlock((slock_t*)ze_mutex);

   if(ret && message->message == CDIF_MSG_FATAL_ERROR)
   {
      log_cb(RETRO_LOG_ERROR, "%s\n", message->str_message.c_str());
      return false;
   }

   return(ret);
}

void CDIF_Queue::Write(const CDIF_Message &message)
{
   slock_lock((slock_t*)ze_mutex);

   ze_queue.push(message);

   scond_signal((scond_t*)ze_cond); // Signal while the mutex is held to prevent icky race conditions

   slock_unlock((slock_t*)ze_mutex);
}


bool CDIF_MT::RT_EjectDisc(bool eject_status, bool skip_actual_eject)
{
   int32_t old_de = DiscEjected;

   DiscEjected = eject_status;

   if(old_de != DiscEjected)
   {
      if(!skip_actual_eject)
         disc_cdaccess->Eject(eject_status);

      if(!eject_status)	// Re-read the TOC
      {
         disc_cdaccess->Read_TOC(&disc_toc);

         if(disc_toc.first_track < 1 || disc_toc.last_track > 99 || disc_toc.first_track > disc_toc.last_track)
         {
            log_cb(RETRO_LOG_ERROR, "TOC first(%d)/last(%d) track numbers bad.\n", disc_toc.first_track, disc_toc.last_track);
            return false;
         }
      }

      SBWritePos = 0;
      ra_lba = 0;
      ra_count = 0;
      last_read_lba = ~0U;
      memset(SectorBuffers, 0, SBSize * sizeof(CDIF_Sector_Buffer));
   }

   return true;
}

struct RTS_Args
{
   CDIF_MT *cdif_ptr;
};

static int ReadThreadStart_C(void *v_arg)
{
   RTS_Args *args = (RTS_Args *)v_arg;

   return args->cdif_ptr->ReadThreadStart();
}

int CDIF_MT::ReadThreadStart()
{
   bool Running = true;

   DiscEjected = true;
   SBWritePos = 0;
   ra_lba = 0;
   ra_count = 0;
   last_read_lba = ~0U;

   if (!RT_EjectDisc(false, true))
   {
      UnrecoverableError = true;
      EmuThreadQueue.Write(CDIF_Message(CDIF_MSG_DONE));
      return 0;
   }

   EmuThreadQueue.Write(CDIF_Message(CDIF_MSG_DONE));

   while(Running)
   {
      CDIF_Message msg;

      // Only do a blocking-wait for a message if we don't have any sectors to read-ahead.
      if(ReadThreadQueue.Read(&msg, ra_count ? false : true))
      {
         switch(msg.message)
         {
            case CDIF_MSG_DIEDIEDIE:
               Running = false;
               break;

            case CDIF_MSG_EJECT:
               RT_EjectDisc(msg.args[0]);
               EmuThreadQueue.Write(CDIF_Message(CDIF_MSG_DONE));
               break;

            case CDIF_MSG_READ_SECTOR:
               {
                  static const int       max_ra = 16;
                  static const int   initial_ra = 1;
                  static const int speedmult_ra = 2;
                  uint32_t              new_lba = msg.args[0];

                  assert((unsigned int)max_ra < (SBSize / 4));

                  if(last_read_lba != ~0U && new_lba == (last_read_lba + 1))
                  {
                     int how_far_ahead = ra_lba - new_lba;

                     if(how_far_ahead <= max_ra)
                        ra_count = MIN(speedmult_ra, 1 + max_ra - how_far_ahead);
                     else
                        ra_count++;
                  }
                  else if(new_lba != last_read_lba)
                  {
                     ra_lba = new_lba;
                     ra_count = initial_ra;
                  }

                  last_read_lba = new_lba;
               }
               break;
         }
      }

      // Don't read >= the "end" of the disc, silly snake.  Slither.
      if(ra_count && ra_lba == disc_toc.tracks[100].lba)
         ra_count = 0;

      if(ra_count)
      {
         uint8_t tmpbuf[2352 + 96];
         bool error_condition = false;

         disc_cdaccess->Read_Raw_Sector(tmpbuf, ra_lba);

         slock_lock((slock_t*)SBMutex);

         SectorBuffers[SBWritePos].lba = ra_lba;
         memcpy(SectorBuffers[SBWritePos].data, tmpbuf, 2352 + 96);
         SectorBuffers[SBWritePos].valid = true;
         SectorBuffers[SBWritePos].error = error_condition;
         SBWritePos = (SBWritePos + 1) % SBSize;

         scond_signal((scond_t*)SBCond);
         slock_unlock((slock_t*)SBMutex);

         ra_lba++;
         ra_count--;
      }
   }

   return(1);
}

CDIF_MT::CDIF_MT(CDAccess *cda) : disc_cdaccess(cda), CDReadThread(NULL), SBMutex(NULL), SBCond(NULL)
{
   CDIF_Message msg;
   RTS_Args s;

   SBMutex            = slock_new();
   SBCond             = scond_new();
   UnrecoverableError = false;

   s.cdif_ptr = this;

   CDReadThread = sthread_create((void (*)(void*))ReadThreadStart_C, &s);
   EmuThreadQueue.Read(&msg);
}


CDIF_MT::~CDIF_MT()
{
   bool thread_deaded_failed = false;

   ReadThreadQueue.Write(CDIF_Message(CDIF_MSG_DIEDIEDIE));

   if(!thread_deaded_failed)
      sthread_join((sthread_t*)CDReadThread);

   if(SBMutex)
   {
      slock_free((slock_t*)SBMutex);
      SBMutex = NULL;
   }

   if(SBCond)
   {
      scond_free(SBCond);
      SBCond = NULL;
   }

   if(disc_cdaccess)
   {
      delete disc_cdaccess;
      disc_cdaccess = NULL;
   }
}

bool CDIF_MT::ReadRawSector(uint8 *buf, uint32 lba, int64 timeout_us)
{
   bool found = false;
   bool error_condition = false;

   if(UnrecoverableError)
   {
      memset(buf, 0, 2352 + 96);
      return(false);
   }

   // This shouldn't happen, the emulated-system-specific CDROM emulation code should make sure the emulated program doesn't try
   // to read past the last "real" sector of the disc.
   if(lba >= disc_toc.tracks[100].lba)
      return(false);

   ReadThreadQueue.Write(CDIF_Message(CDIF_MSG_READ_SECTOR, lba));

   slock_lock((slock_t*)SBMutex);

   do
   {
      int i;
      for(i = 0; i < SBSize; i++)
      {
         if(SectorBuffers[i].valid && SectorBuffers[i].lba == lba)
         {
            error_condition = SectorBuffers[i].error;
            memcpy(buf, SectorBuffers[i].data, 2352 + 96);
            found = true;
         }
      }

      if(!found)
      {
         if (timeout_us >= 0)
         {
            if (!scond_wait_timeout((scond_t*)SBCond, (slock_t*)SBMutex, timeout_us))
            {
               error_condition = true;
               memset(buf, 0, 2352 + 96);
               break;
            }
         }
         else
            scond_wait((scond_t*)SBCond, (slock_t*)SBMutex);
      }
   } while(!found);

   slock_unlock((slock_t*)SBMutex);

   return(!error_condition);
}

bool CDIF_MT::ReadRawSectorPWOnly(uint8 *buf, uint32 lba, bool hint_fullread)
{
   if(UnrecoverableError)
   {
      memset(buf, 0, 96);
      return(false);
   }

   // This shouldn't happen, the emulated-system-specific CDROM emulation code should make sure the emulated program doesn't try
   // to read past the last "real" sector of the disc.
   if(lba >= disc_toc.tracks[100].lba)
   {
      memset(buf, 0, 96);
      return(false);
   }

   if (hint_fullread)
   {
      HintReadSector(lba);
   }

   return disc_cdaccess->Read_Raw_PW(buf, lba);
}

void CDIF_MT::HintReadSector(uint32 lba)
{
   if(UnrecoverableError)
      return;

   ReadThreadQueue.Write(CDIF_Message(CDIF_MSG_READ_SECTOR, lba));
}

bool CDIF_MT::Eject(bool eject_status)
{
   CDIF_Message msg;

   if(UnrecoverableError)
      return(false);

   ReadThreadQueue.Write(CDIF_Message(CDIF_MSG_EJECT, eject_status));
   EmuThreadQueue.Read(&msg);

   return(true);
}

#endif /* HAVE_THREADS */

bool CDIF::ValidateRawSector(uint8 *buf)
{
   int mode = buf[12 + 3];

   if(mode != 0x1 && mode != 0x2)
      return(false);

   if(!edc_lec_check_and_correct(buf, mode == 2))
      return(false);

   return(true);
}

int CDIF::ReadSector(uint8* pBuf, uint32 lba, uint32 nSectors)
{
   int ret = 0;

   if(UnrecoverableError)
      return(false);

   while(nSectors--)
   {
      int mode;
      uint8_t tmpbuf[2352 + 96];

      if(!ReadRawSector(tmpbuf, lba))
         return(false);

      if(!ValidateRawSector(tmpbuf))
         return(false);

      mode = tmpbuf[12 + 3];

      if(!ret)
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
            return(false);
      }

      pBuf += 2048;
      lba++;
   }

   return(ret);
}

// Single-threaded implementation follows.

CDIF_ST::CDIF_ST(CDAccess *cda) : disc_cdaccess(cda)
{
   UnrecoverableError = false;
   DiscEjected = false;

   if (!disc_cdaccess)
   {
      MDFN_Error(0, "CDIF_ST: NULL CDAccess");
      UnrecoverableError = true;
      return;
   }

   disc_cdaccess->Read_TOC(&disc_toc);

   if(disc_toc.first_track < 1 || disc_toc.last_track > 99 || disc_toc.first_track > disc_toc.last_track)
   {
      MDFN_Error(0, "TOC first(%d)/last(%d) track numbers bad.",
            disc_toc.first_track, disc_toc.last_track);
      UnrecoverableError = true;
   }
}

CDIF_ST::~CDIF_ST()
{
   if(disc_cdaccess)
   {
      delete disc_cdaccess;
      disc_cdaccess = NULL;
   }
}

void CDIF_ST::HintReadSector(uint32 lba)
{
   /* TODO: disc_cdaccess seek hint? (probably not, would require asynchronousitycamel) */
}

bool CDIF_ST::ReadRawSector(uint8 *buf, uint32 lba, int64 timeout_us)
{
   if(UnrecoverableError)
   {
      memset(buf, 0, 2352 + 96);
      return(false);
   }

   disc_cdaccess->Read_Raw_Sector(buf, lba);

   return(true);
}

bool CDIF_ST::ReadRawSectorPWOnly(uint8 *buf, uint32 lba, bool hint_fullread)
{
   if(UnrecoverableError)
   {
      memset(buf, 0, 96);
      return(false);
   }

   // This shouldn't happen, the emulated-system-specific CDROM emulation code should make sure the emulated program doesn't try
   // to read past the last "real" sector of the disc.
   if(lba >= disc_toc.tracks[100].lba)
   {
      memset(buf, 0, 96);
      return(false);
   }

   return disc_cdaccess->Read_Raw_PW(buf, lba);
}

bool CDIF_ST::Eject(bool eject_status)
{
   if(UnrecoverableError)
      return(false);

   int32_t old_de = DiscEjected;

   DiscEjected = eject_status;

   if(old_de != DiscEjected)
   {
      disc_cdaccess->Eject(eject_status);

      if(!eject_status)     // Re-read the TOC
      {
         disc_cdaccess->Read_TOC(&disc_toc);

         if(disc_toc.first_track < 1 || disc_toc.last_track > 99 || disc_toc.first_track > disc_toc.last_track)
         {
            log_cb(RETRO_LOG_ERROR, "TOC first(%d)/last(%d) track numbers bad.\n", disc_toc.first_track, disc_toc.last_track);
            return false;
         }
      }
   }

   return(true);
}


/* CDIF_Stream_Thing: a Stream view onto a region of a CDIF (start_lba
 * .. start_lba+sector_count*2048). The CDIF itself is borrowed - this
 * stream does not own it and must not outlive it.
 *
 * Embeds struct Stream as the first member so the static cdif_stream_ops
 * vtable can dispatch via the same upcast pattern as FileStream and
 * MemoryStream. Heap-allocated by MakeStream(); destroy via stream_destroy. */
struct CDIF_Stream_Thing
{
   struct Stream  base;
   CDIF          *cdintf;
   uint32         start_lba;
   uint32         sector_count;
   int64          position;
};

static struct CDIF_Stream_Thing *cst_from_stream(struct Stream *s)
{
   return (struct CDIF_Stream_Thing *)s;
}

static uint64_t cst_read(struct Stream *s, void *data, uint64_t count)
{
   struct CDIF_Stream_Thing *cst = cst_from_stream(s);
   uint64_t rp;
   uint64_t end_byte = (uint64_t)cst->sector_count * 2048;

   if (cst->position < 0 || (uint64_t)cst->position >= end_byte)
      return 0;

   if (count > end_byte - (uint64_t)cst->position)
      count = end_byte - (uint64_t)cst->position;
   if (!count)
      return 0;

   for (rp = (uint64_t)cst->position; rp < (uint64_t)cst->position + count;
         rp = (rp & ~(uint64_t)2047) + 2048)
   {
      uint8_t buf[2048];
      uint64_t in_sector_off = rp & 2047;
      uint64_t want = 2048 - in_sector_off;
      uint64_t remaining = count - (rp - (uint64_t)cst->position);

      cst->cdintf->ReadSector(buf, cst->start_lba + (rp / 2048), 1);

      if (want > remaining)
         want = remaining;
      memcpy((uint8_t *)data + (rp - (uint64_t)cst->position),
            buf + in_sector_off, (size_t)want);
   }

   cst->position += count;
   return count;
}

static void cst_seek(struct Stream *s, int64_t offset, int whence)
{
   struct CDIF_Stream_Thing *cst = cst_from_stream(s);
   int64_t new_position;

   switch (whence)
   {
      case SEEK_SET:
         new_position = offset;
         break;
      case SEEK_CUR:
         new_position = cst->position + offset;
         break;
      case SEEK_END:
         new_position = (int64_t)cst->sector_count * 2048 + offset;
         break;
      default:
         /* The original Mednafen code fell through to a use of an
          * uninitialised new_position here. No caller in this
          * libretro core triggers it, but treat it as a no-op
          * defensively. */
         return;
   }

   if (new_position < 0)
      return;
   cst->position = new_position;
}

static uint64_t cst_tell(struct Stream *s)
{
   return (uint64_t)cst_from_stream(s)->position;
}

static uint64_t cst_size(struct Stream *s)
{
   return (uint64_t)cst_from_stream(s)->sector_count * 2048;
}

static void cst_close(struct Stream *s)
{
   /* Nothing to release - cdintf is borrowed. */
   (void)s;
}

static void cst_destroy(struct Stream *s)
{
   if (!s)
      return;
   free(cst_from_stream(s));
}

static const struct StreamOps cdif_stream_ops =
{
   cst_read,
   cst_seek,
   cst_tell,
   cst_size,
   cst_close,
   cst_destroy,
   NULL,    /* get_line: not used on CDIF streams in this core */
};

Stream *CDIF::MakeStream(uint32 lba, uint32 sector_count)
{
   struct CDIF_Stream_Thing *cst =
      (struct CDIF_Stream_Thing *)malloc(sizeof(*cst));
   if (!cst)
      return NULL;

   cst->base.ops     = &cdif_stream_ops;
   cst->cdintf       = this;
   cst->start_lba    = lba;
   cst->sector_count = sector_count;
   cst->position     = 0;
   return &cst->base;
}


extern "C" CDIF *CDIF_Open(bool *success, const char *path, const bool is_device, bool image_memcache)
{
   CDAccess *cda;
   CDIF     *cdif;

   *success = true;
   cda = cdaccess_open_image(success, path, image_memcache);

   if (!*success)
   {
      if (cda)
         delete cda;
      return NULL;
   }

#if HAVE_THREADS
   if (!image_memcache)
   {
      CDIF_MT *mt = new CDIF_MT(cda);
      if (mt->IsUnrecoverable())
      {
         *success = false;
         delete mt; /* destructor will free cda */
         return NULL;
      }
      return mt;
   }
#endif /* HAVE_THREADS */

   cdif = new CDIF_ST(cda);
   if (cdif->IsUnrecoverable())
   {
      *success = false;
      delete cdif; /* destructor will free cda */
      return NULL;
   }
   return cdif;
}

/*
 * C-linkage shims so plain-C consumers (cdc.c) can drive CDIF
 * without needing to parse the C++ class declaration.  Each one
 * resolves to a single virtual call (the CDIF instance is still
 * a vtable-bearing C++ object); the wrappers just route the
 * non-mangled symbol to the mangled member function.
 */
extern "C" void CDIF_ReadTOC(CDIF *cdif, TOC *read_target)
{
   cdif->ReadTOC(read_target);
}

extern "C" bool CDIF_ReadRawSector(CDIF *cdif, uint8_t *buf,
                                   uint32_t lba, int64_t timeout_us)
{
   return cdif->ReadRawSector(buf, lba, timeout_us);
}

extern "C" bool CDIF_ReadRawSectorPWOnly(CDIF *cdif, uint8_t *buf,
                                         uint32_t lba, bool hint_fullread)
{
   return cdif->ReadRawSectorPWOnly(buf, lba, hint_fullread);
}
