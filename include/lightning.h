#define HAVE_MMAP 1

/* The bundled mman-win32 shim in deps/mman exports the memory-protect
 * routine as _mprotect (every other function uses the plain POSIX name).
 * Alias it so lightning's mprotect() call sites in lib/lightning.c resolve
 * to the shim. Must precede <sys/mman.h>, which lightning.c includes when
 * HAVE_MMAP is set. */
#if defined(_WIN32) && !defined(_XBOX)
#  define mprotect _mprotect
#endif

#include <lightning-actual.h>
