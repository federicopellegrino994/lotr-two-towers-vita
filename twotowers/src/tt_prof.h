/* tt_prof.h -- (v154) hand-written code in the profiler's frame chain.
 *
 * The generated functions link a frame into their thread's register struct
 * on entry (postproc_prof.py); time spent in a hand-written hook was counted
 * as its caller's own time. TT_PROF_NATIVE(tag) at the top of a hook links
 * one for it: [PROF] and [PROFINC] show it under the tag, F0xxxxxx and up
 * (F1000000 + id: tt_hk's hooks; F2xxxxxx: a native function at xxxxxx). */
#ifndef TT_PROF_H
#define TT_PROF_H
#if defined(__vita__)
#include <stdint.h>
#include "../../xboxrecomp-0.12.0/src/platform/recomp_fastregs.h"
#endif
#if defined(__vita__) && !defined(TT_NO_PROF)
typedef struct tt_pf_s { uint32_t fn; void *up; void *ctx; } tt_pf_t;
static inline void tt_pf_leave_(volatile tt_pf_t *p) { *(void *volatile *)&((RecompRegs *)p->ctx)->r_prof_top = p->up; }
#define TT_PROF_NATIVE(tag) RecompRegs *const _pfc = recomp_ctx(); \
    volatile tt_pf_t _pf __attribute__((cleanup(tt_pf_leave_))); _pf.fn = (uint32_t)(tag); _pf.up = _pfc->r_prof_top; _pf.ctx = _pfc; \
    *(void *volatile *)&_pfc->r_prof_top = (void *)&_pf
#else
#define TT_PROF_NATIVE(tag) (void)0
#endif
#endif
