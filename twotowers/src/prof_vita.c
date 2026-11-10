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

static int prof_thread(SceSize args, void *argp)
{
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    uint32_t total = 0;
    (void)args; (void)argp;
    for (;;) {
        uint32_t f = g_prof_fn, h, i;
        sceKernelDelayThread(1000);
        s_prof_alive = 1;
        h = (f * 2654435761u) >> 19;
        for (i = 0; i < PROF_N; i++) {
            uint32_t k = (h + i) & (PROF_N - 1);
            if (s_key[k] == f && (s_cnt[k] || f == 0)) { s_cnt[k]++; break; }
            if (!s_cnt[k]) { s_key[k] = f; s_cnt[k] = 1; break; }
        }
        total++;
        if (sceKernelGetProcessTimeWide() - t0 >= 10000000) {
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
static void *prof_pthread(void *a) { (void)a; prof_thread(0, NULL); return NULL; }
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
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    r = pthread_create(&t, &at, prof_pthread, NULL);
    snprintf(msg, sizeof msg, "  [PROF] sampling profiler: %s (%d)\n", r ? "could not start" : "on", r);
    return msg;
}
void prof_start(void) { }
#endif
