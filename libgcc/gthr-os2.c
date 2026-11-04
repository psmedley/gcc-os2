/* Threads compatibily routines for libgcc2.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1997-2016 Free Software Foundation, Inc.
   Contributed by KO Myung-Hun <komh@chollian.net>.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#define INCL_DOS
#define INCL_DOSERRORS
#include <os2.h>

#include <assert.h>
#include <errno.h>

#include <sys/builtin.h>
#include <sys/fmutex.h>
#include <sys/time.h>

#include "gthr-os2.h"

/* With NDEBUG the deadlock assertion below compiles away, so there is no
   need to query the owner count of a non-recursive mutex on every
   blocking lock.  */
#ifdef NDEBUG
# define __GTHREAD_OS2_CHECK_DEADLOCK 0
#else
# define __GTHREAD_OS2_CHECK_DEADLOCK 1
#endif

/* Protects the thread list used by create/join/detach.  */
static _fmutex __gthread_os2_thread_lock = _FMUTEX_INITIALIZER;

void
__gthread_os2_mutex_init (__gthread_mutex_t *mutex, int recursive)
{
  mutex->recursive = recursive;
  DosCreateMutexSem (NULL, (PHMTX) &mutex->mtx, 0, FALSE);
}

int
__gthread_os2_mutex_destroy (__gthread_mutex_t *mutex, int recursive)
{
  if (mutex->recursive != recursive)
    return 1;

  /* A statically initialized mutex that was never locked has no handle.  */
  if ((HMTX) mutex->mtx == NULLHANDLE)
    return 0;

  return DosCloseMutexSem ((HMTX) mutex->mtx);
}

static int
__gthread_os2_mutex_lock_common (__gthread_mutex_t *mutex, int recursive,
                                 ULONG ulTimeout)
{
  APIRET rc;

  if (mutex->recursive != recursive)
    return 1;

  /* Initialize static mutex.  */
  if ((HMTX) mutex->mtx == NULLHANDLE)
    {
      /* FIXME: resource leaks.  */
      _fmutex_request (&mutex->static_lock, _FMR_IGNINT);
      if ((HMTX) mutex->mtx == NULLHANDLE)
        __gthread_os2_mutex_init (mutex, recursive);
      _fmutex_release (&mutex->static_lock);
    }

  /* An indefinite wait can be interrupted (e.g. by signal delivery);
     a lock that silently fails here would leave callers such as
     __gthread_os2_cond_wait_common believing they own the mutex.  */
  do
    rc = DosRequestMutexSem ((HMTX) mutex->mtx, ulTimeout);
  while (rc == ERROR_INTERRUPT && ulTimeout == SEM_INDEFINITE_WAIT);

  if (rc)
    return 1;

  /* OS/2 mutex semaphores are always recursive.  For a non-recursive
     mutex, a request by the owner must fail (trylock / timedlock) or is a
     deadlock (lock).  Recursive mutexes don't need the extra query.  */
  if (!mutex->recursive
      && (ulTimeout != SEM_INDEFINITE_WAIT || __GTHREAD_OS2_CHECK_DEADLOCK))
    {
      PID pid;
      TID tid;
      ULONG ulCount = 1;

      DosQueryMutexSem ((HMTX) mutex->mtx, &pid, &tid, &ulCount);

      if (ulCount > 1 && ulTimeout != SEM_INDEFINITE_WAIT)
        {
          DosReleaseMutexSem ((HMTX) mutex->mtx);

          return 1;
        }

      assert (!"Dead lock" || ulCount == 1);
    }

  return 0;
}

int
__gthread_os2_mutex_lock (__gthread_mutex_t *mutex, int recursive)
{
  return __gthread_os2_mutex_lock_common (mutex, recursive,
                                          SEM_INDEFINITE_WAIT);
}

int
__gthread_os2_mutex_trylock (__gthread_mutex_t *mutex, int recursive)
{
  return __gthread_os2_mutex_lock_common (mutex, recursive,
                                          SEM_IMMEDIATE_RETURN);
}

int
__gthread_os2_mutex_unlock (__gthread_mutex_t *mutex, int recursive)
{
  if (mutex->recursive != recursive)
    return 1;

  return DosReleaseMutexSem ((HMTX) mutex->mtx);
}

void
__gthread_os2_cond_init (__gthread_cond_t *cond)
{
  HEV hevAck = NULLHANDLE;
  HEV hevEv = NULLHANDLE;

  cond->signaled = 0;
  cond->waiters = 0;

  /* cond->ev is the "initialized" flag tested without a lock by the
     lazy-init fast path, so it must be published last, after ack exists.  */
  DosCreateEventSem (NULL, &hevAck, 0, FALSE);
  DosCreateEventSem (NULL, &hevEv, 0, FALSE);
  cond->ack = hevAck;
  __asm__ __volatile__ ("" ::: "memory");
  cond->ev = hevEv;
}

int
__gthread_os2_cond_destroy (__gthread_cond_t *cond)
{
  APIRET rc1, rc2;

  /* A statically initialized condition that was never used has no
     handles.  */
  if ((HEV) cond->ev == NULLHANDLE)
    return 0;

  rc1 = DosCloseEventSem ((HEV) cond->ev);
  rc2 = DosCloseEventSem ((HEV) cond->ack);

  return rc1 || rc2;
}

static void
__gthread_os2_static_cond_init (__gthread_cond_t *cond)
{
  /* FIXME: resource leaks */
  _fmutex_request (&cond->static_lock, _FMR_IGNINT);
  if ((HEV) cond->ev == NULLHANDLE)
     __gthread_os2_cond_init (cond);
  _fmutex_release (&cond->static_lock);
}

int
__gthread_os2_cond_broadcast (__gthread_cond_t *cond)
{
  if ((HEV) cond->ev == NULLHANDLE)
    __gthread_os2_static_cond_init (cond);

  while (!__atomic_cmpxchg32 (&cond->waiters, 0, 0))
    __gthread_os2_cond_signal (cond);

  return 0;
}

static int
__gthread_os2_cond_wait_common (__gthread_cond_t *cond,
                                __gthread_mutex_t *mutex,
                                int recursive, ULONG ulTimeout)
{
  ULONG ulPost;
  APIRET rc;

  if (mutex->recursive != recursive)
    return 1;

  if ((HEV) cond->ev == NULLHANDLE)
    __gthread_os2_static_cond_init (cond);

  __atomic_increment (&cond->waiters);

  __gthread_os2_mutex_unlock (mutex, recursive);

  do
    {
      rc = DosWaitEventSem ((HEV) cond->ev, ulTimeout);
      if (rc == NO_ERROR)
        DosResetEventSem ((HEV) cond->ev, &ulPost);
    } while (rc == NO_ERROR && !__atomic_cmpxchg32 (&cond->signaled, 0, 1));

  __atomic_decrement (&cond->waiters);

  DosPostEventSem ((HEV) cond->ack);

  __gthread_os2_mutex_lock (mutex, recursive);

  return rc;
}

int
__gthread_os2_cond_wait (__gthread_cond_t *cond, __gthread_mutex_t *mutex)
{
  return __gthread_os2_cond_wait_common (cond, mutex, 0, SEM_INDEFINITE_WAIT);
}

int
__gthread_os2_cond_wait_recursive (__gthread_cond_t *cond,
                                   __gthread_recursive_mutex_t *mutex)
{
  return __gthread_os2_cond_wait_common (cond, mutex, 1, SEM_INDEFINITE_WAIT);
}

typedef struct __gthread_os2_s __gthread_os2_t;

struct __gthread_os2_s {
  __gthread_t tid;
  void *(*func) (void*);
  void *args;
  void *result;
  HEV hevQuit;
  HEV hevDone;
  int detached;
  __gthread_os2_t *next;
};

static __gthread_os2_t main_thread =
  { 1, NULL, NULL, NULL, NULLHANDLE, NULLHANDLE, 0, NULL };

static __gthread_os2_t *__gthread_os2_thread_start = &main_thread;

static __gthread_os2_t *
__gthread_os2_thread_new (void *(*func) (void*), void *args)
{
  __gthread_os2_t *new_thread;

  new_thread = calloc (1, sizeof (*new_thread));
  if (new_thread)
    {
      new_thread->func = func;
      new_thread->args = args;
      DosCreateEventSem (NULL, &new_thread->hevQuit, 0, FALSE);
      DosCreateEventSem (NULL, &new_thread->hevDone, 0, FALSE);
    }

  return new_thread;
}

static void
__gthread_os2_thread_free (__gthread_os2_t *thread)
{
  if (thread == &main_thread)
    return;

  DosCloseEventSem (thread->hevQuit);
  DosCloseEventSem (thread->hevDone);

  free (thread);
}

static __gthread_os2_t *
__gthread_os2_thread_find (__gthread_t tid)
{
  __gthread_os2_t *thread = __gthread_os2_thread_start;

  while (thread && thread->tid != tid)
    thread = thread->next;

  return thread;
}

/* The thread list functions below must be called with
   __gthread_os2_thread_lock held.  */

static void
__gthread_os2_thread_add (__gthread_os2_t *thread, __gthread_t tid)
{
  thread->tid = tid;
  thread->next = __gthread_os2_thread_start;

  __gthread_os2_thread_start = thread;
}

static void
__gthread_os2_thread_del (__gthread_os2_t *thread)
{
  if (thread == &main_thread)
    return;

  if (thread == __gthread_os2_thread_start)
    __gthread_os2_thread_start = thread->next;
  else
    {
      __gthread_os2_t *prev;

      for (prev = __gthread_os2_thread_start; prev->next != thread;
           prev = prev->next)
        /* nothing */;

      prev->next = thread->next;
    }

  __gthread_os2_thread_free (thread);
}

static void thread_entry (void *args)
{
  __gthread_os2_t *th = (__gthread_os2_t *)args;
  int detached;

  th->result = th->func(th->args);
  DosWaitEventSem (th->hevQuit, SEM_INDEFINITE_WAIT);

  detached = th->detached;

  DosPostEventSem (th->hevDone);

  if (detached)
    {
      _fmutex_request (&__gthread_os2_thread_lock, _FMR_IGNINT);
      __gthread_os2_thread_del (th);
      _fmutex_release (&__gthread_os2_thread_lock);
    }
}

int
__gthread_os2_create (__gthread_t *thread, void *(*func) (void*), void *args)
{
  __gthread_os2_t *th;
  int tid;

  th = __gthread_os2_thread_new (func, args);
  if (!th)
    return ENOMEM;

  /* Hold the list lock across thread creation so that the new thread
     is in the list before anyone can look it up.  thread_entry only
     takes the lock after join/detach, which require the list entry.  */
  _fmutex_request (&__gthread_os2_thread_lock, _FMR_IGNINT);

  tid = _beginthread (thread_entry, NULL, 1024 * 1024, th);
  if (tid == -1)
    {
      int err = errno ? errno : EAGAIN;

      _fmutex_release (&__gthread_os2_thread_lock);
      __gthread_os2_thread_free (th);

      return err;
    }

  __gthread_os2_thread_add (th, tid);

  _fmutex_release (&__gthread_os2_thread_lock);

  *thread = tid;

  return 0;
}

int
__gthread_os2_join (__gthread_t thread, void **value_ptr)
{
  __gthread_os2_t *th;

  APIRET rc;

  _fmutex_request (&__gthread_os2_thread_lock, _FMR_IGNINT);
  th = __gthread_os2_thread_find (thread);
  _fmutex_release (&__gthread_os2_thread_lock);

  if (!th || th->detached)
    return 1;

  DosPostEventSem (th->hevQuit);

  do
    rc = DosWaitEventSem (th->hevDone, SEM_INDEFINITE_WAIT);
  while (rc == ERROR_INTERRUPT);

  if (rc)
    return 1;

  if (value_ptr)
    *value_ptr = th->result;

  _fmutex_request (&__gthread_os2_thread_lock, _FMR_IGNINT);
  __gthread_os2_thread_del (th);
  _fmutex_release (&__gthread_os2_thread_lock);

  return 0;
}

int
__gthread_os2_detach (__gthread_t thread)
{
  __gthread_os2_t *th;

  _fmutex_request (&__gthread_os2_thread_lock, _FMR_IGNINT);
  th = __gthread_os2_thread_find (thread);
  if (th)
    th->detached = 1;
  _fmutex_release (&__gthread_os2_thread_lock);

  if (!th)
    return 1;

  DosPostEventSem (th->hevQuit);

  return 0;
}

/* Compare threads. Return non-zero if same, otherwise 0.  */
int
__gthread_os2_equal (__gthread_t t1, __gthread_t t2)
{
  return t1 == t2;
}

__gthread_t
__gthread_os2_self (void)
{
  return _gettid();
}

int
__gthread_os2_yield (void)
{
  DosSleep (0);

  return 0;
}

static ULONG
__gthread_os2_abs2rem (const __gthread_time_t *abs_timeout)
{
  struct timeval tv;
  long long llNow;
  long long llDeadline;
  long long llRemain;

  /* Compute in 64 bits: seconds since the epoch times 1000 does not fit
     in a ULONG, and the wrapped 32-bit values compare wrongly whenever the
     wait straddles a wrap (about every 49.7 days), giving either an
     immediate timeout or a near-infinite wait.  */
  gettimeofday (&tv, NULL);
  llNow = (long long) tv.tv_sec * 1000 + tv.tv_usec / 1000;
  llDeadline = (long long) abs_timeout->tv_sec * 1000
               + abs_timeout->tv_nsec / 1000000L;
  llRemain = llDeadline - llNow;

  if (llRemain <= 0)
    return SEM_IMMEDIATE_RETURN;

  /* SEM_INDEFINITE_WAIT is (ULONG) -1; stay just below it.  */
  if (llRemain >= (long long) SEM_INDEFINITE_WAIT)
    return SEM_INDEFINITE_WAIT - 1;

  return (ULONG) llRemain;
}

int
__gthread_os2_mutex_timedlock (__gthread_mutex_t *mutex,
                               const __gthread_time_t *abs_timeout)
{
  return __gthread_os2_mutex_lock_common (mutex, 0,
                                          __gthread_os2_abs2rem (abs_timeout));
}

int
__gthread_os2_recursive_mutex_timedlock (__gthread_recursive_mutex_t *mutex,
                                         const __gthread_time_t *abs_timeout)
{
  return __gthread_os2_mutex_lock_common (mutex, 1,
                                          __gthread_os2_abs2rem (abs_timeout));
}

int
__gthread_os2_cond_signal (__gthread_cond_t *cond)
{
  ULONG ulPost;

  if ((HEV) cond->ev == NULLHANDLE)
    __gthread_os2_static_cond_init (cond);

  DosResetEventSem ((HEV) cond->ack, &ulPost);

  if (!__atomic_cmpxchg32 (&cond->waiters, 0, 0))
    {
      __atomic_xchg (&cond->signaled, 1);
      DosPostEventSem ((HEV) cond->ev);

      DosWaitEventSem ((HEV) cond->ack, SEM_INDEFINITE_WAIT);
    }

  return 0;
}

int
__gthread_os2_cond_timedwait (__gthread_cond_t *cond, __gthread_mutex_t *mutex,
                              const __gthread_time_t *abs_timeout)
{
  return __gthread_os2_cond_wait_common (cond, mutex, 0,
                                         __gthread_os2_abs2rem (abs_timeout));
}
