#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error reporting helper.
 *
 * The contract is simple: MDFN_Error logs an error message via
 * log_cb (libretro's logging callback) and returns. Callers must check
 * their own status and propagate failure via return codes.
 */
void MDFN_Error(int errno_code, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
