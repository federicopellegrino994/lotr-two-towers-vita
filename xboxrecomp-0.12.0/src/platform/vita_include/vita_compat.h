/* Force-included in every PS Vita translation unit (CMakeLists.txt).
 * Fills the gaps between newlib on the Vita and the POSIX the Linux build
 * of the runtime is written against. */
#ifndef XBOXRECOMP_VITA_COMPAT_H
#define XBOXRECOMP_VITA_COMPAT_H
#if defined(__vita__)
#include <limits.h>
#include <time.h>
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
/* No per-thread CPU clock: wall time is what the profiling lines then show. */
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID CLOCK_MONOTONIC
#endif
time_t timegm(struct tm *tm);   /* platform/vita_mman.c */
#endif
#endif
