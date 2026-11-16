#!/usr/bin/env python3
"""Two cheaper forms for the lifted functions (run last but for postproc_prof.py).

1. K accessors. A guest access is `host = table[va >> 24] + va`: a shift and
   a table load on top of the access itself. The guest stack and every
   constant address below 64 MB are in guest RAM, which is one host block,
   so for those the host address is `_k0 + va` with _k0 a local of the
   function: MEM32(L_esp + 8) becomes KMEM32(L_esp + 8), PUSH32(L_esp, x)
   KPUSH32, MEM32(0x201184) KMEM32. ebp counts as the stack only in a
   function that sets it up as its frame pointer (`ebp = L_esp;` once, before
   any use, and nothing else ever written to it but the caller's value).

2. XL registers. The SSE registers were per-thread globals of a union type,
   every operation on one a load of both operands, a store of the result
   and a 16-byte copy. Inside a function they are now locals of the host's
   vector type (X_xmm0..), written back to the globals before every call and
   return and read again after a call, exactly as postproc_regs.py does with
   the integer registers.

Only functions postproc_regs.py converted are touched (L_esp is its local).
A function this pass cannot follow keeps its lifted form. Idempotent.
"""
import glob
import os
import re
import sys

FN_RE = re.compile(r'^void (sub_\w+)\(void\)\n\{\n(.*?)^\}\n', re.M | re.S)
REGS_MARK = '/* registers in locals (postproc_regs.py) */'
MARK = '/* K accessors, XL registers (postproc_fast.py) */'
IDENT_CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
RAM_TOP = 0x03FFFF00

ACC1 = ('MEM8', 'MEM16', 'MEM32', 'SMEM8', 'SMEM16', 'SMEM32', 'SMEM64', 'MEMF', 'MEMD')
XMM_ADDR1 = ('XMM_MEM', 'XMM_STORE', 'XMM_STORE_LOW', 'XMM_STORE_HIGH')
XMM_ADDR2 = ('XMM_LOAD_LOW', 'XMM_LOAD_HIGH')
XMM_OK = set('''XMM_SHUFFLE XMM_ADD XMM_MUL XMM_SUB XMM_SCALAR XMM_ZERO XMM_UNPACK_LOW
XMM_UNPACK_HIGH XMM_MOVE_HIGH_TO_LOW XMM_MOVE_LOW_TO_HIGH XMM_XOR XMM_RCP XMM_MIN
XMM_MAX'''.split()) | set(XMM_ADDR1) | set(XMM_ADDR2)

PURE = set('''
MEM8 MEM16 MEM32 SMEM8 SMEM16 SMEM32 SMEM64 MEMF MEMD PUSH32 POP32 LO8 HI8 LO16
SET_LO8 SET_HI8 SET_LO16 RECOMP_FCMP RECOMP_FCMP_CC RECOMP_PARITY8 ZX8 ZX16
SX8 SX16 XBOX_PTR RECOMP_DF_STEP memcpy memset memmove sqrt sqrtf sin cos tan
log2 exp2 ldexp fabs atan2 fmod floor ceil recomp_fist recomp_ld80 recomp_st80
recomp_frndint recomp_cvtss2si recomp_cvttss2si recomp_fxam ROL8 ROL16 ROL32
ROR8 ROR16 ROR32 RC_ROT RC_ROT8 RC_ROT16 RC_ROT32 xbox_apu_w32 xbox_apu_r32
fp_push fp_pop fp_top fp_st fp_st1 xbox_ReadTimeStampCounter
recomp_eflags_read recomp_eflags_write recomp_cpuid RECOMP_ATOMIC_ADD32
RECOMP_ATOMIC_CAS32 BSWAP32 if while for switch sizeof return
xio_r32 xio_w32 xio_r16 xio_w16 xio_r8 xio_w8 RECOMP_PROF_ENTER
KMEM8 KMEM16 KMEM32 KSMEM8 KSMEM16 KSMEM32 KSMEM64 KMEMF KMEMD KPUSH32 KPOP32
'''.split())
PURE_PREFIX = ('CMP_', 'TEST_', 'XMM_', 'MMX_', 'SAR', 'SHL', 'SHR', 'XL_', 'KXL_', '__builtin_')


class Unsupported(Exception):
    pass


def mask_comments(s):
    out = list(s)
    i, n = 0, len(s)
    while i < n:
        if s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif s.startswith('//', i):
            j = s.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def is_pure(name):
    return name in PURE or name.startswith(PURE_PREFIX)


def call_args(m, open_end):
    """The top-level arguments of the call whose '(' ends at open_end:
    [(start, end)], and the index past the ')'."""
    j, depth, a0, args = open_end, 1, open_end, []
    while depth:
        if j >= len(m):
            raise Unsupported('unbalanced')
        c = m[j]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                args.append((a0, j))
        elif c == ',' and depth == 1:
            args.append((a0, j))
            a0 = j + 1
        j += 1
    return args, j


LIT_RE = re.compile(r'^(0x[0-9A-Fa-f]+|\d+)[uU]?$')


def is_ram(arg, framed):
    a = arg.strip()
    if re.match(r'^L_esp\b(\s*[+-].*)?$', a, re.S):
        return True
    if framed and re.match(r'^ebp\b(\s*[+-].*)?$', a, re.S):
        return True
    mm = LIT_RE.match(a)
    if mm:
        v = int(mm.group(1), 0)
        return 0x10000 <= v < RAM_TOP
    return False


def ebp_is_frame(m):
    """ebp is this function's frame pointer and nothing else."""
    sets = [x for x in re.finditer(r'\bebp\s*=(?!=)\s*([^;]*);', m)]
    n_frame, first_frame = 0, None
    for x in sets:
        v = x.group(1).strip()
        if v == 'L_esp':
            n_frame += 1
            if first_frame is None:
                first_frame = x.start()
        elif v not in ('0', 'g_ebp', 'g_seh_ebp'):
            return False
    if n_frame != 1:
        return False
    if re.search(r'&\s*ebp\b|\bebp\s*(\+\+|--|[-+*/|&^]=|<<=|>>=)|(\+\+|--)\s*ebp\b', m):
        return False
    # written by a macro other than the pop of the caller's value?
    for x in re.finditer(r'\b(\w+)\s*\(\s*([^;]*?)\bebp\b\s*\)\s*;', m):
        pass
    for x in re.finditer(r'\bSET_\w+\s*\(\s*ebp\b', m):
        return False
    for x in re.finditer(r'\bPOP32\s*\(\s*(\w+)\s*,\s*ebp\s*\)', m):
        if x.group(1) != 'L_esp':
            return False
    # any use as an address before the frame is set up?
    use = re.search(r'\(\s*ebp\b', m)
    if use and use.start() < first_frame:
        return False
    return True


def convert(body):
    if REGS_MARK not in body or MARK in body:
        return None, 0, 0
    m = mask_comments(body)
    if re.search(r'\bsetjmp\w*\b|\brecomp_setjmp_slot\b', m):
        raise Unsupported('setjmp')
    framed = ebp_is_frame(m)
    edits = []
    nk = 0

    # ---- XL: can this function's SSE registers be locals?
    used = sorted(set(re.findall(r'\bxmm([0-7])\b', m)))
    xl = bool(used)
    if xl:
        if re.search(r'\bxmm[0-7]\s*\.\s*(?!f\s*\[)', m) or re.search(r'&\s*xmm[0-7]\b', m):
            xl = False
        for mm in re.finditer(r'\b(XMM_\w+)\s*\(', m):
            if mm.group(1) not in XMM_OK:
                xl = False
    if xl:
        # every call must be a statement of its own
        for mm in IDENT_CALL.finditer(m):
            name = mm.group(1)
            if is_pure(name):
                continue
            k = mm.start() - 1
            while k >= 0 and m[k] in ' \t':
                k -= 1
            prev = m[k] if k >= 0 else '\n'
            if prev not in ';{}\n:':
                if prev == ')' and re.search(r'\b(if|else)\b', m[max(0, k - 400):k]):
                    pass
                elif prev == 'e' and m[max(0, k - 3):k + 1] == 'else':
                    pass
                else:
                    xl = False
                    break
            args, j = call_args(m, mm.end())
            while j < len(m) and m[j] in ' \t':
                j += 1
            if j >= len(m) or m[j] != ';':
                xl = False
                break
    out_code = in_code = ''
    if xl:
        out_code = ' '.join('XL_OUT(g_xmm%s, X_xmm%s);' % (r, r) for r in used)
        in_code = ' '.join('X_xmm%s = XL_IN(g_xmm%s);' % (r, r) for r in used)
        for mm in re.finditer(r'\bxmm([0-7])\b(\s*\.\s*f)?', m):
            edits.append((mm.start(), mm.end(), 'X_xmm' + mm.group(1)))
        for mm in IDENT_CALL.finditer(m):
            name = mm.group(1)
            if is_pure(name):
                continue
            args, j = call_args(m, mm.end())
            while j < len(m) and m[j] in ' \t':
                j += 1
            edits.append((mm.start(), mm.start(), '{ ' + out_code + ' '))
            edits.append((j + 1, j + 1, ' ' + in_code + ' }'))
        for mm in re.finditer(r'\breturn\s*;', m):
            edits.append((mm.start(), mm.start(), '{ ' + out_code + ' '))
            edits.append((mm.end(), mm.end(), ' }'))

    # ---- accessors (and the XMM macros' new names)
    for mm in IDENT_CALL.finditer(m):
        name = mm.group(1)
        if name in ACC1:
            args, j = call_args(m, mm.end())
            if len(args) == 1 and is_ram(m[args[0][0]:args[0][1]], framed):
                edits.append((mm.start(1), mm.end(1), 'K' + name))
                nk += 1
        elif name in ('PUSH32', 'POP32'):
            args, j = call_args(m, mm.end())
            if len(args) == 2 and m[args[0][0]:args[0][1]].strip() == 'L_esp':
                edits.append((mm.start(1), mm.end(1), 'K' + name))
                nk += 1
        elif name.startswith('XMM_') and xl:
            new = 'XL_' + name[4:]
            if name == 'XMM_SHUFFLE':
                # shufps with 0x00/0x55/0xAA/0xFF: one lane of each operand, twice
                args, j = call_args(m, mm.end())
                if len(args) == 3 and re.match(r'^(0|0x0+|0x55|0xAA|0xFF)$', m[args[2][0]:args[2][1]].strip(), re.I):
                    new = 'XL_SHUFFLE_DUP'
            if name in XMM_ADDR1 or name in XMM_ADDR2:
                args, j = call_args(m, mm.end())
                a = args[0] if name in XMM_ADDR1 else args[1]
                if is_ram(m[a[0]:a[1]], framed):
                    new = 'K' + new
                    nk += 1
            edits.append((mm.start(1), mm.end(1), new))

    if not edits:
        return None, 0, 0
    edits.sort(key=lambda e: (e[0], e[1]))
    res, at = [], 0
    for s0, e0, rep in edits:
        if s0 < at:
            raise Unsupported('overlapping edits')
        res.append(body[at:s0])
        res.append(rep)
        at = e0
    res.append(body[at:])
    text = ''.join(res)
    decl = '    const uintptr_t _k0 = RECOMP_K0; (void)_k0; %s\n' % MARK
    if xl:
        decl += '    rxl_t %s;\n' % ', '.join('X_xmm%s = XL_IN(g_xmm%s)' % (r, r) for r in used)
        text += '    ' + out_code + '\n'
    k = text.index(REGS_MARK)
    k = text.index('\n', k) + 1
    return text[:k] + decl + text[k:], nk, 1 if xl else 0


def process(path, stats):
    text = open(path).read()

    def one(mo):
        name, body = mo.group(1), mo.group(2)
        try:
            nb, nk, nx = convert(body)
        except Unsupported:
            stats['left'] += 1
            return mo.group(0)
        if nb is None:
            return mo.group(0)
        stats['done'] += 1
        stats['k'] += nk
        stats['xl'] += nx
        return 'void %s(void)\n{\n%s}\n' % (name, nb)

    text2 = FN_RE.sub(one, text)
    if text2 != text:
        try:
            os.chmod(path, 0o644)
        except OSError:
            pass
        open(path, 'w').write(text2)


def main():
    stats = {'done': 0, 'left': 0, 'k': 0, 'xl': 0}
    for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
        if os.path.basename(f) in ('recomp_dispatch.c',):
            continue
        process(f, stats)
    print('fast forms: %d functions (%d K accesses, %d with XL registers), %d left as lifted'
          % (stats['done'], stats['k'], stats['xl'], stats['left']))


if __name__ == '__main__':
    main()
