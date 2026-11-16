#!/usr/bin/env python3
"""Profiling hooks in the generated code (PS Vita sampling profiler).

Every recompiled function gets, as its first statement,
    RECOMP_PROF_ENTER(0x<va>);
which (with RECOMP_PROFILE defined) stores the function's address in the
global g_prof_fn and puts the caller's back when the function's scope ends
(a cleanup variable, so every return path does it). A sampler thread reads
g_prof_fn (twotowers/src/prof_vita.c). Without RECOMP_PROFILE the macro is
nothing. Run last."""
import re, sys, glob, os
MARK = '/* postproc_prof */'
DECL = MARK + '''
#if defined(RECOMP_PROFILE) && defined(__vita__) && defined(RECOMP_FAST_REGS)
/* (v154) the chain of frames each thread is in, kept in its own register
 * struct (r_prof_top): no thread test, no shared globals. prof_vita.c walks
 * the game thread's chain: own time its first frame, inclusive time every
 * frame in it. */
typedef struct recomp_prof_s { uint32_t fn; struct recomp_prof_s *up; void *ctx; } recomp_prof_t;
static inline void recomp_prof_leave_(volatile recomp_prof_t *p) { *(void *volatile *)&((RecompRegs *)p->ctx)->r_prof_top = p->up; }
#define RECOMP_PROF_ENTER(va) RECOMP_CTX_DECL; volatile recomp_prof_t _prof __attribute__((cleanup(recomp_prof_leave_))); _prof.fn = (va); _prof.up = (recomp_prof_t *)_rctx->r_prof_top; _prof.ctx = _rctx; *(void *volatile *)&_rctx->r_prof_top = (void *)&_prof
#else
#ifndef RECOMP_CTX_DECL
#define RECOMP_CTX_DECL (void)0
#endif
#define RECOMP_PROF_ENTER(va) RECOMP_CTX_DECL
#endif
/* postproc_prof end */
'''
FDEF = re.compile(r"^((?:static )?(?:void|uint32_t) sub_([0-9A-F]{8})\w*\([^)]*\)\r?\n\{\r?\n)", re.M)
ONELINE = re.compile(r"^((?:static )?void sub_[0-9A-F]{8}\w*\(void\) \{ )(?!RECOMP_)", re.M)
OTHER = re.compile(r"^((?:static )?(?:inline )?[A-Za-z_][\w ]*?\*?\s*\b(?!sub_)\w+\s*\([^;{)]*\)\r?\n\{(\r?\n))(?!    RECOMP_)", re.M)
total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    s = open(f, newline='').read()
    if MARK in s:
        a = s.find(MARK)
        b = s.find('/* postproc_prof end */', a)
        if b < 0:   # (an older block: up to the #endif that closes it, after its last macro)
            b = s.find('#define RECOMP_PROF_ENTER(va) RECOMP_CTX_DECL\n', a)
            b = s.find('#endif', b if b >= 0 else a)
        b = s.find('\n', b) + 1
        if s[a:b] != DECL:
            s = s[:a] + DECL + s[b:]
        s, n2 = OTHER.subn(lambda m: m.group(1) + '    RECOMP_CTX_DECL;' + m.group(2), s)
        s, n3 = ONELINE.subn(lambda m: m.group(1) + 'RECOMP_CTX_DECL; ', s)
        n2 += n3
        open(f, 'w', newline='').write(s)
        total += n2
        continue
    s, n = FDEF.subn(lambda m: m.group(1) + '    RECOMP_PROF_ENTER(0x%s);\n' % m.group(2), s)
    # every other function defined here gets the register-file lookup alone
    # (RECOMP_CTX_DECL: the macros the generated code uses need _rctx)
    s, n2 = OTHER.subn(lambda m: m.group(1) + '    RECOMP_CTX_DECL;' + m.group(2), s)
    n += n2
    s, n2 = ONELINE.subn(lambda m: m.group(1) + 'RECOMP_CTX_DECL; ', s)
    n += n2
    if not n:
        continue
    at = s.rfind('#include')
    eol = s.find('\n', at) + 1
    s = s[:eol] + DECL + s[eol:]
    open(f, 'w', newline='').write(s)
    total += n
print('profiling hooks:', total)
