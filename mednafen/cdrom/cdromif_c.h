#ifndef __MDFN_CDROM_CDROMIF_C_H
#define __MDFN_CDROM_CDROMIF_C_H

#include <stdint.h>
#include <boolean.h>

#include "CDUtility.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C-linkage shims around the CDIF class (cdromif.h) for consumers
 * that have been converted to plain C and can no longer parse the
 * `class CDIF { virtual ... }` declaration.
 *
 * From C, `struct CDIF` is opaque - the underlying object is still
 * a C++ instance with virtual methods, allocated and freed through
 * CDIF_Open and (currently) C++ `delete` on the libretro.cpp side.
 * The C consumer only ever holds a pointer.
 *
 * The shim functions forward to the corresponding C++ methods.
 * They are defined in cdromif.cpp inside an extern "C" block, so
 * the symbol names are unmangled and link from C objects.
 *
 * The argument shapes match the C++ signatures, with default
 * arguments expanded - C doesn't have default args, so callers
 * pass the value explicitly.  ReadRawSector defaults to -1 in C++
 * (meaning "no timeout"); the C shim takes the same semantics
 * but the caller must spell the value out.
 */

struct CDIF;
typedef struct CDIF CDIF;

/* Existing factory function from cdromif.cpp; redeclared here so
 * C consumers can grab a pointer.  The pointer is opaque on the C
 * side and must only be passed back through these shims (or freed
 * via the C++ delete in libretro.cpp - we don't expose a Close
 * shim because the only deleter today is libretro.cpp's C++
 * teardown path). */
struct CDIF *CDIF_Open(bool *success, const char *path,
                       const bool is_device, bool image_memcache);

/* CDIF::ReadTOC copies the disc's parsed table-of-contents into
 * *read_target.  The original C++ method was inline; the shim
 * still resolves to a few-instruction copy. */
void CDIF_ReadTOC(struct CDIF *cdif, TOC *read_target);

/* CDIF::ReadRawSector reads a 2352-byte sector plus 96 bytes of
 * subchannel data (2448 bytes total) into buf.  Returns true on
 * success.  On the async backend, timeout_us is the wait budget
 * in microseconds - 0 means "non-blocking try", -1 means "wait
 * indefinitely".  On a synchronous backend the value is ignored.
 *
 * On failure, the underlying CDIF backends still write a defined
 * pattern (zeros for CDIF_ST, MakeSubPQ + zero for CDAccess_Image)
 * so the buffer is always safe to subsequently DecodeSubQ. */
bool CDIF_ReadRawSector(struct CDIF *cdif, uint8_t *buf,
                        uint32_t lba, int64_t timeout_us);

/* CDIF::ReadRawSectorPWOnly reads only the 96 bytes of subchannel
 * (P+W) data for the sector at lba into buf.  hint_fullread asks
 * the backend to bring the corresponding full sector into cache
 * even though only the subchannel is being returned, so a
 * subsequent ReadRawSector for the same lba is fast. */
bool CDIF_ReadRawSectorPWOnly(struct CDIF *cdif, uint8_t *buf,
                              uint32_t lba, bool hint_fullread);

#ifdef __cplusplus
}
#endif

#endif /* __MDFN_CDROM_CDROMIF_C_H */
