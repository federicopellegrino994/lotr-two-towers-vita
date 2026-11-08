#!/usr/bin/env python3
"""What still assumes the flat guest address space -- for RECOMP_COMPACT_GUEST.

In the default model every guest VA is `offset + va` on the host. In compact
mode (32-bit / non-x86 hosts) that sum is only right for RAM (guest
0x00000000-0x03FFFFFF); the contiguous window (0x80000000), the tiled
aperture (0xF0000000) and the NV2A/MCPX/flash apertures live elsewhere and
must be reached through xbox_HostPtr(va) (C runtime) or XBOX_PTR (generated
code, already table-driven).

    python3 tools/compact_audit.py              runtime + game sources
    python3 tools/compact_audit.py --gen DIR    also the generated code in DIR

Runtime report, per file:
  WRONG   the flat sum is used with an address that is never RAM (a literal
          0x8xxxxxxx/0xFxxxxxxx, XBOX_CONTIG_BASE, XBOX_TILED_BASE, ...):
          broken in compact mode, convert to xbox_HostPtr.
  CHECK   a flat base pointer or sum whose address comes from a variable;
          right only if that address is always RAM. Pushbuffer, texture and
          surface addresses usually are NOT (D3D allocates them contiguous).

Generated-code report:
  MMIO    constant-address MEMn() accesses into the APU (0xFE800000-
          0xFE87FFFF) still plain memory -- postproc_apu.py should have
          turned them into calls; any left means it did not run.
Exit status is the number of WRONG sites (0 when clean).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN = [os.path.join(ROOT, "xboxrecomp-0.12.0", "src"), os.path.join(ROOT, "twotowers", "src")]
SKIP_DIRS = {"third_party"}
EXTS = (".c", ".h", ".inc", ".cpp")

FLAT = re.compile(r"g_xbox_mem_offset|g_memory_offset|xbox_GetMemoryOffset\s*\(\s*\)|xbox_GetMemoryBase\s*\(\s*\)")
NON_RAM = re.compile(r"0x[89A-Fa-f][0-9A-Fa-f]{7}u?\b|XBOX_(CONTIG|TILED|NV2A|MCPX|FLASH|KERNEL)_BASE|KERNEL_VA_BASE")
# Lines that only declare or set the offset itself.
BENIGN = re.compile(r"^\s*(extern\s+)?ptrdiff_t\s+g_xbox_mem_offset|g_xbox_mem_offset\s*=\s*xbox_GetMemoryOffset|^\s*(\*|/\*|//)")
APU_LO, APU_HI = 0xFE800000, 0xFE880000
MEM_CONST = re.compile(r"\b(?:S?MEM(?:8|16|32|64|F|D))\(\s*(0x[0-9A-Fa-f]+|\d+)u?\s*\)")


def runtime_report():
    wrong = check = 0
    for base in SCAN:
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for name in sorted(files):
                if not name.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, ROOT)
                with open(path, encoding="utf-8", errors="replace") as f:
                    lines = f.readlines()
                hits = []
                for no, line in enumerate(lines, 1):
                    if not FLAT.search(line) or BENIGN.search(line):
                        continue
                    # The layout code computes the flat addresses for its own
                    # fixed-address path; compact mode never uses them.
                    if name == "xbox_memory_layout.c" and "_native =" in line:
                        continue
                    kind = "WRONG" if NON_RAM.search(line) else "CHECK"
                    hits.append((kind, no, line.strip()))
                if hits:
                    print(rel)
                    for kind, no, text in hits:
                        print("  %-5s %5d  %s" % (kind, no, text[:110]))
                        wrong += kind == "WRONG"
                        check += kind == "CHECK"
    print("\nruntime: %d WRONG, %d CHECK" % (wrong, check))
    return wrong


def gen_report(gen):
    left = 0
    for name in sorted(os.listdir(gen)):
        if not name.endswith(".c"):
            continue
        with open(os.path.join(gen, name), encoding="utf-8", errors="replace") as f:
            for no, line in enumerate(f, 1):
                for m in MEM_CONST.finditer(line):
                    v = int(m.group(1), 0)
                    if APU_LO <= v < APU_HI:
                        left += 1
                        print("  MMIO  %s:%d  %s" % (name, no, line.strip()[:100]))
    print("generated: %d constant APU accesses still plain memory" % left)


def main():
    wrong = runtime_report()
    if "--gen" in sys.argv:
        gen_report(sys.argv[sys.argv.index("--gen") + 1])
    return min(wrong, 255)


if __name__ == "__main__":
    sys.exit(main())
