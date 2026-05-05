#ifndef _GENERAL_H
#define _GENERAL_H

#include <stddef.h>
#include <string>

/* File-inclusion-for-read-only-path safety check, used by PSF and
 * CUE/TOC sheet processing to refuse paths that escape the playlist
 * directory. */
bool MDFN_IsFIROPSafe(const std::string &path);

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

std::string MDFN_EvalFIP(const std::string &dir_path, const std::string &rel_path, bool skip_safety_check);
#endif
