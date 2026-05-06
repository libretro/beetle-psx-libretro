#ifndef __MDFN_CDROM_CDACCESS_C_H
#define __MDFN_CDROM_CDACCESS_C_H

#include <stdint.h>
#include <boolean.h>

#include "CDUtility.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C-linkage shims around the CDAccess class hierarchy (CDAccess.h)
 * so plain-C consumers (cdromif.c) can drive the polymorphic disc-
 * format readers without parsing the C++ class declaration.
 *
 * The CDAccess pointer is opaque on the C side. Construction is via
 * cdaccess_open_image (which dispatches on path extension to one of
 * CDAccess_Image / CDAccess_CCD / CDAccess_CHD / CDAccess_PBP);
 * destruction is via CDAccess_destroy.
 *
 * The shims are thin forwarders that resolve to a single virtual
 * call. Defined inside an extern "C" block in CDAccess.cpp so the
 * symbol names are unmangled.
 */

struct CDAccess;
typedef struct CDAccess CDAccess;

/* Construct a CDAccess of the appropriate concrete type for the
 * given path extension. On success returns a pointer and *success
 * is set to true; on failure may return NULL and *success is false.
 * The caller must eventually free the pointer via CDAccess_destroy. */
CDAccess *cdaccess_open_image(bool *success, const char *path,
                              bool image_memcache);

/* CDAccess::Read_Raw_Sector - reads 2352 main + 96 subchannel bytes
 * (2448 total) for the sector at lba into buf. Returns true on
 * success. */
bool CDAccess_Read_Raw_Sector(CDAccess *cda, uint8_t *buf, int32_t lba);

/* CDAccess::Read_Raw_PW - reads only the 96 bytes of P+W subchannel
 * data for the sector at lba into buf. Returns true on success. */
bool CDAccess_Read_Raw_PW(CDAccess *cda, uint8_t *buf, int32_t lba);

/* CDAccess::Read_TOC - parses the disc's table-of-contents into
 * *toc. Returns true on success. */
bool CDAccess_Read_TOC(CDAccess *cda, TOC *toc);

/* CDAccess::Eject - eject (true) or insert (false) the disc.
 * Returns true on success or NOP. */
void CDAccess_Eject(CDAccess *cda, bool eject_status);

/* CDAccess destructor (delete cda). */
void CDAccess_destroy(CDAccess *cda);

#ifdef __cplusplus
}
#endif

#endif /* __MDFN_CDROM_CDACCESS_C_H */
