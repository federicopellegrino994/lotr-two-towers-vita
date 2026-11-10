/* PS Vita: thread-local storage for __thread / _Thread_local.
 *
 * GCC for arm-vita-eabi has no native TLS: every thread-local access calls
 * __emutls_get_address(&object). libgcc's copy keeps its state behind a
 * zero-initialised gthread mutex set up by a pthread_once -- built against
 * newlib's pthread types, not those of the pthread library the Vita links
 * (pthread-embedded) -- so the once never runs and the first access dies in
 * pthread_mutex_unlock (seen on hardware: WinMain -> CreateDirectoryA ->
 * SetLastError). This definition takes libgcc's place at link time (an
 * object file beats an archive member) and keeps libgcc's ABI:
 *
 *   struct __emutls_object { size, align, loc (index once assigned), templ }
 *
 * Each object gets an index the first time any thread touches it; each
 * thread has an array of pointers (pthread key), grown on demand. The guest
 * register file (RECOMP_TLS) goes through here on every spill, so the fast
 * path is one pthread_getspecific, two loads and a compare. */
#if defined(__vita__)
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

typedef unsigned int xr_word;
struct __emutls_object {
    xr_word size;
    xr_word align;
    union { uintptr_t offset; void *ptr; } loc;
    void *templ;
};

typedef struct { uintptr_t n; void *data[]; } xr_tls_array;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t   s_key;
static volatile int    s_key_ok;
static uintptr_t       s_count;
void *__emutls_get_address_slow(struct __emutls_object *obj);

static int xr_in_ctx(const void *p);
static void xr_tls_destroy(void *p)
{
    xr_tls_array *a = (xr_tls_array *)p;
    uintptr_t i;
    if (!a) return;
    for (i = 0; i < a->n; i++)
        if (a->data[i] && !xr_in_ctx(a->data[i])) free(((void **)a->data[i])[-1]);
    free(a);
}

static uintptr_t xr_tls_index(struct __emutls_object *obj)
{
    uintptr_t idx = __atomic_load_n(&obj->loc.offset, __ATOMIC_ACQUIRE);
    if (idx) return idx;
    pthread_mutex_lock(&s_lock);
    if (!s_key_ok) {
        pthread_key_create(&s_key, xr_tls_destroy);
        __atomic_store_n(&s_key_ok, 1, __ATOMIC_RELEASE);
    }
    idx = obj->loc.offset;
    if (!idx) {
        idx = ++s_count;
        __atomic_store_n(&obj->loc.offset, idx, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&s_lock);
    return idx;
}

/* One variable's storage for this thread, aligned as asked; the block's
 * start is kept just below the returned pointer for the destructor. */
static void *xr_tls_alloc(const struct __emutls_object *obj)
{
    xr_word align = obj->align < sizeof(void *) ? sizeof(void *) : obj->align;
    char *raw = (char *)malloc(obj->size + align + sizeof(void *));
    char *p;
    if (!raw) abort();
    p = (char *)(((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1));
    ((void **)p)[-1] = raw;
    if (obj->templ) memcpy(p, obj->templ, obj->size);
    else memset(p, 0, obj->size);
    return p;
}

/* The fast path. pthread_getspecific is a long way round here (pte ->
 * newlib's reent -> sceKernelGetTLSAddr -> a list walk) and the recompiled
 * code reads and writes its guest registers through this on every call, so
 * each thread's array is also kept in two of the thread's own TLS words
 * (slots 0x8A, 0x8B: the SDK's libraries use 0x89 only): [0] the array,
 * [1] the thread pointer (TPIDRURO) it belongs to. When those words sit at a
 * fixed distance from TPIDRURO -- checked on every thread's first slow call
 * -- a lookup is a coprocessor read, two loads and two compares. */
#include <psp2/kernel/threadmgr.h>
#define XR_SLOT 0x8A
static int s_fast = -1;          /* -1 not known yet, 1 slots at TPIDRURO + s_off, 0 no */
static uintptr_t s_off;
static inline uintptr_t xr_tp(void)
{
    uintptr_t v;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v));
    return v;
}
static void xr_slot_set(xr_tls_array *a)
{
    void **sl = (void **)sceKernelGetTLSAddr(XR_SLOT);
    uintptr_t t = xr_tp();
    if (!sl) { s_fast = 0; return; }
    sl[0] = a; sl[1] = (void *)t;
    if (s_fast < 0) { s_off = (uintptr_t)sl - t; s_fast = s_off < 0x1000 ? 1 : 0; }
    else if (s_fast && (uintptr_t)sl != t + s_off) s_fast = 0;
}

void *__emutls_get_address(struct __emutls_object *obj)
{
    uintptr_t idx = __atomic_load_n(&obj->loc.offset, __ATOMIC_RELAXED);
    if (idx && s_fast > 0) {
        uintptr_t t = xr_tp();
        void **sl = (void **)(t + s_off);
        if (sl[1] == (void *)t) {
            xr_tls_array *fa = (xr_tls_array *)sl[0];
            if (fa && idx <= fa->n && fa->data[idx - 1]) return fa->data[idx - 1];
        }
    }
    return __emutls_get_address_slow(obj);
}

/* ---- the guest register file in one struct (recomp_fastregs.h) ----
 * The generated code (RECOMP_FAST_REGS) reaches it inline through
 * TPIDRURO + g_recomp_ctx_off (TLS slots 0x8C/0x8D: [0] the struct, [1] the
 * thread pointer); the host code's g_eax ... go through emutls as before, and
 * those variables' storage is placed in the struct here (xr_ctx_field), so
 * both see the same registers. */
#include "recomp_fastregs.h"
#include <stddef.h>
#define XR_CTX_SLOT 0x8C
uint32_t g_recomp_ctx_off = 0;   /* (until the first thread sets it: reads the thread's own TLS words, whose check word does not match) */
#define XR_EV(n) extern struct __emutls_object xr_ev_##n __asm__("__emutls_v." #n);
XR_EV(g_eax) XR_EV(g_ecx) XR_EV(g_edx) XR_EV(g_esp) XR_EV(g_ebx) XR_EV(g_esi) XR_EV(g_edi) XR_EV(g_ebp)
XR_EV(g_seh_ebp) XR_EV(g_fs_base) XR_EV(g_df) XR_EV(g_fp_top) XR_EV(g_fp_cmp) XR_EV(g_fp_control_word)
XR_EV(g_fp_cc) XR_EV(g_fp_stack)
XR_EV(g_xmm0) XR_EV(g_xmm1) XR_EV(g_xmm2) XR_EV(g_xmm3) XR_EV(g_xmm4) XR_EV(g_xmm5) XR_EV(g_xmm6) XR_EV(g_xmm7)
XR_EV(g_mm0) XR_EV(g_mm1) XR_EV(g_mm2) XR_EV(g_mm3) XR_EV(g_mm4) XR_EV(g_mm5) XR_EV(g_mm6) XR_EV(g_mm7)
static const struct { struct __emutls_object *o; uint16_t off, size; } s_ctxmap[] = {
#define XR_F(n, f) { &xr_ev_##n, offsetof(RecompRegs, f), sizeof(((RecompRegs *)0)->f) },
    XR_F(g_eax, r_eax) XR_F(g_ecx, r_ecx) XR_F(g_edx, r_edx) XR_F(g_esp, r_esp) XR_F(g_ebx, r_ebx) XR_F(g_esi, r_esi)
    XR_F(g_edi, r_edi) XR_F(g_ebp, r_ebp) XR_F(g_seh_ebp, r_seh_ebp) XR_F(g_fs_base, r_fs_base) XR_F(g_df, r_df)
    XR_F(g_fp_top, r_fp_top) XR_F(g_fp_cmp, r_fp_cmp) XR_F(g_fp_control_word, r_fp_control_word) XR_F(g_fp_cc, r_fp_cc)
    XR_F(g_fp_stack, r_fp_stack)
    XR_F(g_xmm0, r_xmm[0]) XR_F(g_xmm1, r_xmm[1]) XR_F(g_xmm2, r_xmm[2]) XR_F(g_xmm3, r_xmm[3])
    XR_F(g_xmm4, r_xmm[4]) XR_F(g_xmm5, r_xmm[5]) XR_F(g_xmm6, r_xmm[6]) XR_F(g_xmm7, r_xmm[7])
    XR_F(g_mm0, r_mm[0]) XR_F(g_mm1, r_mm[1]) XR_F(g_mm2, r_mm[2]) XR_F(g_mm3, r_mm[3])
    XR_F(g_mm4, r_mm[4]) XR_F(g_mm5, r_mm[5]) XR_F(g_mm6, r_mm[6]) XR_F(g_mm7, r_mm[7])
#undef XR_F
};
#define XR_NMAP (sizeof s_ctxmap / sizeof s_ctxmap[0])
static int s_ctx_off_set;

RecompRegs *recomp_ctx_slow(void)
{
    void **sl = (void **)sceKernelGetTLSAddr(XR_CTX_SLOT);
    uintptr_t t = xr_tp();
    RecompRegs *c;
    unsigned i;
    if (!sl) abort();
    if (sl[0] && sl[1] == (void *)t) c = (RecompRegs *)sl[0];
    else {
        c = (RecompRegs *)calloc(1, sizeof *c);
        if (!c) abort();
        for (i = 0; i < XR_NMAP; i++) {   /* (the variables' initial values: g_fs_base, g_fp_control_word) */
            const struct __emutls_object *o = s_ctxmap[i].o;
            if (o->templ && o->size <= s_ctxmap[i].size) memcpy((char *)c + s_ctxmap[i].off, o->templ, o->size);
        }
        sl[0] = c; sl[1] = (void *)t;
    }
    if (!s_ctx_off_set) { g_recomp_ctx_off = (uint32_t)((uintptr_t)sl - t); s_ctx_off_set = 1; }
    return c;
}
static void *xr_ctx_field(const struct __emutls_object *obj)
{
    unsigned i;
    for (i = 0; i < XR_NMAP; i++)
        if (s_ctxmap[i].o == obj) {
            if (obj->size > s_ctxmap[i].size) abort();   /* (a field smaller than the variable: the layout is wrong) */
            return (char *)recomp_ctx_slow() + s_ctxmap[i].off;
        }
    return NULL;
}
static int xr_in_ctx(const void *p)
{
    void **sl = (void **)sceKernelGetTLSAddr(XR_CTX_SLOT);
    const char *c = sl ? (const char *)sl[0] : NULL;
    return c && (const char *)p >= c && (const char *)p < c + sizeof(RecompRegs);
}

void *__emutls_get_address_slow(struct __emutls_object *obj)
{
    uintptr_t idx = xr_tls_index(obj);
    xr_tls_array *a = (xr_tls_array *)pthread_getspecific(s_key);
    void *p;
    if (!a || idx > a->n) {
        uintptr_t n = a ? a->n : 0, want = idx + 32;
        xr_tls_array *b = (xr_tls_array *)realloc(a, sizeof *a + want * sizeof(void *));
        if (!b) abort();
        memset(&b->data[n], 0, (want - n) * sizeof(void *));
        b->n = want;
        a = b;
        pthread_setspecific(s_key, a);
    }
    p = a->data[idx - 1];
    if (!p) {
        p = xr_ctx_field(obj);
        if (!p) p = xr_tls_alloc(obj);
        a->data[idx - 1] = p;
    }
    xr_slot_set(a);
    return p;
}

void __emutls_register_common(struct __emutls_object *obj, xr_word size,
                              xr_word align, void *templ)
{
    if (obj->size < size) { obj->size = size; obj->templ = NULL; }
    if (obj->align < align) obj->align = align;
    if (templ && size == obj->size) obj->templ = templ;
}
#endif
