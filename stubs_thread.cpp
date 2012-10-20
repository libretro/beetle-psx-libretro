#include "mednafen/mednafen-types.h"
#include "mednafen/mednafen.h"
#include "mednafen/md5.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/mednafen-driver.h"
#include "thread.h"

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
