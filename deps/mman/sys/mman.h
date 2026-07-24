/* Forwarding shim.
 *
 * lightning and lightrec include <sys/mman.h>; on Windows the
 * implementation is libretro-common's memmap shim, which provides
 * mmap/munmap/mprotect and the PROT_/MAP_ request bits, is linked
 * here unconditionally for data_transfer's window mode, and handles
 * the JIT's full shape: anonymous mappings, executable protections,
 * and W^X mprotect transitions.  The mman-win32 implementation this
 * directory used to carry duplicated those symbols and is gone. */
#ifndef BEETLE_SYS_MMAN_FORWARD_H
#define BEETLE_SYS_MMAN_FORWARD_H

#include <memmap.h>

#endif
