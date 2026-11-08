/* PS Vita mmap emulation -- see vita_include/sys/mman.h. */
#if defined(__vita__)
#include <sys/mman.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>

/* Blocks handed out, so munmap frees only what mmap returned (a tiny table:
 * the runtime makes a few dozen anonymous mappings in a session). */
#define XR_MAX_MAPS 512
static struct { void *p; size_t len; } s_maps[XR_MAX_MAPS];
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

void *xr_vita_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    void *p;
    int i;
    (void)prot; (void)off;
    if (!len || (flags & MAP_FIXED) || !(flags & MAP_ANONYMOUS) || fd != -1) {
        (void)addr;
        errno = ENOMEM;
        return MAP_FAILED;
    }
    p = memalign(4096, len);
    if (!p) { errno = ENOMEM; return MAP_FAILED; }
    memset(p, 0, len);
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < XR_MAX_MAPS; i++)
        if (!s_maps[i].p) { s_maps[i].p = p; s_maps[i].len = len; break; }
    pthread_mutex_unlock(&s_lock);
    return p;
}

int xr_vita_munmap(void *addr, size_t len)
{
    int i;
    (void)len;
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < XR_MAX_MAPS; i++)
        if (s_maps[i].p == addr) {
            s_maps[i].p = NULL;
            pthread_mutex_unlock(&s_lock);
            free(addr);
            return 0;
        }
    pthread_mutex_unlock(&s_lock);
    return 0;   /* partial or foreign range: nothing to give back */
}
#endif

#if defined(__vita__)
#include <time.h>
/* newlib on the Vita has no timegm: seconds since the epoch for a UTC
 * broken-down time (days-from-civil, proleptic Gregorian). */
time_t timegm(struct tm *tm)
{
    long y = tm->tm_year + 1900L, m = tm->tm_mon + 1L;
    long era, yoe, doy, doe, days;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + tm->tm_mday - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + doe - 719468;
    return (time_t)((((long long)days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}
#endif

#if defined(__vita__)
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

/* Pieces of POSIX newlib on the Vita does not provide. */
int posix_memalign(void **out, size_t align, size_t size)
{
    void *p;
    if (!out || align < sizeof(void *) || (align & (align - 1))) return EINVAL;
    p = memalign(align, size ? size : 1);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

int backtrace(void **buf, int n) { (void)buf; (void)n; return 0; }

ssize_t readlink(const char *path, char *buf, size_t len)
{ (void)path; (void)buf; (void)len; errno = ENOSYS; return -1; }

/* fnmatch: '*', '?', '[...]' (with '!'/'^' and ranges), '\' escapes; the
 * runtime only matches single file names (no FNM_PATHNAME needed). */
int fnmatch(const char *p, const char *s, int flags)
{
    (void)flags;
    for (;; p++, s++) {
        switch (*p) {
        case 0:
            return *s ? 1 : 0;
        case '?':
            if (!*s) return 1;
            break;
        case '*':
            while (*p == '*') p++;
            if (!*p) return 0;
            for (; *s; s++) if (!fnmatch(p, s, flags)) return 0;
            return 1;
        case '[': {
            const char *q = p + 1;
            int neg = 0, ok = 0;
            if (!*s) return 1;
            if (*q == '!' || *q == '^') { neg = 1; q++; }
            do {
                unsigned char lo = (unsigned char)*q, hi = lo;
                if (!*q) return 1;
                if (q[1] == '-' && q[2] && q[2] != ']') { hi = (unsigned char)q[2]; q += 2; }
                if ((unsigned char)*s >= lo && (unsigned char)*s <= hi) ok = 1;
                q++;
            } while (*q != ']');
            if (ok == neg) return 1;
            p = q;
            break; }
        case '\\':
            if (p[1]) p++;
            /* fall through */
        default:
            if (*p != *s) return 1;
        }
    }
}
#endif
