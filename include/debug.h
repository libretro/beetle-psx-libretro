/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2014-2021 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <unistd.h>

#include <libretro.h>
extern retro_log_printf_t log_cb;

#define NOLOG_L 0
#define ERROR_L 1
#define WARNING_L 2
#define INFO_L 3
#define DEBUG_L 4

#ifndef LOG_LEVEL
#define LOG_LEVEL INFO_L
#endif

// -------------

#if (LOG_LEVEL >= DEBUG_L)
# define pr_debug(...) \
   log_cb(RETRO_LOG_DEBUG, "[Lightrec]: " __VA_ARGS__)
#else
#define pr_debug(...)
#endif

#if (LOG_LEVEL >= INFO_L)
# define pr_info(...) \
   log_cb(RETRO_LOG_INFO, "[Lightrec]: " __VA_ARGS__)
#else
#define pr_info(...)
#endif

#if (LOG_LEVEL >= WARNING_L)
# define pr_warn(...) \
   log_cb(RETRO_LOG_WARN, "[Lightrec]: " __VA_ARGS__)
#else
#define pr_warn(...)
#endif

#if (LOG_LEVEL >= ERROR_L)
# define pr_err(...) \
   log_cb(RETRO_LOG_ERROR, "[Lightrec]: " __VA_ARGS__)
#else
#define pr_err(...)
#endif

#endif
