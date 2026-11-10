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
#if defined(RECOMP_PROFILE)
extern volatile uint32_t g_prof_fn;
static inline void recomp_prof_leave_(uint32_t *p) { g_prof_fn = *p; }
#define RECOMP_PROF_ENTER(va) uint32_t _prof_prev __attribute__((cleanup(recomp_prof_leave_))) = g_prof_fn; g_prof_fn = (va)
#else
#define RECOMP_PROF_ENTER(va) ((void)0)
#endif
'''
FDEF = re.compile(r"^((?:static )?(?:void|uint32_t) sub_([0-9A-F]{8})\w*\([^)]*\)\r?\n\{\r?\n)", re.M)
total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    s = open(f, newline='').read()
    if MARK in s:
        continue
    s, n = FDEF.subn(lambda m: m.group(1) + '    RECOMP_PROF_ENTER(0x%s);\n' % m.group(2), s)
    if not n:
        continue
    at = s.rfind('#include')
    eol = s.find('\n', at) + 1
    s = s[:eol] + DECL + s[eol:]
    open(f, 'w', newline='').write(s)
    total += n
print('profiling hooks:', total)
