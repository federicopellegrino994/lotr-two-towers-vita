/* (v149) 0x000F79C0, the scene's visibility test, written out natively.
 *
 * thiscall: ecx the view, [esp+4] the box's low corner, [esp+8] its high
 * corner, [esp+0xC] where its outcode goes; eax = 0 out, 1 partly in,
 * 2 wholly in; ret 12. With 0x000F7710 (a box corner chosen by a 3-bit
 * selector) it was 3-4% of the game thread in a crowd, all of it the lifted
 * code's x87 and SSE bookkeeping. The same arithmetic as the lifted form:
 *   1. the box against the view's own box (+0x90 low, +0xA0 high), the
 *      outcode's bits 8 20 2 1 4 10; any set: out;
 *   2. six planes (+0x140, 0x1C each: normal, +0x10 distance, +0x14 / +0x18
 *      the selectors of the corner farthest and nearest): the far corner in
 *      front (> 0): out; the near corner not behind: 0x40 into the outcode;
 *      the outcode shifted right once a plane (arithmetic); all six behind:
 *      outcode 0, wholly in;
 *   3. three projections (+0x260, 0x7C each: a count, its selectors 24 bytes
 *      before, its lines (a, b, c) from 0x78 before, axes from 0x184A6C):
 *      a line with the corner in front: out; else partly in.
 * The plane products are single precision summed (x*x + y*y) + z*z like
 * the SSE form; the distance and the line sums in double like the x87.
 * Returns 0 when it leaves the box to the lifted function (a selector out of
 * range -- the lifted one keeps a stale corner there). */
static inline void f79_corner(uint32_t sel, uint32_t A, uint32_t B, float *p)
{
    p[0] = MEMF(((sel & 1u) ? B : A) + 0);
    p[1] = MEMF(((sel & 2u) ? B : A) + 4);
    p[2] = MEMF(((sel & 4u) ? B : A) + 8);
}
static inline float f79_dot(uint32_t n, const float *p)
{
    float a = MEMF(n) * p[0], b = MEMF(n + 4) * p[1], c = MEMF(n + 8) * p[2];
    return (float)((float)(a + b) + c);
}
static int tt_f79_native(void)
{
    const uint32_t v = ecx, A = MEM32(esp + 4), B = MEM32(esp + 8), F = MEM32(esp + 12);
    uint32_t i, k, f = 0, inside = 0;
    float p[3];
    if (v < 0x10000u || A < 0x10000u || B < 0x10000u || F < 0x10000u) return 0;
    for (i = 0; i < 6; i++) {
        uint32_t pl = v + 0x140u + 0x1Cu * i;
        if (MEM32(pl + 0x14) > 7u || MEM32(pl + 0x18) > 7u) return 0;
    }
    /* 1 */
    if (MEMF(v + 0xA0) < MEMF(A))     f |= 8u;
    if (MEMF(v + 0x90) > MEMF(B))     f |= 0x20u;
    if (MEMF(v + 0xA4) < MEMF(A + 4)) f |= 2u;
    if (MEMF(B + 4) < MEMF(v + 0x94)) f |= 1u;
    if (MEMF(v + 0xA8) < MEMF(A + 8)) f |= 4u;
    if (MEMF(B + 8) < MEMF(v + 0x98)) f |= 0x10u;
    MEM32(F) = f;
    if (f) { eax = 0; esp += 16; return 1; }
    /* 2 */
    for (i = 0; i < 6; i++) {
        uint32_t pl = v + 0x140u + 0x1Cu * i;
        double d;
        f79_corner(MEM32(pl + 0x14), A, B, p);
        d = (double)f79_dot(pl, p) + (double)MEMF(pl + 0x10);
        if (d > 0.0) { eax = 0; esp += 16; return 1; }
        f79_corner(MEM32(pl + 0x18), A, B, p);
        d = (double)f79_dot(pl, p) + (double)MEMF(pl + 0x10);
        if (d < 0.0) inside++;
        else MEM32(F) = MEM32(F) | 0x40u;
        MEM32(F) = (uint32_t)((int32_t)MEM32(F) >> 1);
    }
    if (inside == 6) { MEM32(F) = 0; eax = 2; esp += 16; return 1; }
    /* 3 */
    for (k = 0; k < 3; k++) {
        uint32_t base = v + 0x260u + 0x7Cu * k, ax = MEM32(0x184A6Cu + 8u * k), bx = MEM32(0x184A70u + 8u * k);
        int32_t n = (int32_t)MEM32(base), j;
        if (n <= 0) continue;
        if (ax > 3u) ax = 0;   /* (the lifted switch: anything above 3 is x) */
        if (bx > 3u) return 0;
        for (j = 0; j < n; j++) {
            uint32_t sel = MEM32(base - 24u + 4u * (uint32_t)j), ln = base - 0x70u + 0x10u * (uint32_t)j;
            float pw[4];
            double d;
            if (sel > 7u) return 0;
            f79_corner(sel, A, B, pw);
            pw[3] = (sel == 0u) ? MEMF(A + 12) : (sel == 7u) ? MEMF(B + 12) : 1.0f;   /* (the corner's w: copied whole for 0 and 7, else 1) */
            d = ((double)MEMF(ln - 4) * (double)pw[bx] + (double)MEMF(ln - 8) * (double)pw[ax]) + (double)MEMF(ln);
            if (d > 0.0) { eax = 0; esp += 16; return 1; }
        }
    }
    eax = 1; esp += 16;
    return 1;
}
