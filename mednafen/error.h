#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error reporting helper.
 *
 * Historically this header declared a C++ MDFN_Error class that was either
 * thrown as an exception or constructed-and-discarded as a "log + continue"
 * side-effect. Both patterns were error-prone:
 *   - Exceptions are not portable across all libretro target toolchains
 *     (consoles in particular often build with -fno-exceptions or only
 *     partial unwinding support, and the cost of unwinding tables on
 *     statically-linked builds is non-trivial).
 *   - Constructing a temporary object purely for its logging side-effect
 *     was visually indistinguishable from throwing it.
 *
 * The new contract is simpler: MDFN_Error logs an error message via
 * log_cb (libretro's logging callback) and returns. Callers must check
 * their own status and propagate failure via return codes.
 */
void MDFN_Error(int errno_code, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
