/*
 * Compact guest address space (RECOMP_COMPACT_GUEST) -- the properties a
 * 32-bit host port depends on, checked on whatever host builds this.
 *
 *   1. RAM wraps at its size through every slice up to the contiguous
 *      window, as the 26-bit Xbox address bus does (no mirror mappings).
 *   2. The contiguous window at 0x80000000 is its own storage, and the tiled
 *      aperture at 0xF0000000 aliases it, as the file-mapping path builds.
 *   3. NV2A, MCPX and flash apertures are backed.
 *   4. An unbacked slice is reported, not silently aliased.
 *   5. g_xbox_mem_offset + va still equals the table's answer for RAM, so
 *      runtime code doing the flat sum keeps working there.
 *   6. The generated code's MEM macros (recomp_types.h) read and write
 *      through the table, misaligned float/double/int64 included.
 *
 * Usage: compact_guest_test <xbe>   (tools/conformance/mkxbe.py makes one)
 */
#define RECOMP_GENERATED_CODE_TEST 1
#include "recomp_types.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Supplied by recompiled game code in a real build. */
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(unsigned int va) { (void)va; return 0; }
recomp_func_t recomp_lookup_manual(unsigned int va) { (void)va; return 0; }

static int failures;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static unsigned char *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
    b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

int main(int argc, char **argv)
{
    size_t len = 0;
    unsigned char *xbe;

#if !defined(RECOMP_COMPACT_GUEST)
    printf("built without RECOMP_COMPACT_GUEST; nothing to test\n");
    return 1;
#endif
    if (argc < 2 || !(xbe = load(argv[1], &len))) {
        fprintf(stderr, "usage: %s <xbe>\n", argv[0]);
        return 2;
    }
    printf("compact guest (host pointers %u-bit, unaligned-safe macros %s)\n",
           (unsigned)(sizeof(void *) * 8),
#if defined(RECOMP_UNALIGNED_GUEST)
           "on"
#else
           "off"
#endif
           );
    if (!xbox_MemoryLayoutInit(xbe, len)) {
        printf("  xbox_MemoryLayoutInit failed\n");
        return 1;
    }

    /* 1. RAM wrap */
    MEM32(0x00070000u) = 0x11223344u;
    check(MEM32(0x04070000u) == 0x11223344u, "RAM: 0x04070000 reads 0x00070000 (64 MB wrap)");
    check(MEM32(0x7C070000u) == 0x11223344u, "RAM: 0x7C070000 reads 0x00070000 (last slice)");
    MEM32(0x20070004u) = 0xCAFEF00Du;
    check(MEM32(0x00070004u) == 0xCAFEF00Du, "RAM: write through a mirror lands in RAM");

    /* 2. contiguous window and tiled alias */
    MEM32(0x00001000u + 0x100000u) = 0x5A5A5A5Au;
    MEM32(0x80101000u) = 0xA5C30F17u;
    check(MEM32(0x00101000u) == 0x5A5A5A5Au, "contig: separate from RAM (no alias onto the image)");
    check(MEM32(0xF0101000u) == 0xA5C30F17u, "tiled: 0xF0101000 aliases contig 0x80101000");
    MEM32(0x83FFFFFCu) = 0x01020304u;
    check(MEM32(0x83FFFFFCu) == 0x01020304u, "contig: last word of the 64 MB window");

    /* 3. device apertures */
    MEM32(0xFD001804u) = 0x00000001u;
    check(MEM32(0xFD001804u) == 1u, "NV2A aperture backed (0xFD001804)");
    MEM32(0xFE820010u) = 0x77u;
    check(MEM32(0xFE820010u) == 0x77u, "MCPX aperture backed (APU 0xFE820010)");
    MEM32(0xFF000040u) = 0x99u;
    check(MEM32(0xFF000040u) == 0x99u, "flash aperture backed (0xFF000040)");
    check(MEM32(0xFF100040u) == 0x99u && MEM32(0xFFF00040u) == 0x99u,
          "flash: 1 MB repeated through its 16 MB slice");
    check(xbox_HostPtr(0xFE820010u) == (void *)XBOX_PTR(0xFE820010u),
          "xbox_HostPtr agrees with the generated code's XBOX_PTR");

    /* 4. unbacked slice */
    check(xbox_HostPtr(0x90000000u) == NULL, "unbacked slice 0x90000000 reported as NULL");
    check(g_xbox_region_base[0x90] == 0, "unbacked slice faults at host 0 rather than aliasing");

    /* 5. flat sum still right for RAM */
    check((uintptr_t)g_xbox_mem_offset + 0x00070000u == (uintptr_t)xbox_HostPtr(0x00070000u),
          "RAM: g_xbox_mem_offset + va == xbox_HostPtr(va)");

    /* 6. misaligned typed access */
    MEMF(0x00200001u) = 3.25f;
    check(MEMF(0x00200001u) == 3.25f, "MEMF at an odd address round-trips");
    MEMD(0x00200013u) = -1234.5;
    check(MEMD(0x00200013u) == -1234.5, "MEMD at an odd address round-trips");
    SMEM64(0x0020002Du) = -42;
    check(SMEM64(0x0020002Du) == -42, "SMEM64 at an odd address round-trips");
    MEM16(0x00200041u) = 0xBEEF;
    check(MEM16(0x00200041u) == 0xBEEF && MEM8(0x00200041u) == 0xEF,
          "MEM16 at an odd address, little-endian bytes");
    MEMF(0x80200003u) = 0.5f;
    check(MEMF(0xF0200003u) == 0.5f, "misaligned float through the tiled alias");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
