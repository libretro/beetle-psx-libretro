#include "mednafen/mednafen-types.h"
#include "mednafen/mednafen.h"
#include "mednafen/md5.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/mednafen-driver.h"

#if defined(__CELLOS_LV2__)
#include <sys/timer.h>
#include <ppu_intrinsics.h>
#endif

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <iostream>

//#define LIBRETRO_DEBUG

// Stubs

extern std::string retro_base_directory;
extern std::string retro_base_name;

#ifdef _WIN32
static void sanitize_path(std::string &path)
{
   size_t size = path.size();
   for (size_t i = 0; i < size; i++)
      if (path[i] == '/')
         path[i] = '\\';
}
#endif

// Use a simpler approach to make sure that things go right for libretro.
std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
   char slash;
#ifdef _WIN32
   slash = '\\';
#else
   slash = '/';
#endif
   std::string ret;
   switch (type)
   {
      case MDFNMKF_SAV:
         ret = retro_base_directory + slash + retro_base_name +
            std::string(".") +
#ifndef _XBOX
	    md5_context::asciistr(MDFNGameInfo->MD5, 0) + std::string(".") +
#endif
            std::string(cd1);
         break;
      case MDFNMKF_FIRMWARE:
         ret = std::string(cd1);
#ifdef _WIN32
   sanitize_path(ret); // Because Windows path handling is mongoloid.
#endif
         break;
      default:
         break;
   }

   fprintf(stderr, "%s\n", ret.c_str());
   return ret;
}

void MDFND_DispMessage(unsigned char *str)
{
#ifdef LIBRETRO_DEBUG
   if(str != NULL)
      fprintf(stderr, "DISPMSG: %s\n", str);
#endif
}

void MDFND_Message(const char *str)
{
#ifdef LIBRETRO_DEBUG
   if(str != NULL)
      fprintf(stderr, "MSG: %s\n", str);
#endif
}

void MDFND_MidSync(const EmulateSpecStruct *)
{}

void MDFND_PrintError(const char* err)
{
#ifdef LIBRETRO_DEBUG
   if(err != NULL)
      fprintf(stderr, "ERR: %s\n", err);
#endif
}

void MDFND_Sleep(unsigned int time)
{
#if defined(_WIN32)
   Sleep(time);
#elif defined(__CELLOS_LV2__)
   sys_timer_usleep(time * 1000);
#else
   usleep(time * 1000);
#endif
}

#ifdef WANT_THREADING

#include "thread.h"
#include <stdlib.h>

#if defined(_WIN32) && !defined(_XBOX)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(_XBOX)
#include <xtl.h>
#elif defined(GEKKO)
#include "thread/gx_pthread.h"
#else
#include <pthread.h>
#include <time.h>
#endif

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

struct thread_data
{
   void (*func)(void*);
   void *userdata;
};

#ifdef _WIN32

struct sthread
{
   HANDLE thread;
};

static DWORD CALLBACK thread_wrap(void *data_)
{
   struct thread_data *data = (struct thread_data*)data_;
   data->func(data->userdata);
   free(data);
   return 0;
}

sthread_t *sthread_create(void (*thread_func)(void*), void *userdata)
{
   sthread_t *thread = (sthread_t*)calloc(1, sizeof(*thread));
   if (!thread)
      return NULL;

   struct thread_data *data = (struct thread_data*)calloc(1, sizeof(*data));
   if (!data)
   {
      free(thread);
      return NULL;
   }

   data->func = thread_func;
   data->userdata = userdata;

   thread->thread = CreateThread(NULL, 0, thread_wrap, data, 0, NULL);
   if (!thread->thread)
   {
      free(data);
      free(thread);
      return NULL;
   }

   return thread;
}

void sthread_join(sthread_t *thread)
{
   WaitForSingleObject(thread->thread, INFINITE);
   CloseHandle(thread->thread);
   free(thread);
}

struct slock
{
   CRITICAL_SECTION lock;
};

slock_t *slock_new(void)
{
   slock_t *lock = (slock_t*)calloc(1, sizeof(*lock));
   if (!lock)
      return NULL;

   InitializeCriticalSection(&lock->lock);
   return lock;
}

void slock_free(slock_t *lock)
{
   DeleteCriticalSection(&lock->lock);
   free(lock);
}

void slock_lock(slock_t *lock)
{
   EnterCriticalSection(&lock->lock);
}

void slock_unlock(slock_t *lock)
{
   LeaveCriticalSection(&lock->lock);
}

struct scond
{
   HANDLE event;
};

scond_t *scond_new(void)
{
   scond_t *cond = (scond_t*)calloc(1, sizeof(*cond));
   if (!cond)
      return NULL;

   cond->event = CreateEvent(NULL, FALSE, FALSE, NULL);
   if (!cond->event)
   {
      free(cond);
      return NULL;
   }

   return cond;
}

void scond_wait(scond_t *cond, slock_t *lock)
{
   WaitForSingleObject(cond->event, 0);
   slock_unlock(lock);

   WaitForSingleObject(cond->event, INFINITE);

   slock_lock(lock);
}

int scond_wait_timeout(scond_t *cond, slock_t *lock, unsigned timeout_ms)
{
   WaitForSingleObject(cond->event, 0);
   slock_unlock(lock);

   DWORD res = WaitForSingleObject(cond->event, timeout_ms);

   slock_lock(lock);
   return res == WAIT_OBJECT_0;
}

void scond_signal(scond_t *cond)
{
   SetEvent(cond->event);
}

void scond_free(scond_t *cond)
{
   CloseHandle(cond->event);
   free(cond);
}

#else

struct sthread
{
   pthread_t id;
};

static void *thread_wrap(void *data_)
{
   struct thread_data *data = (struct thread_data*)data_;
   data->func(data->userdata);
   free(data);
   return NULL;
}

sthread_t *sthread_create(void (*thread_func)(void*), void *userdata)
{
   sthread_t *thr = (sthread_t*)calloc(1, sizeof(*thr));
   if (!thr)
      return NULL;

   struct thread_data *data = (struct thread_data*)calloc(1, sizeof(*data));
   if (!data)
   {
      free(thr);
      return NULL;
   }

   data->func = thread_func;
   data->userdata = userdata;

   if (pthread_create(&thr->id, NULL, thread_wrap, data) < 0)
   {
      free(data);
      free(thr);
      return NULL;
   }

   return thr;
}

void sthread_join(sthread_t *thread)
{
   pthread_join(thread->id, NULL);
   free(thread);
}

struct slock
{
   pthread_mutex_t lock;
};

slock_t *slock_new(void)
{
   slock_t *lock = (slock_t*)calloc(1, sizeof(*lock));
   if (!lock)
      return NULL;

   if (pthread_mutex_init(&lock->lock, NULL) < 0)
   {
      free(lock);
      return NULL;
   }

   return lock;
}

void slock_free(slock_t *lock)
{
   pthread_mutex_destroy(&lock->lock);
   free(lock);
}

void slock_lock(slock_t *lock)
{
   pthread_mutex_lock(&lock->lock);
}

void slock_unlock(slock_t *lock)
{
   pthread_mutex_unlock(&lock->lock);
}

struct scond
{
   pthread_cond_t cond;
};

scond_t *scond_new(void)
{
   scond_t *cond = (scond_t*)calloc(1, sizeof(*cond));
   if (!cond)
      return NULL;

   if (pthread_cond_init(&cond->cond, NULL) < 0)
   {
      free(cond);
      return NULL;
   }

   return cond;
}

void scond_free(scond_t *cond)
{
   pthread_cond_destroy(&cond->cond);
   free(cond);
}

void scond_wait(scond_t *cond, slock_t *lock)
{
   pthread_cond_wait(&cond->cond, &lock->lock);
}

void scond_signal(scond_t *cond)
{
   pthread_cond_signal(&cond->cond);
}

#endif

MDFN_Thread *MDFND_CreateThread(int (*fn)(void *), void *data)
{
   return (MDFN_Thread*)sthread_create((void (*)(void*))fn, data);
}

void MDFND_WaitThread(MDFN_Thread *thr, int *val)
{
   sthread_join((sthread_t*)thr);

   if (val)
   {
      *val = 0;
      fprintf(stderr, "WaitThread relies on return value.\n");
   }
}

void MDFND_KillThread(MDFN_Thread *)
{
   fprintf(stderr, "Killing a thread is a BAD IDEA!\n");
}

MDFN_Mutex *MDFND_CreateMutex()
{
   return (MDFN_Mutex*)slock_new();
}

void MDFND_DestroyMutex(MDFN_Mutex *lock)
{
   slock_free((slock_t*)lock);
}

int MDFND_LockMutex(MDFN_Mutex *lock)
{
   slock_lock((slock_t*)lock);
   return 0;
}

int MDFND_UnlockMutex(MDFN_Mutex *lock)
{
   slock_unlock((slock_t*)lock);
   return 0;
}

#endif
