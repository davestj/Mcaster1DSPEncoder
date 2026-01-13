// platform.h — Cross-platform compatibility shims for Mcaster1DSPEncoder
//
// Included by shared .cpp files that compile on both Windows (MSVC/VS2022)
// and Linux (GCC/Clang).  Windows-specific code is unchanged — macros resolve
// to the same MSVC intrinsics they always used.  Linux gets POSIX equivalents.
//
// Rules:
//   • Do NOT include this from any .h that ships in the public API.
//   • Include it as the FIRST include in each affected .cpp file.
//   • Never include <windows.h> outside of this header (on Windows it comes
//     in via stdafx.h which is pulled in here conditionally).

#pragma once
#ifndef MCASTER1_PLATFORM_H
#define MCASTER1_PLATFORM_H

#ifdef _WIN32
/* ── Windows (MSVC / VS2022) ─────────────────────────────────────────────── */
#include "stdafx.h"          /* precompiled header — must come first on MSVC  */
#include <windows.h>
#include <io.h>              /* _access, _stat64                              */

/* String helpers — MSVC names */
#define mc_snprintf     _snprintf
#define mc_stricmp      _stricmp
#define mc_strcasecmp   _stricmp
#define mc_strdup       _strdup

/* File-stat helpers — MSVC uses _stat64 for large-file support */
#define mc_stat         _stat64
#define mc_stat_t       struct _stat64

/* Path separator */
#define MC_PATH_SEP     "\\"

#else
/* ── Linux / POSIX (GCC / Clang) ─────────────────────────────────────────── */
#include <strings.h>         /* strcasecmp, strncasecmp                       */
#include <sys/stat.h>        /* stat, struct stat                             */
#include <sys/types.h>
#include <unistd.h>

/* String helpers — POSIX names */
#define mc_snprintf     snprintf
#define mc_stricmp      strcasecmp
#define mc_strcasecmp   strcasecmp
#define mc_strdup       strdup

/* File-stat helpers — POSIX uses stat (already 64-bit on modern Linux) */
#define mc_stat         stat
#define mc_stat_t       struct stat

/* Path separator */
#define MC_PATH_SEP     "/"

#endif /* _WIN32 */

#endif /* MCASTER1_PLATFORM_H */
