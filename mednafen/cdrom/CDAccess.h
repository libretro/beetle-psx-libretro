#ifndef __MDFN_CDROMFILE_H
#define __MDFN_CDROMFILE_H

#include <stdint.h>
#include <boolean.h>

#include "CDUtility.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CDAccess: polymorphic disc-image reader.
 *
 * Previously a C++ abstract base class with four pure-virtual
 * methods.  Now a plain C struct with explicit function-pointer
 * vtable.  Each backend (Image / CCD / CHD / PBP) defines a
 * concrete struct that embeds `struct CDAccess base` as its first
 * member and a factory function `CDAccess_<X>_New` that allocates
 * the struct and installs the function pointers.
 *
 * Lifecycle: `cdaccess_open_image` dispatches on file extension
 * to the right factory.  Use `CDAccess_Read_Raw_Sector` (or
 * `cda->Read_Raw_*` etc. for the rest of the vtable) to drive an
 * instance, and `cda->destroy(cda)` to tear it down.
 *
 * Backends MUST set Read_Raw_Sector, Read_Raw_PW, Read_TOC, Eject,
 * and destroy.  All five vtable slots are required - the public
 * dispatch wrappers below invoke them unconditionally.
 */
struct CDAccess
{
   bool (*Read_Raw_Sector)(struct CDAccess *self, uint8_t *buf, int32_t lba);
   bool (*Read_Raw_PW)    (struct CDAccess *self, uint8_t *buf, int32_t lba);
   bool (*Read_TOC)       (struct CDAccess *self, TOC *toc);
   void (*Eject)          (struct CDAccess *self, bool eject_status);
   void (*destroy)        (struct CDAccess *self);
};
typedef struct CDAccess CDAccess;

/* Factory: dispatches on path extension (.ccd, .pbp, .chd, else
 * Image).  On success returns a pointer and *success is true; on
 * failure may still return a non-NULL pointer (caller must
 * destroy it) and sets *success to false. */
CDAccess *cdaccess_open_image(bool *success, const char *path,
      bool image_memcache);

/* All vtable ops are invoked directly through the cda->op(cda, ...)
 * call shape; there are no public dispatch wrappers because they
 * would be one-line passthroughs. */

#ifdef __cplusplus
}
#endif

#endif
