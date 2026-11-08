#!/usr/bin/env python3
"""Rewrite flat guest-address sums in runtime C to XR_HOST / XR_PTR.

    python3 tools/compact_rewrite.py FILE...      (in place; prints a summary)

Forms handled (OFF = g_xbox_mem_offset | g_memory_offset |
xbox_GetMemoryOffset()):
  ((uintptr_t)EXPR + OFF)               -> (XR_HOST(EXPR))
  (OFF + (uint32_t)(a))                 -> (XR_HOST(a))
  NAME = (T *)OFF;  ... NAME + EXPR     -> XR_PTR(EXPR)   (in that function)
                        NAME[EXPR]      -> XR_PTR(EXPR)[0]
  NAME = (T *)OFF + BASE; ... NAME + E  -> XR_PTR(BASE + E)
The first two keep their meaning in the flat model (XR_HOST is the same sum
there). The pointer form is only rewritten inside the function that sets
NAME from the offset, and only when NAME is not reassigned there. Anything
else is listed as LEFT for a hand fix.
"""
import re
import sys

OFF = r'(?:g_xbox_mem_offset|g_memory_offset|xbox_GetMemoryOffset\s*\(\s*\))'
OPERAND = r'(\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)|[A-Za-z_][\w\.\[\]>-]*)'
SUM = re.compile(r'\(\(uintptr_t\)\s*' + OPERAND + r'\s*\+\s*\(?(?:uintptr_t\)?\s*)?' + OFF + r'\)')
SUM2 = re.compile(r'\(\s*\(?(?:char|uint8_t)?\s*\*?\)?\s*' + OFF + r'\s*\+\s*\(uint32_t\)' + OPERAND + r'\)')
DECL = re.compile(r'^(\s*)(?:static\s+)?(?:const\s+)?(?:volatile\s+)?(?:unsigned\s+char|uint8_t|char)\s*'
                  r'(?:const\s+)?\*\s*(\w+)\s*=\s*\((?:const\s+)?(?:volatile\s+)?(?:unsigned\s+char|uint8_t|char)\s*\*\)'
                  r'\s*(?:\(uintptr_t\)\s*)?' + OFF + r'\s*(?:\+\s*([^;]+))?;')


def take_expr(s, i):
    """From s[i], the expression up to a depth-0 ',', ')', ';', ']' or '?'/':'."""
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if c in '([':
            depth += 1
        elif c in ')]':
            if depth == 0:
                break
            depth -= 1
        elif c in ',;?:' and depth == 0:
            break
        elif c == '=' and depth == 0 and s[j + 1:j + 2] != '=' and s[j - 1:j] not in '=!<>':
            break
        j += 1
    return s[i:j].rstrip(), j


def func_end(lines, k):
    """Index of the line closing the function that contains line k ('}' at column 0)."""
    for j in range(k, len(lines)):
        if lines[j].startswith('}'):
            return j
    return len(lines) - 1


def rewrite_pointer_uses(body, name, base):
    out, n, i = [], 0, 0
    pat = re.compile(r'(?<![\w.>])' + re.escape(name) + r'\s*(\+|\[)')
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            break
        out.append(body[i:m.start()])
        expr, end = take_expr(body, m.end())
        if base:
            expr = '(' + base + ') + ' + expr
        if m.group(1) == '[':
            out.append('XR_PTR(' + expr.strip() + ')[0]')
            end += 1   # the closing ']'
        else:
            out.append('XR_PTR(' + expr.strip() + ')')
        n += 1
        i = end
    return ''.join(out), n


def process(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    src, a = SUM.subn(lambda m: '(XR_HOST(' + m.group(1) + '))', src)
    src, b = SUM2.subn(lambda m: '(XR_HOST(' + m.group(1) + '))', src)
    lines = src.split('\n')
    c = 0
    k = 0
    while k < len(lines):
        m = DECL.match(lines[k])
        if m:
            name, base = m.group(2), (m.group(3) or '').strip()
            end = func_end(lines, k + 1)
            body = '\n'.join(lines[k + 1:end + 1])
            if re.search(r'(?<![\w.>])' + re.escape(name) + r'\s*(=[^=]|\+=|-=|\+\+|--)', body):
                print('  LEFT  %s:%d  %s reassigned in its function' % (path, k + 1, name))
            else:
                new, n = rewrite_pointer_uses(body, name, base)
                if n:
                    lines[k + 1:end + 1] = new.split('\n')
                    lines[k] = m.group(1) + '/* (compact guest: %s uses go through XR_PTR) */' % name
                    c += n
        k += 1
    src = '\n'.join(lines)
    open(path, 'w', encoding='utf-8').write(src)
    left = [l for l in src.split('\n') if re.search(OFF, l) and not re.search(r'^\s*(\*|/\*|//|#define XR_)', l)]
    print('%s: %d sums, %d macro sums, %d pointer uses rewritten; %d lines still name the offset'
          % (path, a, b, c, len(left)))
    for l in left:
        print('    ' + l.strip()[:120])


if __name__ == '__main__':
    for p in sys.argv[1:]:
        process(p)
