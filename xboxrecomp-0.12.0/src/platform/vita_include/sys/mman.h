/* PS Vita: no mmap. The POSIX layer (platform/win32_compat.c) uses it for
 * VirtualAlloc and the file-mapping objects. In the compact guest model
 * (RECOMP_COMPACT_GUEST, always on for the Vita) nothing needs a fixed host
 * address or an aliased view, so anonymous mappings become zeroed, 4 KB
 * aligned heap blocks and every other request fails cleanly.
 *   - MAP_FIXED / an fd-backed mapping: MAP_FAILED (callers fall back or
 *     report it; none of those paths runs in compact mode)
 *   - munmap: frees only a whole block it handed out; a partial unmap (the
 *     arena trims alignment slack that way) is ignored
 *   - mprotect: accepted, no effect (the Vita's user heap is always RW) */
#ifndef XBOXRECOMP_VITA_MMAN_H
#define XBOXRECOMP_VITA_MMAN_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FAILED    ((void *)-1)
#define MADV_DONTNEED 4
#define MS_SYNC       4

void *xr_vita_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   xr_vita_munmap(void *addr, size_t len);

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{ return xr_vita_mmap(addr, len, prot, flags, fd, off); }
static inline int munmap(void *addr, size_t len) { return xr_vita_munmap(addr, len); }
static inline int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return 0; }
static inline int madvise(void *addr, size_t len, int adv) { (void)addr; (void)len; (void)adv; return 0; }
static inline int msync(void *addr, size_t len, int fl) { (void)addr; (void)len; (void)fl; return 0; }
static inline int shm_open(const char *n, int o, int m) { (void)n; (void)o; (void)m; return -1; }
static inline int shm_unlink(const char *n) { (void)n; return -1; }
#endif
