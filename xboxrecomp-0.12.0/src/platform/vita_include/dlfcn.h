/* PS Vita: no dynamic loader for host libraries. The runtime only uses
 * dlopen/dlsym for optional desktop extras (X11 helpers, SDL message boxes
 * looked up at run time); every caller already copes with NULL. */
#ifndef XBOXRECOMP_VITA_DLFCN_H
#define XBOXRECOMP_VITA_DLFCN_H
#define RTLD_LAZY    1
#define RTLD_NOW     2
#define RTLD_LOCAL   0
#define RTLD_GLOBAL  0x100
#define RTLD_DEFAULT ((void *)0)
static inline void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline int dlclose(void *h) { (void)h; return 0; }
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
static inline int dladdr(const void *a, Dl_info *i) { (void)a; if (i) { i->dli_fname = 0; i->dli_fbase = 0; i->dli_sname = 0; i->dli_saddr = 0; } return 0; }
static inline char *dlerror(void) { return (char *)"dlopen is not available on PS Vita"; }
#endif
