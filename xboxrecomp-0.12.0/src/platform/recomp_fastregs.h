/* PS Vita: the guest register file for the generated code without emutls.
 *
 * The recompiled code keeps the x86 registers (g_eax ... g_xmm7) as
 * thread-locals, and on the Vita every access to one is a call into
 * __emutls_get_address -- some twenty of them in an ordinary function's
 * prologue, epilogue and calls. With RECOMP_FAST_REGS (set for the generated
 * sources only) recomp_types.h turns those names into fields of one
 * per-thread struct, found inline: TPIDRURO, plus g_recomp_ctx_off to two of
 * the thread's own TLS words ([0] the struct, [1] the thread pointer it
 * belongs to). recomp_ctx is declared const, so a function looks it up once.
 *
 * The host code still uses the real thread-locals: vita_emutls.c places
 * those variables' storage inside the same struct (recomp_ctx_slow), so both
 * see one register file. */
#ifndef RECOMP_FASTREGS_H
#define RECOMP_FASTREGS_H
#include <stdint.h>

typedef struct RecompRegs {
    /* (r_: the generated code has macros named eax, xmm0 ...) */
    uint32_t r_eax, r_ecx, r_edx, r_esp, r_ebx, r_esi, r_edi, r_ebp, r_seh_ebp, r_fs_base;
    int32_t  r_df, r_fp_top, r_fp_cmp;
    uint16_t r_fp_control_word, r_fp_cc;
    uint32_t r_pad;
    double   r_fp_stack[8];
    uint64_t r_xmm[8][2];
    uint64_t r_mm[8];
    void *volatile r_prof_top;   /* (v154) the profiler's frame chain (postproc_prof.py) */
} RecompRegs;

extern uint32_t g_recomp_ctx_off;
RecompRegs *recomp_ctx_slow(void);

static inline __attribute__((const, always_inline)) RecompRegs *recomp_ctx(void)
{
    uint32_t tp;
    void **s;
    __asm__("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
    s = (void **)(tp + g_recomp_ctx_off);
    if (__builtin_expect(s[1] == (void *)tp, 1)) return (RecompRegs *)s[0];
    return recomp_ctx_slow();
}
#endif
