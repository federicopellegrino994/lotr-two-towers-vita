#!/usr/bin/env python3
"""APU register accesses with a computed address, in DirectSound's code, as
direct calls (the compact guest layout: PS Vita).

postproc_apu.py turns the accesses whose address is a constant into
xbox_apu_r32/w32 calls; the rest reached the APU through the page-fault trap,
which the compact layout (RECOMP_COMPACT_GUEST) does not have: there the
registers are plain memory, DirectSound's writes never reached the emulated
APU, it never started ("apu 0 periods") and the game waited for its sound
for ever (the loading screen, on the Vita).

In the functions of the DSOUND library section only (DSOUND_LO..DSOUND_HI),
every MEM32 access whose address is computed and not on the stack becomes
    MEM32(a) = v;   ->  xio_w32(a, v);
    ... MEM32(a) ...  ->  ... xio_r32(a) ...
which send APU addresses (0xFE800000-0xFE87FFFF) to the APU and anything
else to memory as before. Without RECOMP_COMPACT_GUEST they are MEM32 itself
(the trap still handles the APU there). Run after postproc_regs.py.

Second pass, the AC'97 bus-master reset: DirectSoundCreate sets RR (bit 1)
in a channel's control byte (0xFEC0010B + the channel's offset), reads it
once and spins while the value it read had RR -- on hardware the reset is
long over by then. The desktop runtime's write trap keeps RR from ever
landing (xbox_memory_layout.c, ac97_arm_write_trap); here every byte store
to such an address drops RR as it is made:
    MEM8(eax + 0xFEC0010B) = v;  ->  MEM8(eax + 0xFEC0010B) = XAC97_NORR(v);
"""
import re, sys, glob, os

DSOUND_LO, DSOUND_HI = 0x00128B60, 0x00134A34   # the XBE's DSOUND section (Xbox USA retail)
MARK = '/* postproc_apu_io */'
DECL = MARK + '''
#if defined(RECOMP_COMPACT_GUEST)
extern uint32_t xbox_apu_r32(uint32_t va); extern void xbox_apu_w32(uint32_t va, uint32_t v);
static inline uint32_t xio_r32(uint32_t a) { return (a - 0xFE800000u) < 0x80000u ? xbox_apu_r32(a) : MEM32(a); }
static inline void xio_w32(uint32_t a, uint32_t v) { if ((a - 0xFE800000u) < 0x80000u) xbox_apu_w32(a, v); else MEM32(a) = v; }
#else
#define xio_r32(a) MEM32(a)
#define xio_w32(a, v) ((void)(MEM32(a) = (v)))
#endif
'''
FDEF = re.compile(r'^(?:static )?(?:void|uint32_t) sub_([0-9A-F]{8})\w*\(', re.M)
CONST = re.compile(r'^\s*(-?\d+|0x[0-9A-Fa-f]+)u?\s*$')
STACK = re.compile(r'\b(L_esp|L_ebp|esp|ebp|g_esp|g_ebp|XBOX_FS_BASE)\b')

def close_paren(s, i):
    """s[i] is just after '('; index of the matching ')'"""
    d = 1
    while i < len(s):
        c = s[i]
        if c == '(':
            d += 1
        elif c == ')':
            d -= 1
            if not d:
                return i
        i += 1
    return -1

def wanted(addr):
    return not CONST.match(addr) and not STACK.search(addr)

def rewrite(s):
    """MEM32 accesses in a piece of C; returns (text, count)"""
    out, i, n = [], 0, 0
    pat = re.compile(r'(?<![A-Za-z0-9_])MEM32\(')
    while True:
        m = pat.search(s, i)
        if not m:
            out.append(s[i:])
            return ''.join(out), n
        j = close_paren(s, m.end())
        if j < 0:
            out.append(s[i:])
            return ''.join(out), n
        addr = s[m.end():j]
        addr2, k = rewrite(addr)
        n += k
        out.append(s[i:m.start()])
        rest = s[j + 1:]
        w = re.match(r'\s*=(?!=)\s*', rest)
        if wanted(addr) and w:
            # a store: up to the statement's ';' at this depth
            p, d = j + 1 + w.end(), 0
            q = p
            while q < len(s):
                c = s[q]
                if c in '([': d += 1
                elif c in ')]': d -= 1
                elif c == ';' and d == 0: break
                q += 1
            rhs, k = rewrite(s[p:q])
            n += k + 1
            out.append('xio_w32(%s, (uint32_t)(%s))' % (addr2, rhs))
            i = q   # (the ';' stays)
        elif wanted(addr):
            out.append('xio_r32(%s)' % addr2)
            n += 1
            i = j + 1
        else:
            out.append('MEM32(%s)' % addr2)
            i = j + 1

def process(path):
    s = open(path, newline='').read()
    if MARK in s:
        return 0
    defs = [(m.start(), int(m.group(1), 16)) for m in FDEF.finditer(s)]
    pieces, last, total = [], 0, 0
    for idx, (pos, va) in enumerate(defs):
        if not (DSOUND_LO <= va < DSOUND_HI):
            continue
        end = defs[idx + 1][0] if idx + 1 < len(defs) else len(s)
        body, k = rewrite(s[pos:end])
        pieces.append(s[last:pos]); pieces.append(body)
        last = end
        total += k
    if not total:
        return 0
    pieces.append(s[last:])
    text = ''.join(pieces)
    at = text.rfind('#include')
    eol = text.find('\n', at) + 1
    text = text[:eol] + DECL + text[eol:]
    open(path, 'w', newline='').write(text)
    return total

MARK2 = '/* postproc_apu_io: ac97 */'
DECL2 = MARK2 + '''
#if defined(RECOMP_COMPACT_GUEST)
#define XAC97_NORR(v) ((uint8_t)((v) & ~2u))   /* (the reset a write asks for is done at once) */
#else
#define XAC97_NORR(v) (v)
#endif
'''
AC97_CTL = re.compile(r'^(\s*)MEM8\(([^;]*?(?:-20971253|0xFEC0010B)u?[^;]*?)\) = ([^;]+);', re.M)

def process_ac97(path):
    s = open(path, newline='').read()
    if MARK2 in s:
        return 0
    defs = [(m.start(), int(m.group(1), 16)) for m in FDEF.finditer(s)]
    pieces, last, total = [], 0, 0
    for idx, (pos, va) in enumerate(defs):
        if not (DSOUND_LO <= va < DSOUND_HI):
            continue
        end = defs[idx + 1][0] if idx + 1 < len(defs) else len(s)
        body, k = AC97_CTL.subn(lambda m: '%sMEM8(%s) = XAC97_NORR(%s);' % (m.group(1), m.group(2), m.group(3)), s[pos:end])
        pieces.append(s[last:pos]); pieces.append(body)
        last = end
        total += k
    if not total:
        return 0
    pieces.append(s[last:])
    text = ''.join(pieces)
    at = text.rfind('#include')
    eol = text.find('\n', at) + 1
    text = text[:eol] + DECL2 + text[eol:]
    open(path, 'w', newline='').write(text)
    return total

total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    k = process_ac97(f)
    if k:
        print('%s: %d AC97 channel control stores' % (os.path.basename(f), k))
    total += k
print('AC97 channel resets completed on write:', total)

total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    k = process(f)
    if k:
        print('%s: %d DirectSound accesses' % (os.path.basename(f), k))
    total += k
print('DirectSound computed-address accesses rewritten:', total)
