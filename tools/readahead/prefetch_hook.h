/* Force-included into offthread_test's own build of the file layer,
 * made with -DVFS_WIN32_PREFETCH_HOOK=readahead_test_hook: the stand-in
 * for PrefetchVirtualMemory on one range. Nothing else includes it. */
#ifndef READAHEAD_PREFETCH_HOOK_H
#define READAHEAD_PREFETCH_HOOK_H

#include <stddef.h>

int readahead_test_hook(void *addr, size_t len);

#endif
