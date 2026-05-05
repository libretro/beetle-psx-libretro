#ifndef BEETLE_PSX_OSD_MESSAGE_H
#define BEETLE_PSX_OSD_MESSAGE_H

/*
 * osd_message - print a printf-formatted message to the libretro
 * frontend's OSD (on-screen display) and/or its log.
 *
 * Replaces the historical MDFN_DispMessage / MDFND_DispMessage
 * pair. Mednafen's standalone build had a layered split here
 * (MDFN_* was the formatting helper, MDFND_* was the abstract
 * driver entry point that different frontends - SDL, GTK,
 * libretro - implemented), but in this libretro core there's
 * only one driver and the indirection was pure overhead. The
 * formatting + libretro RETRO_ENVIRONMENT_SET_MESSAGE{,_EXT}
 * env_cb dispatch are now folded into one function defined in
 * libretro.cpp.
 *
 * Args:
 *   priority - 0..3, higher = more important; passed through to
 *              SET_MESSAGE_EXT for frontends that support it.
 *   level    - retro_log_level enum (RETRO_LOG_DEBUG..ERROR);
 *              also passed to SET_MESSAGE_EXT.
 *   target   - retro_message_target enum (LOG / OSD / ALL).
 *   type     - retro_message_type enum (NOTIFICATION /
 *              NOTIFICATION_ALT / STATUS / PROGRESS).
 *   format   - printf-style format string. Variadic args.
 *
 * Display is gated by the `display_notifications` core option
 * (toggled in libretro.cpp). On frontends that don't expose the
 * RETRO_MESSAGE_INTERFACE_VERSION >= 1 extension (libretro_msg_
 * interface_version < 1), falls back to the legacy
 * RETRO_ENVIRONMENT_SET_MESSAGE which only carries the message
 * string and a frame count, dropping priority/level/target/type.
 */

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

void osd_message(unsigned priority,
                 enum retro_log_level level,
                 enum retro_message_target target,
                 enum retro_message_type type,
                 const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
