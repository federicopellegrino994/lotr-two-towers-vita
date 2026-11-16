/* vgl_glt.c -- (v231, PS Vita speed) THE GL WORKER THREAD.
 *
 * The render thread (A) reads the Xbox GPU's commands and turns them into
 * vitaGL calls; vitaGL then builds the GXM commands and waits for the GPU
 * (sceGxmEndScene). In the draw-heavy levels (Helm's Deep 2, Rohan 2) A is
 * busy 60+ ms a frame, ~30% of it inside vitaGL and GXM. Here every vitaGL
 * call A makes is queued to a second thread (B, the worker, on cores 1/2 at a
 * priority below the sound's) that makes the call itself, so the two halves
 * overlap -- the idea of Mesa's glthread.
 *
 * Every vitaGL entry point the renderer uses is --wrap'd (the list:
 * tools/glt/gen_glt.py, which writes vgl_glt_gen.inc): asynchronous ones are
 * queued with their arguments (and a copy of what a pointer argument points
 * to); one that answers something, or reads through a pointer later, is
 * synchronous (A waits for B to have run it, so the order is the same and the
 * pointers stay valid). Client vertex arrays and indices are copied into the
 * queue at the draw (vitaGL copies them at the draw itself, so B points the
 * attributes at the copies for the draw and back after). vitaGL's own state
 * that the renderer pokes directly (the fake buffer objects' memory words,
 * vertex_array_unit, the depth surface) is set through glt_store32 /
 * glt_va_pointer_unit / glt_call so it happens on B in order.
 *
 * At most one finished frame waits in the queue (the swap waits for the one
 * before). Calls from B itself, or with the worker off, go straight to
 * vitaGL; any other thread's calls are queued too (under the same lock).
 * TT_GLT=0: off (everything on the render thread, as before). [GLT] every
 * 10 s: commands, bytes, waits. */
#if defined(__vita__)
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

typedef void (*GltThunk)(const uint32_t *a, uint8_t *pl, uint32_t *res);
static inline uint32_t glt_f2u(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }
static inline float glt_u2f(uint32_t u) { union { float f; uint32_t u; } c; c.u = u; return c.f; }
#if defined(GLT_HOST_TEST)
extern uint32_t glt_host_tp(void);
static inline uint32_t glt_tp(void) { return glt_host_tp(); }
#else
static inline uint32_t glt_tp(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v)); return v; }
#endif

volatile int g_glt_on;                 /* the worker takes the calls */
static uint32_t s_tpA, s_tpB;
volatile const char *g_glt_where;      /* (B's zone, for [RTPROF]) */

#define GLT_RING (8u << 20)
#define GLT_SKIP 1u
#define GLT_WAKE 2u
typedef struct {
    uint32_t size;                      /* the whole command, 8-aligned */
    uint16_t flags, nw;
    GltThunk fn;
    volatile uint32_t *res, *done;
    uint8_t *pl;                        /* the payload (in the ring, or the caller's own memory when synchronous) */
    uint32_t pad;
} GltCmd;                               /* 24 bytes, then nw words, then the payload */
static uint8_t *s_ring;
static volatile uint32_t s_w, s_r;      /* bytes written / read, ever */
static uint32_t s_wp;                   /* (where the command being written starts: under the lock) */
static SceUID s_semB = -1, s_semA = -1, s_thB = -1;
static volatile int s_Bsleep, s_Await;
static SceKernelLwMutexWork s_lock;
static volatile uint32_t s_swaps_posted, s_swaps_done, s_frame_base;
static volatile int s_skip_draw;        /* (B: the next draw has no vertices -- its copy found no room) */

/* stats ([GLT]) */
static volatile uint32_t st_cmds, st_bytes, st_sync, st_copies, st_copyb, st_syncdraw, st_foreign;
static volatile uint64_t st_wait_us[4];   /* 0 room, 1 a synchronous call, 2 the frame before, 3 empty (draining) */
static const char *volatile st_last_sync;
static struct { const char *nm; uint32_t n; } st_sn[16];

static inline int glt_direct(void)
{
    if (!g_glt_on) return 1;
    return glt_tp() == s_tpB;
}
/* the caller is the render thread and the worker takes its calls */
int glt_active(void) { return g_glt_on && glt_tp() == s_tpA; }
int glt_on_worker(void) { return g_glt_on && glt_tp() == s_tpB; }
volatile const char **glt_where_ptr(void)
{
    extern volatile const char *g_vgl_where;
    return glt_on_worker() ? &g_glt_where : &g_vgl_where;
}

static inline uint64_t glt_now(void) { return sceKernelGetProcessTimeWide(); }
static int c_room(void *arg) { uint32_t need = (uint32_t)(uintptr_t)arg; return GLT_RING - (__atomic_load_n(&s_w, __ATOMIC_ACQUIRE) - __atomic_load_n(&s_r, __ATOMIC_ACQUIRE)) >= need; }
static int c_done(void *arg) { return __atomic_load_n((volatile uint32_t *)arg, __ATOMIC_ACQUIRE) != 0; }
static int c_empty(void *arg) { (void)arg; return __atomic_load_n(&s_r, __ATOMIC_ACQUIRE) == __atomic_load_n(&s_w, __ATOMIC_ACQUIRE); }
static int c_lag(void *arg) { (void)arg; return s_swaps_posted - __atomic_load_n(&s_swaps_done, __ATOMIC_ACQUIRE) <= 1u; }
static void glt_wait(int (*cond)(void *), void *arg, int why, int mode)
{
    uint64_t t0;
    int k;
    if (cond(arg)) return;
    t0 = glt_now();
    for (k = 0; k < 200; k++) { if (cond(arg)) goto out; __asm__ volatile("" ::: "memory"); }
    for (;;) {
        SceUInt to = 2000;
        __atomic_store_n(&s_Await, mode, __ATOMIC_SEQ_CST);
        if (cond(arg)) break;
        sceKernelWaitSema(s_semA, 1, &to);
    }
    __atomic_store_n(&s_Await, 0, __ATOMIC_SEQ_CST);
out:
    st_wait_us[why] += glt_now() - t0;
}

/* a command of `size` bytes: room made, the slot returned (under the lock) */
static GltCmd *glt_begin(uint32_t size)
{
    for (;;) {
        const uint32_t w = s_w, pos = w % GLT_RING, tail = GLT_RING - pos;
        const uint32_t need = size <= tail ? size : tail + size;
        if (GLT_RING - (w - __atomic_load_n(&s_r, __ATOMIC_ACQUIRE)) >= need + 8u) {
            if (size > tail) {
                GltCmd *sk = (GltCmd *)(s_ring + pos);
                sk->size = tail; sk->flags = GLT_SKIP; sk->nw = 0; sk->fn = NULL; sk->res = sk->done = NULL; sk->pl = NULL;
                s_wp = w + tail;
                return (GltCmd *)s_ring;
            }
            s_wp = w;
            return (GltCmd *)(s_ring + pos);
        }
        glt_wait(c_room, (void *)(uintptr_t)(need + 8u), 0, 2);
    }
}
static void glt_end(uint32_t size)
{
    __atomic_store_n(&s_w, s_wp + size, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&s_Bsleep, __ATOMIC_SEQ_CST) && __atomic_exchange_n(&s_Bsleep, 0, __ATOMIC_SEQ_CST))
        sceKernelSignalSema(s_semB, 1);
    st_cmds++; st_bytes += size;
}
static inline void glt_lock(void) { sceKernelLockLwMutex(&s_lock, 1, NULL); }
static inline void glt_unlock(void) { sceKernelUnlockLwMutex(&s_lock, 1); }

/* queued: fn(args, payload) on B. pl/plen: copied into the queue (a large one: run synchronously instead) */
static void glt_put(GltThunk fn, const uint32_t *w, uint32_t nw, const void *pl, uint32_t plen, volatile uint32_t *res,
                    volatile uint32_t *done, uint16_t flags, int copy)
{
    const uint32_t ho = (uint32_t)sizeof(GltCmd) + nw * 4u, po = (ho + 7u) & ~7u;
    const uint32_t size = (po + (copy ? plen : 0u) + 7u) & ~7u;
    GltCmd *c;
    if (glt_tp() != s_tpA) st_foreign++;
    glt_lock();
    c = glt_begin(size);
    c->size = size; c->flags = flags; c->nw = (uint16_t)nw; c->fn = fn; c->res = res; c->done = done;
    if (nw) memcpy(c + 1, w, nw * 4u);
    if (copy && plen) { c->pl = (uint8_t *)c + po; memcpy(c->pl, pl, plen); }
    else c->pl = (uint8_t *)(uintptr_t)pl;
    glt_end(size);
    glt_unlock();
}
static uint32_t glt_sync_pl(GltThunk fn, const uint32_t *w, uint32_t nw, const void *pl, const char *nm)
{
    volatile uint32_t res = 0, done = 0;
    int i;
    st_sync++; st_last_sync = nm;
    for (i = 0; i < 16; i++) if (st_sn[i].nm == nm || !st_sn[i].nm) { st_sn[i].nm = nm; st_sn[i].n++; break; }
    glt_put(fn, w, nw, pl, 0, &res, &done, GLT_WAKE, 0);
    glt_wait(c_done, (void *)&done, 1, 1);
    return res;
}
static void glt_post(GltThunk fn, const uint32_t *w, uint32_t nw, const void *pl, uint32_t plen, const char *nm)
{
    if (plen > GLT_RING / 8u) { glt_sync_pl(fn, w, nw, pl, nm); return; }
    glt_put(fn, w, nw, pl, plen, NULL, NULL, 0, 1);
}
static uint32_t glt_sync(GltThunk fn, const uint32_t *w, uint32_t nw, const char *nm) { return glt_sync_pl(fn, w, nw, NULL, nm); }

#include "vgl_glt_gen.inc"

/* ---- the worker ---- */
static int glt_worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    s_tpB = glt_tp();
    for (;;) {
        const uint32_t r = s_r;
        uint32_t w = __atomic_load_n(&s_w, __ATOMIC_ACQUIRE);
        GltCmd *c;
        if (r == w) {
            int k;
            g_glt_where = "glt: waiting for the render thread";
            for (k = 0; k < 400 && (w = __atomic_load_n(&s_w, __ATOMIC_ACQUIRE)) == r; k++) __asm__ volatile("" ::: "memory");
            if (w == r) {
                __atomic_store_n(&s_Bsleep, 1, __ATOMIC_SEQ_CST);
                if (__atomic_load_n(&s_w, __ATOMIC_SEQ_CST) == r) { SceUInt to = 4000; sceKernelWaitSema(s_semB, 1, &to); }
                __atomic_store_n(&s_Bsleep, 0, __ATOMIC_SEQ_CST);
                continue;
            }
        }
        c = (GltCmd *)(s_ring + (r % GLT_RING));
        {
            const uint32_t sz = c->size;
            const uint16_t fl = c->flags;
            if (!(fl & GLT_SKIP)) {
                uint32_t dummy;
                g_glt_where = "glt: vitaGL";
                c->fn((const uint32_t *)(c + 1), c->pl, c->res ? (uint32_t *)c->res : &dummy);
                if (c->done) __atomic_store_n(c->done, 1u, __ATOMIC_RELEASE);
            }
            __atomic_store_n(&s_r, r + sz, __ATOMIC_SEQ_CST);
            {
                const int a = __atomic_load_n(&s_Await, __ATOMIC_SEQ_CST);
                if (a && ((fl & GLT_WAKE) || a == 2 || r + sz == __atomic_load_n(&s_w, __ATOMIC_ACQUIRE))) sceKernelSignalSema(s_semA, 1);
            }
        }
    }
    return 0;
}

/* ---- the render thread's view of the vertex attributes (to copy client arrays at a draw) ---- */
typedef struct { uint8_t en, client; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void *ptr; } GltAttr;
typedef struct { GLuint vao; GLuint elem; int used; GltAttr a[16]; } GltVao;
static GltVao s_vao[8];
static GltVao *s_cv = &s_vao[0];
static GLuint s_abuf;
static GLint s_vp[4];
static inline int glt_shadow_ok(void) { return !s_tpB || glt_tp() != s_tpB; }
static GltVao *glt_vao(GLuint v)
{
    int i;
    for (i = 0; i < 8; i++) if (s_vao[i].used && s_vao[i].vao == v) return &s_vao[i];
    for (i = 0; i < 8; i++) if (!s_vao[i].used) { memset(&s_vao[i], 0, sizeof s_vao[i]); s_vao[i].used = 1; s_vao[i].vao = v; return &s_vao[i]; }
    return &s_vao[7];
}

static void glt_t_bindbuf(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; __real_glBindBuffer((GLenum)a[0], (GLuint)a[1]); }
void __wrap_glBindBuffer(GLenum target, GLuint buffer)
{
    if (glt_shadow_ok()) {
        if (target == GL_ARRAY_BUFFER) s_abuf = buffer;
        else if (target == GL_ELEMENT_ARRAY_BUFFER) s_cv->elem = buffer;
    }
    if (glt_direct()) { __real_glBindBuffer(target, buffer); return; }
    { uint32_t w[2] = { (uint32_t)target, (uint32_t)buffer }; glt_post(glt_t_bindbuf, w, 2, NULL, 0, "glBindBuffer"); }
}
static void glt_t_bindvao(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; __real_glBindVertexArray((GLuint)a[0]); }
void __wrap_glBindVertexArray(GLuint array)
{
    if (glt_shadow_ok()) s_cv = glt_vao(array);
    if (glt_direct()) { __real_glBindVertexArray(array); return; }
    { uint32_t w[1] = { (uint32_t)array }; glt_post(glt_t_bindvao, w, 1, NULL, 0, "glBindVertexArray"); }
}
static void glt_t_enva(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; if (a[1]) __real_glEnableVertexAttribArray((GLuint)a[0]); else __real_glDisableVertexAttribArray((GLuint)a[0]); }
void __wrap_glEnableVertexAttribArray(GLuint index)
{
    if (glt_shadow_ok() && index < 16) s_cv->a[index].en = 1;
    if (glt_direct()) { __real_glEnableVertexAttribArray(index); return; }
    { uint32_t w[2] = { (uint32_t)index, 1 }; glt_post(glt_t_enva, w, 2, NULL, 0, "glEnableVertexAttribArray"); }
}
void __wrap_glDisableVertexAttribArray(GLuint index)
{
    if (glt_shadow_ok() && index < 16) s_cv->a[index].en = 0;
    if (glt_direct()) { __real_glDisableVertexAttribArray(index); return; }
    { uint32_t w[2] = { (uint32_t)index, 0 }; glt_post(glt_t_enva, w, 2, NULL, 0, "glDisableVertexAttribArray"); }
}
static void glt_t_vap(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)pl; (void)res;
    __real_glVertexAttribPointer((GLuint)a[0], (GLint)a[1], (GLenum)a[2], (GLboolean)a[3], (GLsizei)a[4], (const void *)(uintptr_t)a[5]);
}
void __wrap_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    if (glt_shadow_ok() && index < 16) {
        GltAttr *t = &s_cv->a[index];
        t->client = s_abuf == 0; t->size = size; t->type = type; t->norm = normalized; t->stride = stride; t->ptr = pointer;
    }
    if (glt_direct()) { __real_glVertexAttribPointer(index, size, type, normalized, stride, pointer); return; }
    { uint32_t w[6] = { index, (uint32_t)size, type, normalized, (uint32_t)stride, (uint32_t)(uintptr_t)pointer };
      glt_post(glt_t_vap, w, 6, NULL, 0, "glVertexAttribPointer"); }
}
/* an attribute through one of the renderer's own buffer objects (vertex_array_unit set around the call: nv2a_gl_vita.inc) */
extern uint32_t vertex_array_unit;
static void glt_t_vap_unit(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    const uint32_t was = vertex_array_unit;
    (void)pl; (void)res;
    vertex_array_unit = a[6];
    __real_glVertexAttribPointer((GLuint)a[0], (GLint)a[1], (GLenum)a[2], (GLboolean)a[3], (GLsizei)a[4], (const void *)(uintptr_t)a[5]);
    vertex_array_unit = was;
}
void glt_va_pointer_unit(GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr, void *unit)
{
    uint32_t w[7] = { index, (uint32_t)size, type, norm, (uint32_t)stride, (uint32_t)(uintptr_t)ptr, (uint32_t)(uintptr_t)unit };
    if (glt_shadow_ok() && index < 16) { GltAttr *t = &s_cv->a[index]; t->client = 0; t->size = size; t->type = type; t->norm = norm; t->stride = stride; t->ptr = ptr; }
    if (glt_direct()) { glt_t_vap_unit(w, NULL, NULL); return; }
    glt_post(glt_t_vap_unit, w, 7, NULL, 0, "glVertexAttribPointer (own buffer)");
}
static void glt_t_st32(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; *(volatile uint32_t *)(uintptr_t)a[0] = a[1]; }
void glt_store32(volatile uint32_t *p, uint32_t v)
{
    uint32_t w[2] = { (uint32_t)(uintptr_t)p, v };
    if (glt_direct()) { *p = v; return; }
    glt_post(glt_t_st32, w, 2, NULL, 0, "store");
}
/* fn(data) on B, data copied */
static void glt_t_call(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)res; ((void (*)(const void *, uint32_t))(uintptr_t)a[0])(pl, a[1]); }
void glt_call(void (*fn)(const void *, uint32_t), const void *data, uint32_t len)
{
    uint32_t w[2] = { (uint32_t)(uintptr_t)fn, len };
    if (glt_direct()) { fn(data, len); return; }
    glt_post(glt_t_call, w, 2, data, len, "call");
}
/* (v170's one copy of a draw's vertex span, nv2a_gl.inc dz_place) made on B: the span copied here, then into vitaGL's pool there */
extern uint8_t *vgl_reserve_data_pool(uint32_t size);
extern void *sceClibMemcpy(void *, const void *, unsigned);
static void glt_t_dz(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    const uint32_t len = a[1], mis = a[2];
    uint8_t *p = vgl_reserve_data_pool(len + 32u), *dst;
    (void)res;
    if (!p) { s_skip_draw = 1; return; }
    dst = (uint8_t *)(((uintptr_t)p + 15u) & ~(uintptr_t)15u) + mis;
    sceClibMemcpy(dst, pl, len);
    *(volatile uint32_t *)(uintptr_t)a[0] = (uint32_t)(uintptr_t)dst;
}
void glt_dz_copy(volatile uint32_t *obj_mem, const void *src, uint32_t len, uint32_t mis)
{
    uint32_t w[3] = { (uint32_t)(uintptr_t)obj_mem, len, mis };
    glt_post(glt_t_dz, w, 3, src, len, "vertex span");
}

static void glt_t_viewport(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; __real_glViewport((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); }
void __wrap_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (glt_shadow_ok()) { s_vp[0] = x; s_vp[1] = y; s_vp[2] = width; s_vp[3] = height; }
    if (glt_direct()) { __real_glViewport(x, y, width, height); return; }
    { uint32_t w[4] = { (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height }; glt_post(glt_t_viewport, w, 4, NULL, 0, "glViewport"); }
}
static void glt_t_getiv(const uint32_t *a, uint8_t *pl, uint32_t *res) { (void)pl; (void)res; __real_glGetIntegerv((GLenum)a[0], (GLint *)(uintptr_t)a[1]); }
void __wrap_glGetIntegerv(GLenum pname, GLint *data)
{
    if (glt_direct()) { __real_glGetIntegerv(pname, data); return; }
    if (pname == GL_VIEWPORT && glt_tp() == s_tpA) { memcpy(data, s_vp, sizeof s_vp); return; }
    { uint32_t w[2] = { pname, (uint32_t)(uintptr_t)data }; glt_sync(glt_t_getiv, w, 2, "glGetIntegerv"); }
}

/* ---- the draws ---- */
static uint32_t glt_tsz(GLenum t)
{
    switch (t) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: case 0x140B /* GL_HALF_FLOAT */: case 0x8D61 /* GL_HALF_FLOAT_OES */: return 2;
    default: return 4;
    }
}
typedef struct { uint8_t idx, size, norm, pad; uint32_t type, stride, off; uint32_t orig; } GltDA;   /* per client attribute */
#define GLT_DA_MAX 16
/* the client attributes of the draw, vertices 0..n-1, packed after the command's words (and the indices) */
typedef struct { uint32_t mode, a1, a2, a3, a4, a5, nda, ioff, voff, pad; } GltDraw;   /* (a multiple of 8 bytes) */
static void glt_draw_points(const GltDraw *d, const GltDA *da, uint8_t *pl, int back)
{
    uint32_t i;
    const uint32_t was = vertex_array_unit;
    vertex_array_unit = 0;
    for (i = 0; i < d->nda; i++)
        __real_glVertexAttribPointer(da[i].idx, da[i].size, da[i].type, da[i].norm, (GLsizei)da[i].stride,
                                     back ? (const void *)(uintptr_t)da[i].orig : (const void *)(pl + d->voff + da[i].off));
    vertex_array_unit = was;
}
static void glt_t_draw(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    const GltDraw *d = (const GltDraw *)a;
    const GltDA *da = (const GltDA *)(d + 1);
    (void)res;
    if (s_skip_draw) { s_skip_draw = 0; return; }
    g_glt_where = "glt: vitaGL draw";
    if (d->nda) glt_draw_points(d, da, pl, 0);
    switch (d->mode >> 28) {
    case 0: __real_glDrawArrays(d->mode & 0xFFFFu, (GLint)d->a1, (GLsizei)d->a2); break;
    case 1: __real_glDrawElements(d->mode & 0xFFFFu, (GLsizei)d->a1, (GLenum)d->a2, d->ioff != 0xFFFFFFFFu ? (const void *)(pl + d->ioff) : (const void *)(uintptr_t)d->a3); break;
    default: __real_glDrawRangeElements(d->mode & 0xFFFFu, d->a5, d->a4, (GLsizei)d->a1, (GLenum)d->a2,
                                        d->ioff != 0xFFFFFFFFu ? (const void *)(pl + d->ioff) : (const void *)(uintptr_t)d->a3); break;
    }
    if (d->nda) glt_draw_points(d, da, pl, 1);
}
/* the draw queued with copies of its client arrays (vertices 0..nv-1) and client indices; 0: do it synchronously */
static int glt_draw_post(uint32_t kind, GLenum mode, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t nv,
                         const void *ix, uint32_t ixbytes)
{
    struct { GltDraw d; GltDA da[GLT_DA_MAX]; } m;
    struct { uintptr_t lo, hi; } rg[GLT_DA_MAX];
    int rgi[GLT_DA_MAX];
    uint32_t ro[GLT_DA_MAX];
    uint32_t i, j, nr = 0, at, total, nw, ho, po, size;
    GltCmd *c;
    memset(&m.d, 0, sizeof m.d);
    m.d.mode = (kind << 28) | (mode & 0xFFFFu); m.d.a1 = a1; m.d.a2 = a2; m.d.a3 = a3; m.d.a4 = a4; m.d.a5 = a5;
    m.d.ioff = 0xFFFFFFFFu;
    for (i = 0; i < 16; i++) {
        const GltAttr *t = &s_cv->a[i];
        uintptr_t lo, hi;
        uint32_t esz, st;
        if (!t->en || !t->client || !t->ptr) continue;
        if (m.d.nda == GLT_DA_MAX || !nv) return 0;
        esz = (uint32_t)t->size * glt_tsz(t->type);
        st = t->stride ? (uint32_t)t->stride : esz;
        lo = (uintptr_t)t->ptr; hi = lo + (uintptr_t)st * (nv - 1u) + esz;
        for (j = 0; j < nr; j++) if (lo < rg[j].hi && hi > rg[j].lo) break;   /* (interleaved: one copy) */
        if (j < nr) { if (lo < rg[j].lo) rg[j].lo = lo; if (hi > rg[j].hi) rg[j].hi = hi; }
        else { rg[nr].lo = lo; rg[nr].hi = hi; j = nr++; }
        rgi[m.d.nda] = (int)j;
        m.da[m.d.nda].idx = (uint8_t)i; m.da[m.d.nda].size = (uint8_t)t->size; m.da[m.d.nda].norm = t->norm; m.da[m.d.nda].pad = 0;
        m.da[m.d.nda].type = t->type; m.da[m.d.nda].stride = (uint32_t)t->stride; m.da[m.d.nda].orig = (uint32_t)lo; m.da[m.d.nda].off = 0;
        m.d.nda++;
    }
    /* (ranges grown after the fact can overlap each other: merged again) */
    for (i = 0; i < nr; i++) for (j = i + 1; j < nr; j++)
        if (rg[j].hi > rg[j].lo && rg[i].lo < rg[j].hi && rg[i].hi > rg[j].lo) {
            uint32_t k;
            if (rg[j].lo < rg[i].lo) rg[i].lo = rg[j].lo;
            if (rg[j].hi > rg[i].hi) rg[i].hi = rg[j].hi;
            rg[j].lo = rg[j].hi = 0;
            for (k = 0; k < m.d.nda; k++) if (rgi[k] == (int)j) rgi[k] = (int)i;
            j = i;   /* (look again from i) */
        }
    at = (ixbytes + 7u) & ~7u;
    if (ixbytes) m.d.ioff = 0;
    m.d.voff = 0;
    for (i = 0; i < nr; i++) {
        ro[i] = at;
        if (rg[i].hi > rg[i].lo) at += (uint32_t)((rg[i].hi - rg[i].lo + 7u) & ~(uintptr_t)7u);
    }
    total = at;
    if (total > GLT_RING / 8u) return 0;
    for (i = 0; i < m.d.nda; i++) m.da[i].off = ro[rgi[i]] + (uint32_t)((uintptr_t)m.da[i].orig - rg[rgi[i]].lo);
    nw = (uint32_t)(sizeof m.d + m.d.nda * sizeof m.da[0]) / 4u;
    ho = (uint32_t)sizeof(GltCmd) + nw * 4u; po = (ho + 7u) & ~7u; size = (po + total + 7u) & ~7u;
    if (m.d.nda || ixbytes) { st_copies++; st_copyb += total; }
    if (glt_tp() != s_tpA) st_foreign++;
    glt_lock();
    c = glt_begin(size);
    c->size = size; c->flags = 0; c->nw = (uint16_t)nw; c->fn = glt_t_draw; c->res = c->done = NULL;
    memcpy(c + 1, &m, nw * 4u);
    c->pl = total ? (uint8_t *)c + po : NULL;
    if (ixbytes) memcpy(c->pl, ix, ixbytes);
    for (i = 0; i < nr; i++) if (rg[i].hi > rg[i].lo) memcpy(c->pl + ro[i], (const void *)rg[i].lo, rg[i].hi - rg[i].lo);
    glt_end(size);
    glt_unlock();
    return 1;
}
static void glt_t_drawsync(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)pl; (void)res;
    if (s_skip_draw) { s_skip_draw = 0; return; }
    switch (a[0]) {
    case 0: __real_glDrawArrays((GLenum)a[1], (GLint)a[2], (GLsizei)a[3]); break;
    case 1: __real_glDrawElements((GLenum)a[1], (GLsizei)a[2], (GLenum)a[3], (const void *)(uintptr_t)a[4]); break;
    default: __real_glDrawRangeElements((GLenum)a[1], (GLuint)a[5], (GLuint)a[6], (GLsizei)a[2], (GLenum)a[3], (const void *)(uintptr_t)a[4]); break;
    }
}
static int glt_has_client(void)
{
    int i;
    for (i = 0; i < 16; i++) if (s_cv->a[i].en && s_cv->a[i].client) return 1;
    return 0;
}
void __wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (glt_direct()) { __real_glDrawArrays(mode, first, count); return; }
    if (count <= 0) return;
    if (glt_draw_post(0, mode, (uint32_t)first, (uint32_t)count, 0, 0, 0, (uint32_t)(first + count), NULL, 0)) return;
    { uint32_t w[4] = { 0, mode, (uint32_t)first, (uint32_t)count }; st_syncdraw++; glt_sync(glt_t_drawsync, w, 4, "draw (sync)"); }
}
static uint32_t glt_imax(GLenum type, const void *ix, GLsizei count)
{
    uint32_t m = 0; GLsizei i;
    if (type == GL_UNSIGNED_SHORT) { const uint16_t *s = (const uint16_t *)ix; for (i = 0; i < count; i++) if (s[i] > m) m = s[i]; }
    else if (type == GL_UNSIGNED_INT) { const uint32_t *s = (const uint32_t *)ix; for (i = 0; i < count; i++) if (s[i] > m) m = s[i]; }
    else { const uint8_t *s = (const uint8_t *)ix; for (i = 0; i < count; i++) if (s[i] > m) m = s[i]; }
    return m;
}
void __wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (glt_direct()) { __real_glDrawElements(mode, count, type, indices); return; }
    if (count <= 0) return;
    if (!s_cv->elem) {   /* client indices: copied */
        const uint32_t ib = (uint32_t)count * glt_tsz(type);
        const uint32_t nv = glt_has_client() ? glt_imax(type, indices, count) + 1u : 0u;
        if (glt_draw_post(1, mode, (uint32_t)count, type, 0, 0, 0, nv ? nv : 1u, indices, ib)) return;
    } else if (!glt_has_client()) {
        if (glt_draw_post(1, mode, (uint32_t)count, type, (uint32_t)(uintptr_t)indices, 0, 0, 1, NULL, 0)) return;
    }
    { uint32_t w[5] = { 1, mode, (uint32_t)count, type, (uint32_t)(uintptr_t)indices }; st_syncdraw++; glt_sync(glt_t_drawsync, w, 5, "draw (sync)"); }
}
void __wrap_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices)
{
    if (glt_direct()) { __real_glDrawRangeElements(mode, start, end, count, type, indices); return; }
    if (count <= 0) return;
    if (!s_cv->elem) {
        const uint32_t ib = (uint32_t)count * glt_tsz(type);
        if (glt_draw_post(2, mode, (uint32_t)count, type, 0, end, start, end + 1u, indices, ib)) return;
    } else if (glt_draw_post(2, mode, (uint32_t)count, type, (uint32_t)(uintptr_t)indices, end, start, end + 1u, NULL, 0)) return;
    { uint32_t w[7] = { 2, mode, (uint32_t)count, type, (uint32_t)(uintptr_t)indices, start, end }; st_syncdraw++; glt_sync(glt_t_drawsync, w, 7, "draw (sync)"); }
}

/* ---- the frame ---- */
static void glt_t_swap(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)pl; (void)res;
    g_glt_where = "glt: vglSwapBuffers";
    __real_vglSwapBuffers((GLboolean)a[0]);
    __atomic_add_fetch(&s_swaps_done, 1u, __ATOMIC_SEQ_CST);
}
void __wrap_vglSwapBuffers(GLboolean has_commondialog)
{
    if (glt_direct()) { __real_vglSwapBuffers(has_commondialog); return; }
    glt_wait(c_lag, NULL, 2, 1);   /* (the frame before this one drawn: at most one waits in the queue) */
    s_swaps_posted++;
    { uint32_t w[1] = { has_commondialog }; glt_put(glt_t_swap, w, 1, NULL, 0, NULL, NULL, GLT_WAKE, 1); }
}
uint32_t __wrap_vglGetFrameNumber(void)
{
    if (glt_direct()) return __real_vglGetFrameNumber();
    return s_frame_base + __atomic_load_n(&s_swaps_done, __ATOMIC_ACQUIRE);
}

/* everything queued done (the render thread waits) */
void glt_drain(void)
{
    if (!g_glt_on || glt_tp() == s_tpB) return;
    glt_wait(c_empty, NULL, 3, 2);
}

static void glt_report(void)
{
    static uint64_t tl; static uint32_t fl;
    const uint64_t t = glt_now();
    if (!tl) { tl = t; fl = s_swaps_posted; return; }
    if (t - tl < 10000000ull) return;
    {
        const double f = (double)(s_swaps_posted - fl ? s_swaps_posted - fl : 1u);
        char sb[400]; int k = 0, i;
        for (i = 0; i < 16 && st_sn[i].nm; i++) {
            if (k < (int)sizeof sb - 60) k += snprintf(sb + k, sizeof sb - k, " %s %.1f", st_sn[i].nm, st_sn[i].n / f);
            st_sn[i].n = 0;
        }
        fprintf(stderr, "  [GLT] per frame: %.0f commands, %.0f KB queued (%.0f draws with copies, %.0f KB of them; %.1f synchronous draws), "
                        "%.1f synchronous calls (%s), %.1f from other threads; the render thread waited %.2f ms for room, %.2f for answers, %.2f for the frame before, %.2f draining\n",
                st_cmds / f, st_bytes / 1024.0 / f, st_copies / f, st_copyb / 1024.0 / f, st_syncdraw / f, st_sync / f, k ? sb + 1 : "-", st_foreign / f,
                st_wait_us[0] / 1000.0 / f, st_wait_us[1] / 1000.0 / f, st_wait_us[2] / 1000.0 / f, st_wait_us[3] / 1000.0 / f);
        st_cmds = st_bytes = st_sync = st_copies = st_copyb = st_syncdraw = st_foreign = 0;
        memset((void *)st_wait_us, 0, sizeof st_wait_us);
        tl = t; fl = s_swaps_posted;
    }
}

/* at every present, from the render thread (nv2a_gl.inc gpu_present): starts the worker after the first frames; off
 * (everything drained) while a capture is made */
void glt_present_hook(int direct)
{
    static int frames, mode = -1;
    if (mode < 0) { const char *e = getenv("TT_GLT"); mode = !(e && e[0] == '0'); fprintf(stderr, "  [GLT] the GL worker thread %s\n", mode ? "on (after the first frames)" : "off (TT_GLT=0)"); }
    if (!mode) return;
    if (s_thB < 0) {
        if (++frames < 30) return;
        s_ring = (uint8_t *)memalign(64, GLT_RING);
        s_semB = sceKernelCreateSema("glt_b", 0, 0, 0x7FFFFFFF, NULL);
        s_semA = sceKernelCreateSema("glt_a", 0, 0, 0x7FFFFFFF, NULL);
        if (!s_ring || s_semB < 0 || s_semA < 0 || sceKernelCreateLwMutex(&s_lock, "glt_lock", 2 /* recursive */, 0, NULL) < 0) {
            fprintf(stderr, "  [GLT] could not start (ring %p, semaphores %d %d): off\n", (void *)s_ring, (int)s_semB, (int)s_semA);
            mode = 0; return;
        }
        s_frame_base = __real_vglGetFrameNumber();
        s_tpA = glt_tp();
        {
            int pr = 160;
            const char *e = getenv("TT_GLT_PRIO");
            if (e && atoi(e) > 0) pr = atoi(e);
            s_thB = sceKernelCreateThread("tt_glt", glt_worker, pr, 1u << 20, 0, 0x60000 /* cores 1 and 2 */, NULL);
            if (s_thB < 0 || sceKernelStartThread(s_thB, 0, NULL) < 0) { fprintf(stderr, "  [GLT] no worker thread (%08X): off\n", (unsigned)s_thB); mode = 0; s_thB = -1; return; }
            while (!s_tpB) sceKernelDelayThread(100);
            fprintf(stderr, "  [GLT] the GL worker thread runs (priority %d, cores 1/2, %u MB queue)\n", pr, GLT_RING >> 20);
        }
        __atomic_store_n(&g_glt_on, 1, __ATOMIC_SEQ_CST);
        return;
    }
    if (direct && g_glt_on) { glt_drain(); __atomic_store_n(&g_glt_on, 0, __ATOMIC_SEQ_CST); }
    else if (!direct && !g_glt_on) __atomic_store_n(&g_glt_on, 1, __ATOMIC_SEQ_CST);
    glt_report();
}
#endif
