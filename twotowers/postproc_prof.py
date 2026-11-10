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
/* (only the game's frame thread records: g_prof_tp is its TPIDRURO) */
extern volatile uint32_t g_prof_fn, g_prof_tp;
typedef struct { uint32_t prev; int on; } recomp_prof_t;
static inline uint32_t recomp_tp_(void) { uint32_t v; __asm__("mrc p15, 0, %0, c13, c0, 3" : "=r"(v)); return v; }
static inline void recomp_prof_leave_(recomp_prof_t *p) { if (p->on) g_prof_fn = p->prev; }
#define RECOMP_PROF_ENTER(va) recomp_prof_t _prof __attribute__((cleanup(recomp_prof_leave_))) = { g_prof_fn, recomp_tp_() == g_prof_tp }; if (_prof.on) g_prof_fn = (va)
#else
#define RECOMP_PROF_ENTER(va) ((void)0)
#endif
'''
FDEF = re.compile(r"^((?:static )?(?:void|uint32_t) sub_([0-9A-F]{8})\w*\([^)]*\)\r?\n\{\r?\n)", re.M)
total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    s = open(f, newline='').read()
    if MARK in s:
        a = s.find(MARK)
        b = s.find('#endif', a)
        b = s.find('\n', b) + 1
        if s[a:b] != DECL:
            s = s[:a] + DECL + s[b:]
            open(f, 'w', newline='').write(s)
            total += 1
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
