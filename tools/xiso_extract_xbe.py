#!/usr/bin/env python3
"""Copy one file (default: default.xbe) out of an Xbox disc image, nothing else.

    python3 tools/xiso_extract_xbe.py <image.iso> <out file> [name]

Handles both a full Redump image (video partition + game partition) and a
trimmed XISO: it looks for the XDVDFS volume descriptor
("MICROSOFT*XBOX*MEDIA" at sector 32 of the game partition) at the known
partition offsets, then walks the root directory's binary tree.
"""
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game-partition offsets: XISO, XGD1 (Redump), XGD2, XGD3.
PARTITIONS = (0, 0x18300000, 0xFD90000, 0x2080000)


def find_partition(f):
    for base in PARTITIONS:
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    sys.exit("No XDVDFS volume found: is this an Xbox disc image?")


def find_entry(f, base, dir_sector, dir_size, name):
    f.seek(base + dir_sector * SECTOR)
    table = f.read(dir_size)
    want = name.lower()
    off = 0
    seen = set()
    while off not in seen and off + 14 <= len(table):
        seen.add(off)
        left, right, sector, size, attrs, nlen = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF:  # padding at a sector boundary
            break
        ename = table[off + 14:off + 14 + nlen].decode("latin-1")
        if ename.lower() == want:
            return sector, size, attrs
        # Nodes are sorted case-insensitively; pick the subtree.
        nxt = left if want < ename.lower() else right
        if nxt == 0:
            break
        off = nxt * 4
    return None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    iso, out = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else "default.xbe"
    with open(iso, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root_sector, root_size = struct.unpack("<II", f.read(8))
        hit = find_entry(f, base, root_sector, root_size, name)
        if not hit:
            sys.exit("%s not found in the image's root directory" % name)
        sector, size, _ = hit
        f.seek(base + sector * SECTOR)
        data = f.read(size)
    with open(out, "wb") as o:
        o.write(data)
    print("partition at 0x%X, %s: %d bytes -> %s" % (base, name, len(data), out))


if __name__ == "__main__":
    main()
