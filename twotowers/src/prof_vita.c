/* PS Vita: a sampling profiler for the recompiled code.
 *
 * Every generated function stores its address in g_prof_fn on entry and puts
 * its caller's back when it ends (postproc_prof.py, with RECOMP_PROFILE). A
 * thread wakes every millisecond and counts the value it finds; every ten
 * seconds the 25 functions found most often are written to the log
 * ([PROF], share of the samples) and the counts start again. Off with
 * RECOMP_PROF=0. The samples are of every thread that runs guest code; the
 * game thread is most of them. */
#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile uint32_t g_prof_fn;
volatile uint32_t g_prof_tp = 1;   /* (the game's frame thread's TPIDRURO: nv2a_pb_exec.c xbox_PerfWaitNote) */

/* (the report is written by the renderer's thread, prof_take_report: this
 * thread's stderr is not the one the log captures) */
static char s_rep[2048];
static int s_rep_ready;
const char *prof_take_report(void)
{
    static char out[2048];
    if (!__atomic_load_n(&s_rep_ready, __ATOMIC_ACQUIRE)) return NULL;
    memcpy(out, s_rep, sizeof out);
    __atomic_store_n(&s_rep_ready, 0, __ATOMIC_RELEASE);
    return out;
}
static volatile int s_prof_alive;
int prof_alive(void) { return s_prof_alive; }

#define PROF_N 8192
static uint32_t s_key[PROF_N], s_cnt[PROF_N];
/* (v71) the render thread too: the zone it says it is in (g_vgl_where, a
 * string; nv2a_gl.inc VWHERE) and, in a method, which one */
extern volatile const char *g_vgl_where;
extern const char g_vw_method[], g_vw_block[];
extern volatile uint32_t g_rt_meth;
#define RT_N 512
static uint32_t s_rkey[RT_N], s_rcnt[RT_N];
static void rt_count(void)
{
    const char *w = (const char *)g_vgl_where;
    uint32_t key = (uint32_t)(uintptr_t)w, h, i;
    /* (v111) methods as small keys: the Vita's strings are above 0x80000000,
     * and the old 0x80000000/0xC0000000 tags took every zone for a method */
    if (w == g_vw_method) key = 0x10000u | (g_rt_meth & 0xFFFFu);
    else if (w == g_vw_block) key = 0x20000u | (g_rt_meth & 0xFFFFu);
    h = (key * 2654435761u) >> 23;
    for (i = 0; i < RT_N; i++) {
        uint32_t k = (h + i) & (RT_N - 1);
        if (s_rcnt[k] && s_rkey[k] == key) { s_rcnt[k]++; return; }
        if (!s_rcnt[k]) { s_rkey[k] = key; s_rcnt[k] = 1; return; }
    }
}
static char s_rrep[4096];   /* (v176: 40 zones) */
static int s_rrep_ready;
const char *prof_take_rt_report(void)
{
    static char out[4096];
    if (!__atomic_load_n(&s_rrep_ready, __ATOMIC_ACQUIRE)) return NULL;
    memcpy(out, s_rrep, sizeof out);
    __atomic_store_n(&s_rrep_ready, 0, __ATOMIC_RELEASE);
    return out;
}
static void rt_report(uint32_t total)
{
    uint32_t top[40], j, k, n = 0;
    int at;
    if (s_rrep_ready) goto clear;
    for (j = 0; j < 40; j++) {
        uint32_t best = 0, bi = 0, m;
        for (k = 0; k < RT_N; k++) {
            int used = 0;
            for (m = 0; m < n; m++) if (top[m] == k) used = 1;
            if (!used && s_rcnt[k] > best) { best = s_rcnt[k]; bi = k; }
        }
        if (!best) break;
        top[n++] = bi;
    }
    at = snprintf(s_rrep, sizeof s_rrep, "  [RTPROF] the render thread, %u samples:\n", total);
    for (j = 0; j < n && at < (int)sizeof s_rrep - 96; j++) {
        uint32_t key = s_rkey[top[j]];
        double pc = 100.0 * s_rcnt[top[j]] / (total ? total : 1);
        if (key && key < 0x30000u)
            at += snprintf(s_rrep + at, sizeof s_rrep - at, "  [RTPROF]   %5.1f%%  %s %04X\n", pc, key >= 0x20000u ? "method block" : "method", key & 0xFFFFu);
        else
            at += snprintf(s_rrep + at, sizeof s_rrep - at, "  [RTPROF]   %5.1f%%  %s\n", pc, key ? (const char *)(uintptr_t)key : "(none)");
    }
    __atomic_store_n(&s_rrep_ready, 1, __ATOMIC_RELEASE);
clear:
    memset(s_rcnt, 0, sizeof s_rcnt);
    memset(s_rkey, 0, sizeof s_rkey);
}

/* (v144) inclusive time: at each sample, the return addresses found on the
 * game thread's guest stack (its esp as of its last call; words that look
 * like code addresses, each counted once a sample). [PROFINC] every 10 s:
 * the call sites the game thread was under most -- a function's callers
 * show how much of the frame runs beneath them. */
#include "../../xboxrecomp-0.12.0/src/platform/recomp_fastregs.h"
extern uintptr_t g_xbox_region_lin[256];
#define INC_N 8192
static uint32_t s_ikey[INC_N], s_icnt[INC_N], s_itot;
/* (v152) the exact chain: each profiled function's frame links to its
 * caller's (RECOMP_PROF_ENTER, g_prof_top); a sample walks it, every
 * function in it counted once -- its inclusive time. The frames are on the
 * game thread's host stack: a pointer outside it ends the walk. */
typedef struct recomp_prof_s { uint32_t fn; struct recomp_prof_s *up; void *ctx; } recomp_prof_t;
recomp_prof_t *volatile g_prof_top;   /* (unused since v154) */
/* (v154) the game thread's register struct (nv2a_pb_exec.c xbox_PerfWaitNote): its r_prof_top is the chain */
RecompRegs *volatile g_prof_ctx;
static uint32_t s_own;   /* (the sample's own-time function: the chain's first frame) */
extern volatile int g_thid_game;
static uintptr_t s_stk_lo, s_stk_hi;
static void inc_count(void)
{
    RecompRegs *c = g_prof_ctx;
    const recomp_prof_t *f = c ? (const recomp_prof_t *)c->r_prof_top : NULL;
    uint32_t seen[64], ns = 0, k;
    s_own = 0;
    if (!s_stk_hi && g_thid_game > 0) {
        SceKernelThreadInfo ti;
        memset(&ti, 0, sizeof ti); ti.size = sizeof ti;
        if (sceKernelGetThreadInfo(g_thid_game, &ti) >= 0) { s_stk_lo = (uintptr_t)ti.stack; s_stk_hi = s_stk_lo + ti.stackSize; }
    }
    if (!s_stk_hi) return;
    s_itot++;
    for (k = 0; k < 64 && f; k++) {
        uintptr_t a = (uintptr_t)f;
        uint32_t v, i, h, d = 0;
        if (a < s_stk_lo || a + sizeof *f > s_stk_hi || (a & 3)) break;
        v = f->fn;
        if (!k) s_own = v;
        for (i = 0; i < ns; i++) if (seen[i] == v) { d = 1; break; }
        if (!d && ns < 64) {
            seen[ns++] = v;
            h = (v * 2654435761u) >> 19;
            for (i = 0; i < INC_N; i++) {
                uint32_t q = (h + i) & (INC_N - 1);
                if (s_icnt[q] && s_ikey[q] == v) { s_icnt[q]++; break; }
                if (!s_icnt[q]) { s_ikey[q] = v; s_icnt[q] = 1; break; }
            }
        }
        if (f->up && (uintptr_t)f->up <= a) break;   /* (the caller's frame is above on the stack) */
        f = f->up;
    }
}
static char s_irep[4096];
static int s_irep_ready;
const char *prof_take_inc_report(void)
{
    static char out[4096];
    if (!__atomic_load_n(&s_irep_ready, __ATOMIC_ACQUIRE)) return NULL;
    memcpy(out, s_irep, sizeof out);
    __atomic_store_n(&s_irep_ready, 0, __ATOMIC_RELEASE);
    return out;
}
static void inc_report(void)
{
    uint32_t top[60], j, k, n = 0;
    int at;
    if (!s_irep_ready && s_itot) {
        for (j = 0; j < 60; j++) {
            uint32_t best = 0, bi = 0, m;
            for (k = 0; k < INC_N; k++) {
                int used = 0;
                for (m = 0; m < n; m++) if (top[m] == k) used = 1;
                if (!used && s_icnt[k] > best) { best = s_icnt[k]; bi = k; }
            }
            if (!best) break;
            top[n++] = bi;
        }
        at = snprintf(s_irep, sizeof s_irep, "  [PROFINC] game thread, %u samples; inclusive time (function, share of samples inside it):\n  [PROFINC]", s_itot);
        for (j = 0; j < n && at < (int)sizeof s_irep - 40; j++)
            at += snprintf(s_irep + at, sizeof s_irep - at, " %06X %.1f%s", s_ikey[top[j]], 100.0 * s_icnt[top[j]] / s_itot, (j % 8 == 7) ? "\n  [PROFINC]" : ";");
        snprintf(s_irep + at, sizeof s_irep - at, "\n");
        __atomic_store_n(&s_irep_ready, 1, __ATOMIC_RELEASE);
    }
    memset(s_icnt, 0, sizeof s_icnt); memset(s_ikey, 0, sizeof s_ikey); s_itot = 0;
}

static int prof_thread(SceSize args, void *argp)
{
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    uint32_t total = 0;
    (void)args; (void)argp;
    for (;;) {
        uint32_t f, h, i;
        sceKernelDelayThread(1000);
        s_prof_alive = 1;
        inc_count();
        f = s_own;
        h = (f * 2654435761u) >> 19;
        for (i = 0; i < PROF_N; i++) {
            uint32_t k = (h + i) & (PROF_N - 1);
            if (s_key[k] == f && (s_cnt[k] || f == 0)) { s_cnt[k]++; break; }
            if (!s_cnt[k]) { s_key[k] = f; s_cnt[k] = 1; break; }
        }
        total++;
        rt_count();
        if (sceKernelGetProcessTimeWide() - t0 >= 10000000) {
            rt_report(total);
            inc_report();
            uint32_t top[25], j, k, n = 0;
            for (j = 0; j < 25; j++) {
                uint32_t best = 0, bi = 0;
                for (k = 0; k < PROF_N; k++) {
                    int used = 0;
                    uint32_t m;
                    for (m = 0; m < n; m++) if (top[m] == k) used = 1;
                    if (!used && s_cnt[k] > best) { best = s_cnt[k]; bi = k; }
                }
                if (!best) break;
                top[n++] = bi;
            }
            if (!s_rep_ready) {
                int at = snprintf(s_rep, sizeof s_rep, "  [PROF] %u samples in %.1f s; most often running (own time):\n", total,
                                  (double)(sceKernelGetProcessTimeWide() - t0) / 1e6);
                for (j = 0; j < n && at < (int)sizeof s_rep - 64; j++)
                    at += snprintf(s_rep + at, sizeof s_rep - at, "  [PROF]   sub_%08X %5.1f%%\n", s_key[top[j]],
                                   100.0 * s_cnt[top[j]] / (total ? total : 1));
                __atomic_store_n(&s_rep_ready, 1, __ATOMIC_RELEASE);
            }
            memset(s_cnt, 0, sizeof s_cnt);
            memset(s_key, 0, sizeof s_key);
            total = 0;
            t0 = sceKernelGetProcessTimeWide();
        }
    }
    return 0;
}

/* started from the renderer's thread on its first report (pthread: the
 * native thread v38/v39 made never ran); returns a line for the log */
#include <pthread.h>
static void *prof_pthread(void *a)
{
    (void)a;
    { extern int sceKernelChangeThreadCpuAffinityMask(int, int); sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), 0x50000); }   /* (v181: not on the render thread's core) */
    prof_thread(0, NULL); return NULL;
}
const char *prof_start2(void)
{
    static int done;
    static char msg[96];
    pthread_t t;
    pthread_attr_t at;
    int r;
    const char *e = getenv("RECOMP_PROF");
    if (done) return NULL;
    done = 1;
    if (e && e[0] == '0') return "  [PROF] off (RECOMP_PROF=0)\n";
#if defined(TT_NO_PROF)
    (void)at; (void)t; (void)r; (void)msg;
    return "  [PROF] off (a build without the profiler: TT_PROFILE=OFF)\n";
#endif
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    r = pthread_create(&t, &at, prof_pthread, NULL);
    snprintf(msg, sizeof msg, "  [PROF] sampling profiler: %s (%d)\n", r ? "could not start" : "on", r);
    return msg;
}
void prof_start(void) { }
#endif
#if defined(__vita__)
/* (vita-elf-create needs ~4 KB after the text segment for its own data; when
 * the text happens to end just short of a page the data segment overlaps it.
 * This pads the text out by two pages -- change its size if it happens again) */
__attribute__((used)) const char g_vita_text_pad[18432] = { 1 };   /* (v93: 16384 left 984 bytes before the data segment) */
#endif

#if defined(__vita__)
/* (v177) room after the code segment: vita-elf-create puts its tables (~4.5 KB)
 * at the end of segment 0, and the data segment starts at the next 64 KB --
 * the build without the profiler ended 856 bytes short of it */
__attribute__((used, aligned(16))) const unsigned char tt_seg0_pad[8192] = { 1 };
#endif
