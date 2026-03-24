/*
 * kiss_fft_log.h — Logging stub for kiss_fft
 * Redirects kiss_fft debug/warning output to stderr.
 * This file is required by _kiss_fft_guts.h in newer kiss_fft versions.
 */

#ifndef KISS_FFT_LOG_H
#define KISS_FFT_LOG_H

#include <stdio.h>

#ifndef KISS_FFT_LOG_LEVEL
#define KISS_FFT_LOG_LEVEL 0  /* 0 = silent, 1 = errors, 2 = warnings, 3 = info */
#endif

#if KISS_FFT_LOG_LEVEL >= 1
#define KISS_FFT_ERROR(msg, ...) fprintf(stderr, "[kiss_fft ERROR] " msg "\n", ##__VA_ARGS__)
#else
#define KISS_FFT_ERROR(msg, ...) ((void)0)
#endif

#if KISS_FFT_LOG_LEVEL >= 2
#define KISS_FFT_WARNING(msg, ...) fprintf(stderr, "[kiss_fft WARN] " msg "\n", ##__VA_ARGS__)
#else
#define KISS_FFT_WARNING(msg, ...) ((void)0)
#endif

#if KISS_FFT_LOG_LEVEL >= 3
#define KISS_FFT_LOG(msg, ...) fprintf(stderr, "[kiss_fft] " msg "\n", ##__VA_ARGS__)
#else
#define KISS_FFT_LOG(msg, ...) ((void)0)
#endif

#endif /* KISS_FFT_LOG_H */
