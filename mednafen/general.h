#ifndef _GENERAL_H
#define _GENERAL_H

#include <stddef.h>
#include <string>

void MDFN_trim(std::string &string);

/* The full Mednafen enum had eleven entries covering save states,
 * snapshots, cheat lists, IPS patches, palette files, etc.; the
 * libretro core only needs two file-naming categories. */
typedef enum
{
   MDFNMKF_SAV,
   MDFNMKF_FIRMWARE
} MakeFName_Type;

/* Compose a full filesystem path into the caller-provided buffer.
 *
 * Earlier versions of this function returned a pointer into a static
 * buffer, which was a footgun: two MDFN_MakeFName calls in a single
 * expression would silently overwrite each other. The new contract
 * is explicit - the caller owns the buffer.
 *
 *   type   - which directory to compose against (saves vs firmware)
 *   id1    - reserved (Mednafen's slot number; ignored here)
 *   cd1    - filename component to append
 *   out    - destination buffer
 *   outlen - size of `out` in bytes
 *
 * On overflow, `out` is truncated and an error is logged via log_cb. */
void MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1,
      char *out, size_t outlen);

void MDFN_GetFilePathComponents(const std::string &file_path, std::string *dir_path_out, std::string *file_base_out = NULL, std::string *file_ext_out = NULL);

/* Resolve `rel_path` relative to `dir_path`. If `rel_path` is
 * absolute, it is returned unchanged; otherwise `dir_path/rel_path`
 * is composed using the platform-appropriate separator. The previous
 * `skip_safety_check` parameter gated a path-character whitelist
 * predicated on the `filesys.untrusted_fip_check` setting, but that
 * setting is hard-wired to false in this libretro core (see
 * settings.c), making the entire check dead. The parameter is gone. */
std::string MDFN_EvalFIP(const std::string &dir_path, const std::string &rel_path);
#endif
