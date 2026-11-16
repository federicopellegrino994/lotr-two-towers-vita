/* native_hot.c -- (v107, PS Vita speed) the game's hottest functions written
 * by hand instead of lifted from the Xbox code.
 *
 * A profile of a battle put the same few functions on top: the test of a
 * line against a triangle (0x3FD30, most calls end at one of its first three
 * checks) and the six loops that build a character's shadow volume every
 * frame (0x2FA10 the shadow mesh's vertices through their bones, 0x2FAC0
 * which faces look at the light, 0x2FCA0 the outline's edges, 0x2FD30 the
 * vertices pushed away from the light, 0x2FE40 the two caps, 0x2FED0 the
 * side quads). Lifted, every step of those loops went through the imitated
 * x86 stack and registers; here they are plain loops on host pointers.
 *
 * What they leave in guest memory is the same, byte for byte, and so are
 * esp, eax, ecx, edx and the callee-saved registers; twotowers/tools/dt
 * compares each against the lifted function on random and on built inputs
 * (the scratch below the stack pointer and the SSE scratch registers are
 * not kept: nothing reads them after a call).
 *
 * Compiled like the generated code (RECOMP_FAST_REGS on the Vita): the
 * guest registers are fields of the per-thread struct, found once per
 * function. twotowers_manual.c holds the sub_XXXXXXXX entry points (the
 * recompiler's override scan reads that file) and calls in here.
 * TT_NATIVE_HOT=0: the lifted functions. */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifndef RECOMP_CTX_DECL
#define RECOMP_CTX_DECL (void)0
#endif

extern void sub_0003FD30_gen(void);
extern void sub_0002FA10_gen(void);
extern void sub_0002FAC0_gen(void);
extern void sub_0002FCA0_gen(void);
extern void sub_0002FD30_gen(void);
extern void sub_0002FE40_gen(void);
extern void sub_0002FED0_gen(void);

/* calls of each, for [PERFG]: 0 line tests, 1 shadow faces, 2 shadow edges,
 * 3 bone sets (unused here), 4 line tests handed to the lifted code */
#if defined(TT_NATIVE_TEST)
unsigned g_perf_native_n[8];
#else
extern volatile unsigned g_perf_native_n[8];
#endif

static int nat_mode = -1;
static inline int nat_on(void)
{
    if (__builtin_expect(nat_mode < 0, 0)) {
        const char *e = getenv("TT_NATIVE_HOT");
        nat_mode = !(e && e[0] == '0');
    }
    return nat_mode;
}
/* (v110) the line-triangle test apart: with TT_NATIVE_HOT=0 the v109 test
 * found the sword's blows landing better -- it stays the lifted one unless
 * TT_NATIVE_RAY=1 (the shadow stages, which only draw, stay written by hand) */
static int nat_ray_mode = -1;
static inline int nat_ray_on(void)
{
    if (__builtin_expect(nat_ray_mode < 0, 0)) {
        const char *e = getenv("TT_NATIVE_RAY");
        nat_ray_mode = nat_on() && !(e && e[0] == '0');   /* (v161: on, the user's call -- the misses were the frame rate's; host-checked against the lifted code with the x87 state) */
    }
    return nat_ray_mode;
}

/* host memory, any alignment (the Vita's LDR/STR take one) */
typedef struct __attribute__((packed, may_alias)) { uint32_t v; } nh_u32;
typedef struct __attribute__((packed, may_alias)) { uint16_t v; } nh_u16;
typedef struct __attribute__((packed, may_alias)) { float v; } nh_f32;
#define RD32(p)     (((const nh_u32 *)(p))->v)
#define WR32(p, x)  (((nh_u32 *)(p))->v = (x))
#define RD16(p)     (((const nh_u16 *)(p))->v)
#define WR16(p, x)  (((nh_u16 *)(p))->v = (x))
#define RDF(p)      (((const nh_f32 *)(p))->v)
#define WRF(p, x)   (((nh_f32 *)(p))->v = (x))
static inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* the shadow builder's work areas (guest RAM, fixed) */
#define SH_POS    0x002A5658u   /* the mesh's vertices, in the world: 16 bytes each */
#define SH_EXT    0x002A1650u   /* the same pushed away from the light */
#define SH_VIS    0x00210A28u   /* one byte a face: 1 when it looks away from the light */
#define SH_EDGES  0x00203218u   /* the outline: two vertex indices an edge */
#define SH_NEDGE  0x00210A20u   /* (indices in it) */
#define SH_CAPA   0x00207A18u   /* faces with SH_VIS 0, three indices each */
#define SH_CAPB   0x0020C218u   /* faces with SH_VIS 1 */
#define SH_NCAPA  0x00210A18u
#define SH_NCAPB  0x00210A1Cu
#define SH_QUADS  0x00211640u   /* the sides: four vertices an edge */
#define SH_NQUAD  0x00203210u
#define SH_LIGHT  0x0021162Cu   /* the light's place (4 floats) */
#define K_ZERO    0x0018AC58u   /*  0.0  */
#define K_MINUS1  0x00170334u   /* -1.0  */
#define K_TMIN    0x00170B48u   /* -0.001 */
#define K_TMAX    0x00170B44u   /*  1.001 */

static inline int nh_overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return (uint32_t)(a - b) < bn || (uint32_t)(b - a) < an;
}

/* ------------------------------------------------------------------------
 * 0x0003FD30 -- a line (from P along D, 0..1) against a triangle.
 * thiscall: ecx the triangle (three corners of four floats; +0x3C a scale),
 * [esp+4] P, [esp+8] D, [esp+0xC] where the hit's place on the line goes,
 * [esp+0x10] where the triangle's normal goes; al = 1 on a hit; ret 16.
 *
 * The lifted function first makes the normal (written out even on a miss),
 * then turns away a triangle that does not face the line, then one the line
 * ends before or starts behind. Those three ways out are here; a line that
 * passes them goes to the lifted function from the start (nothing has been
 * written yet).
 * ------------------------------------------------------------------------ */
/* (v190) 0x1D840's product (out = b x a as it writes it: y*z' order kept, in
 * double, each rounded to float) and the dot of three floats the SSE way
 * (lane 0, then lanes 2 and 3 of movss + movhps; mulps, two shufps 0x32 + addps) */
static inline void nh_cross(float *o, float a0, float a1, float a2, const float *b)
{
    o[0] = (float)((double)b[2] * (double)a1 - (double)a2 * (double)b[1]);
    o[1] = (float)((double)a2 * (double)b[0] - (double)b[2] * (double)a0);
    o[2] = (float)((double)a0 * (double)b[1] - (double)b[0] * (double)a1);
}
static inline float nh_dot3(const float *a, const float *b)
{
    rxl_t x0, x1 = XL_SCALAR(a[0]), x2 = XL_SCALAR(b[0]);
    XL_LOAD_HIGH_P(x1, a + 1);
    XL_LOAD_HIGH_P(x2, b + 1);
    x1 = XL_MUL(x1, x2);
    x0 = x1;
    x0 = XL_SHUFFLE(x0, x0, 0x32);
    x1 = XL_ADD(x1, x0);
    x0 = XL_SHUFFLE(x0, x0, 0x32);
    x1 = XL_ADD(x1, x0);
    return x1[0];
}
static int nat_ray_full_mode = -1;
static inline int nat_ray_full(void)
{
    if (__builtin_expect(nat_ray_full_mode < 0, 0)) {
        const char *e = getenv("TT_NATIVE_RAYFULL");
        nat_ray_full_mode = !(e && e[0] == '0');
    }
    return nat_ray_full_mode;
}
void tt_nat_0003FD30(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_ray_on()) { sub_0003FD30_gen(); return; }
    {
        const uint32_t sp = g_esp, T = g_ecx;
        const uint8_t *st = (const uint8_t *)(k0 + sp);
        const uint32_t aP = RD32(st + 4), aD = RD32(st + 8), aN = RD32(st + 16);
        const uint8_t *t, *pp, *dd;
        uint8_t *nn;
        float e1x, e1y, e1z, n0, n1, n2, n3, c0, c1, c2, s, N0, N1, N2, N3, dND, dNT, dNP, tf;
        double td;
        int cmp; uint16_t cc; uint32_t stw;
        g_perf_native_n[0]++;
        /* (the lifted code reads the triangle after it has begun writing the
         * normal: the same only if the normal lies apart) */
        if (nh_overlap(aN, 16, T, 0x40) || nh_overlap(aN, 16, aP, 16) || nh_overlap(aN, 16, aD, 16)
            || nh_overlap(aN, 16, sp - 0x80u, 0x80u + 0x14u)
            || nh_overlap(T, 0x40, sp - 0x80u, 0x80u) || nh_overlap(aP, 16, sp - 0x80u, 0x80u) || nh_overlap(aD, 16, sp - 0x80u, 0x80u)) {
            g_perf_native_n[4]++; sub_0003FD30_gen(); return;
        }
        t = (const uint8_t *)XBOX_PTR(T); pp = (const uint8_t *)XBOX_PTR(aP);
        dd = (const uint8_t *)XBOX_PTR(aD); nn = (uint8_t *)XBOX_PTR(aN);

        e1x = RDF(t + 0x10) - RDF(t + 0x00);
        e1y = RDF(t + 0x14) - RDF(t + 0x04);
        e1z = RDF(t + 0x18) - RDF(t + 0x08);
        n0 = RDF(t + 0x10) - RDF(t + 0x20);
        n1 = RDF(t + 0x14) - RDF(t + 0x24);
        n2 = RDF(t + 0x18) - RDF(t + 0x28);
        n3 = RDF(t + 0x1C) - RDF(t + 0x2C);
        c0 = (float)((double)e1z * (double)n1 - (double)e1y * (double)n2);
        c1 = (float)((double)e1x * (double)n2 - (double)e1z * (double)n0);
        c2 = (float)((double)e1y * (double)n0 - (double)e1x * (double)n1);
        s = RDF(t + 0x3C);
        N0 = s * c0; N1 = s * c1; N2 = s * c2; N3 = s * n3;

        dND = (N0 * RDF(dd) + N1 * RDF(dd + 4)) + N2 * RDF(dd + 8);
        cmp = RECOMP_FCMP((double)dND, (double)RDF(k0 + K_ZERO));
        if (cmp != -1) {            /* not facing the line (or not a number) */
            cc = RECOMP_FCMP_CC(cmp);
            g_edx = aD;
            goto out;
        }
        dNT = (N0 * RDF(t) + N1 * RDF(t + 4)) + N2 * RDF(t + 8);
        dNP = (N0 * RDF(pp) + N1 * RDF(pp + 4)) + N2 * RDF(pp + 8);
        td = (-(double)dNT + (double)dNP) * ((double)RDF(k0 + K_MINUS1) / (double)dND);
        tf = (float)td;
        cmp = RECOMP_FCMP(td, (double)RDF(k0 + K_TMIN));
        if (cmp == -1 || cmp == 2) {   /* behind the line's start */
            cc = RECOMP_FCMP_CC(cmp);
            g_edx = aP;
            goto out;
        }
        cmp = RECOMP_FCMP((double)tf, (double)RDF(k0 + K_TMAX));
        if (cmp == 1 || cmp == 2) {    /* past its end */
            cc = RECOMP_FCMP_CC(cmp);
            g_edx = aP;
            goto out;
        }
        if (nat_ray_full()) {   /* (v190) the rest of the test by hand too: the hit point and its three edges */
            const uint32_t F = sp - 0x64u, aT = RD32(st + 12);
            const float K40 = RDF(k0 + 0x00170B40u);
            float tt = tf, H0, H1, H2, a[4], b[4], c[4];
            if (RECOMP_FCMP((double)tt, (double)RDF(k0 + 0x0018AC38u)) == 1) tt = 1.0f;
            H0 = (float)((double)(float)((double)RDF(dd) * (double)tt) + (double)RDF(pp));
            H1 = (float)((double)RDF(dd + 4) * (double)tt + (double)RDF(pp + 4));
            H2 = (float)((double)RDF(dd + 8) * (double)tt + (double)RDF(pp + 8));
            /* edge 1: (H - V0) . (N x' (V1 - V0) * V0.w) */
            a[0] = H0 - RDF(t); a[1] = H1 - RDF(t + 4); a[2] = H2 - RDF(t + 8);
            {   const float w = RDF(t + 0xC);
                b[0] = e1x * w; b[1] = e1y * w; b[2] = e1z * w; }
            nh_cross(c, N0, N1, N2, b);
            cmp = RECOMP_FCMP((double)nh_dot3(a, c), (double)K40);
            cc = RECOMP_FCMP_CC(cmp);
            if (cmp == -1 || cmp == 2) { g_ecx = F + 0x44u; g_edx = F + 0x44u; goto miss; }
            /* edge 2: (H - V1) . (N x' (V2 - V1) * V1.w) */
            a[0] = H0 - RDF(t + 0x10); a[1] = H1 - RDF(t + 0x14); a[2] = H2 - RDF(t + 0x18);
            {   const float w = RDF(t + 0x1C);
                b[0] = (float)RDF(t + 0x20) - RDF(t + 0x10); b[0] = b[0] * w;
                b[1] = (float)(((double)RDF(t + 0x24) - (double)RDF(t + 0x14)) * (double)w);
                b[2] = (float)(((double)RDF(t + 0x28) - (double)RDF(t + 0x18)) * (double)w); }
            nh_cross(c, N0, N1, N2, b);
            {   const float d = nh_dot3(a, c);
                g_fp_stack[(g_fp_top + 7u) & 7u] = (double)d;   /* (0x1D800 leaves it on the x87 stack, the fcomp pops it; its ecx comes back as the dot's bits) */
                g_ecx = fbits(d);
                cmp = RECOMP_FCMP((double)d, (double)K40); }
            cc = RECOMP_FCMP_CC(cmp);
            g_edx = F + 0x44u;
            if (cmp == -1 || cmp == 2) goto miss;
            /* edge 3: (H - V2) . (N x' (V0 - V2) * V2.w) */
            a[0] = H0 - RDF(t + 0x20); a[1] = H1 - RDF(t + 0x24); a[2] = H2 - RDF(t + 0x28);
            {   const float w = RDF(t + 0x2C);
                b[0] = RDF(t) - RDF(t + 0x20); b[0] = b[0] * w;
                b[1] = RDF(t + 4) - RDF(t + 0x24); b[1] = b[1] * w;
                b[2] = RDF(t + 8) - RDF(t + 0x28); b[2] = b[2] * w; }
            nh_cross(c, N0, N1, N2, b);
            {   const float d = nh_dot3(a, c);
                g_fp_stack[(g_fp_top + 7u) & 7u] = (double)d;
                g_ecx = fbits(d);
                cmp = RECOMP_FCMP((double)d, (double)K40); }
            cc = RECOMP_FCMP_CC(cmp);
            if (cmp == -1 || cmp == 2) goto miss;
            WRF(nn, N0); WRF(nn + 4, N1); WRF(nn + 8, N2); WRF(nn + 12, N3);
            WRF(XBOX_PTR(aT), tt);
            stw = (((uint32_t)g_fp_top & 7u) << 11) | cc;
            g_fp_cmp = cmp; g_fp_cc = cc;
            g_eax = ((F + 0x44u) & 0xFFFF0000u) | (stw & 0xFF00u) | 1u;
            g_edx = aT;
            g_esp = sp + 20;
            g_perf_native_n[5]++;
            return;
miss:
            WRF(nn, N0); WRF(nn + 4, N1); WRF(nn + 8, N2); WRF(nn + 12, N3);
            stw = (((uint32_t)g_fp_top & 7u) << 11) | cc;
            g_fp_cmp = cmp; g_fp_cc = cc;
            g_eax = ((F + 0x44u) & 0xFFFF0000u) | (stw & 0xFF00u);
            g_esp = sp + 20;
            g_perf_native_n[5]++;
            return;
        }
        g_perf_native_n[4]++;
        sub_0003FD30_gen();
        return;
out:
        WRF(nn, N0); WRF(nn + 4, N1); WRF(nn + 8, N2); WRF(nn + 12, N3);
        stw = (((uint32_t)g_fp_top & 7u) << 11) | cc;
        g_fp_cmp = cmp; g_fp_cc = cc;
        g_eax = (aN & 0xFFFF0000u) | (stw & 0xFF00u);
        g_ecx = fbits(c1);
        g_esp = sp + 20;
    }
}

/* ------------------------------------------------------------------------
 * The shadow volume of a character (0x00030360 calls these in turn; cdecl,
 * the shadow mesh in [esp+4]). The mesh: +0 vertices, +4 where they are
 * (from its own address: x y z and a bone's number each), +8 faces, +0xC
 * where (three 16-bit vertex indices each), +0x10 edges, +0x14 where (two
 * faces and two vertices each, 16-bit).
 * ------------------------------------------------------------------------ */

/* 0x0002FA10(mesh, ?, bones): each vertex through its bone's matrix
 * (out = x*M0 + y*M1 + z*M2 + M3) into SH_POS */
void tt_nat_0002FA10(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FA10_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t S = RD32(k0 + sp + 4), bones = RD32(k0 + sp + 12);
        const uint8_t *sm = (const uint8_t *)XBOX_PTR(S);
        const uint32_t n = RD32(sm), V = RD32(sm + 4) + S + 4;
        uint32_t i;
        g_ecx = S; g_esp = sp + 4;
        if (n == 0) { g_eax = V; g_edx = 0; return; }
        {
            const uint8_t *v = (const uint8_t *)XBOX_PTR(V);
            uint8_t *o = (uint8_t *)(k0 + SH_POS);
            const rxl_t one = (rxl_t){1.0f, 1.0f, 1.0f, 1.0f};
            rxl_t r = one;
            for (i = 0; i < n; i++, v += 16, o += 16) {
                const float x = RDF(v), y = RDF(v + 4), z = RDF(v + 8);
                const uint8_t *m = (const uint8_t *)XBOX_PTR(bones + (RD32(v + 12) << 6));
                r = XL_MUL(XL_MEM_P(m), ((rxl_t){x, x, x, x}));
                r = XL_ADD(r, XL_MUL(XL_MEM_P(m + 16), ((rxl_t){y, y, y, y})));
                r = XL_ADD(r, XL_MUL(XL_MEM_P(m + 32), ((rxl_t){z, z, z, z})));
                r = XL_ADD(r, XL_MUL(XL_MEM_P(m + 48), one));
                XL_STORE_P(o, r);
            }
            g_eax = n;
            g_edx = RD32(o - 16);
        }
    }
}

/* 0x0002FAC0(mesh): SH_VIS for every face -- 1 unless its normal points
 * against the way from the light to its first corner */
void tt_nat_0002FAC0(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FAC0_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t S = RD32(k0 + sp + 4);
        const uint8_t *sm = (const uint8_t *)XBOX_PTR(S);
        const uint32_t n = RD32(sm + 8), I = RD32(sm + 12) + S + 12;
        g_perf_native_n[1] += n;
        g_esp = sp + 4;
        if (n == 0) { g_eax = I; g_ecx = 0; return; }
        {
            const uint8_t *ix = (const uint8_t *)XBOX_PTR(I);
            const uint8_t *pos = (const uint8_t *)(k0 + SH_POS);
            uint8_t *vis = (uint8_t *)(k0 + SH_VIS);
            const float lx = RDF(k0 + SH_LIGHT), ly = RDF(k0 + SH_LIGHT + 4), lz = RDF(k0 + SH_LIGHT + 8);
            const double zero = (double)RDF(k0 + K_ZERO);
            uint32_t i; int cmp = 0;
            for (i = 0; i < n; i++, ix += 6) {
                const uint8_t *a = pos + ((uint32_t)RD16(ix) << 4), *b = pos + ((uint32_t)RD16(ix + 2) << 4),
                              *c = pos + ((uint32_t)RD16(ix + 4) << 4);
                const float ax = RDF(a), ay = RDF(a + 4), az = RDF(a + 8);
                const float bx = RDF(b), by = RDF(b + 4), bz = RDF(b + 8);
                const float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
                const float e2x = RDF(c) - bx;
                const double e2y = (double)RDF(c + 4) - (double)by, e2z = (double)RDF(c + 8) - (double)bz;
                const float e2zf = (float)e2z;
                const float nx = (float)(e2z * (double)e1y - e2y * (double)e1z);
                const float ny = (float)((double)e1z * (double)e2x - (double)e2zf * (double)e1x);
                const float nz = (float)(e2y * (double)e1x - (double)e1y * (double)e2x);
                const float dx = ax - lx, dy = ay - ly, dz = az - lz;
                const float dot = (nx * dx + ny * dy) + nz * dz;
                cmp = RECOMP_FCMP((double)dot, zero);
                vis[i] = (uint8_t)!(cmp == -1 || cmp == 2);
            }
            g_fp_cmp = cmp; g_fp_cc = RECOMP_FCMP_CC(cmp);
            g_eax = n;
            g_ecx = I + 4 + 6 * n;
            g_edx = sp - 0x5Cu;
        }
    }
}

/* 0x0002FCA0(mesh): the edges between a face that looks at the light and
 * one that does not, wound by which of the two does */
void tt_nat_0002FCA0(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FCA0_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t S = RD32(k0 + sp + 4);
        const uint8_t *sm = (const uint8_t *)XBOX_PTR(S);
        const uint32_t ne = RD32(sm + 0x10), E = RD32(sm + 0x14) + S + 0x14;
        const uint8_t *vis = (const uint8_t *)(k0 + SH_VIS);
        uint8_t *list = (uint8_t *)(k0 + SH_EDGES);
        uint32_t cnt = 0, j;
        g_perf_native_n[2] += ne;
        g_esp = sp + 4;
        WR32(k0 + SH_NEDGE, 0);
        if (ne) {
            const uint8_t *e = (const uint8_t *)XBOX_PTR(E);
            for (j = 0; j < ne; j++, e += 8) {
                const uint8_t a = vis[RD16(e)], b = vis[RD16(e + 2)];
                if (a == b) continue;
                if (a == 0) { WR16(list + cnt * 2, RD16(e + 6)); WR16(list + cnt * 2 + 2, RD16(e + 4)); }
                else        { WR16(list + cnt * 2, RD16(e + 4)); WR16(list + cnt * 2 + 2, RD16(e + 6)); }
                cnt += 2;
            }
            WR32(k0 + SH_NEDGE, cnt);
        }
        g_eax = E + 8 * ne;
        g_ecx = cnt;
        g_edx = ne;
    }
}

/* 0x0002FD30(mesh): SH_EXT = each SH_POS vertex pushed along the way from
 * the light, as far as the plane z = -1 */
void tt_nat_0002FD30(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FD30_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t S = RD32(k0 + sp + 4);
        const uint32_t n = RD32((const uint8_t *)XBOX_PTR(S));
        const uint8_t *p = (const uint8_t *)(k0 + SH_POS);
        uint8_t *x = (uint8_t *)(k0 + SH_EXT);
        const float lx = RDF(k0 + SH_LIGHT), ly = RDF(k0 + SH_LIGHT + 4), lz = RDF(k0 + SH_LIGHT + 8),
                    lw = RDF(k0 + SH_LIGHT + 12), km = RDF(k0 + K_MINUS1);
        uint32_t k;
        g_esp = sp + 4;
        for (k = 0; k < n; k++, p += 16, x += 16) {
            const float ax = RDF(p), ay = RDF(p + 4), az = RDF(p + 8), aw = RDF(p + 12);
            const double dx = (double)ax - (double)lx, dy = (double)ay - (double)ly;
            const float dz = az - lz, dw = aw - lw;
            const float s = (float)(((double)km - (double)az) / (double)dz);
            const float ex = (float)(dx * (double)s), ey = (float)(dy * (double)s);
            const float ez = dz * s, ew = dw * s;
            WRF(x, ax + ex); WRF(x + 4, ey + ay); WRF(x + 8, ez + az); WRF(x + 12, ew + aw);
            if (k + 1 == n) WRF(k0 + sp + 4, s);   /* (the lifted code keeps this in its argument's slot) */
        }
        g_eax = n << 4;
        g_ecx = n;
        g_edx = S;
    }
}

/* 0x0002FE40(mesh): the faces sorted in two lists by SH_VIS (the volume's
 * near and far caps) */
void tt_nat_0002FE40(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FE40_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t S = RD32(k0 + sp + 4);
        const uint8_t *sm = (const uint8_t *)XBOX_PTR(S);
        const uint32_t n = RD32(sm + 8), I = RD32(sm + 12) + S + 12;
        const uint8_t *vis = (const uint8_t *)(k0 + SH_VIS);
        uint8_t *ca = (uint8_t *)(k0 + SH_CAPA), *cb = (uint8_t *)(k0 + SH_CAPB);
        uint32_t na = 0, nb = 0, i;
        g_esp = sp + 4;
        if (n) {
            const uint8_t *ix = (const uint8_t *)XBOX_PTR(I);
            for (i = 0; i < n; i++, ix += 6) {
                uint8_t *d;
                if (vis[i] == 0) d = ca + 6 * na++; else d = cb + 6 * nb++;
                WR32(d, RD32(ix)); WR16(d + 4, RD16(ix + 4));
            }
        }
        WR32(k0 + SH_NCAPA, na);
        WR32(k0 + SH_NCAPB, nb);
        g_eax = n;
        g_ecx = S;
        g_edx = I + 6 * n;
    }
}

/* 0x0002FED0(): a quad for every outline edge -- its two SH_POS vertices,
 * then the same two from SH_EXT the other way round */
void tt_nat_0002FED0(void)
{
    RECOMP_CTX_DECL;
    const uintptr_t k0 = RECOMP_K0;
    if (!nat_on()) { sub_0002FED0_gen(); return; }
    {
        const uint32_t sp = g_esp;
        const uint32_t ne = RD32(k0 + SH_NEDGE);
        const uint8_t *list = (const uint8_t *)(k0 + SH_EDGES);
        const uint8_t *pos = (const uint8_t *)(k0 + SH_POS), *ext = (const uint8_t *)(k0 + SH_EXT);
        uint8_t *q = (uint8_t *)(k0 + SH_QUADS);
        uint32_t b, nq = 0;
        g_esp = sp + 4;
        WR32(k0 + SH_NQUAD, 0);
        if (ne == 0) { g_eax = 0; return; }
        for (b = 0; b < ne; b += 2, q += 64, nq += 4) {
            const uint32_t v0 = (uint32_t)RD16(list + b * 2) << 4, v1 = (uint32_t)RD16(list + b * 2 + 2) << 4;
            memcpy(q, pos + v0, 16);
            memcpy(q + 16, pos + v1, 16);
            memcpy(q + 32, ext + v1, 16);
            memcpy(q + 48, ext + v0, 16);
        }
        WR32(k0 + SH_NQUAD, nq);
        g_eax = ne;
        g_ecx = SH_QUADS + 64u * (nq >> 2) - 16u;
        g_edx = SH_QUADS + 64u * (nq >> 2);
    }
}

/* (v115) the characters' shadow volumes: off on the Vita unless TT_SHADOWS=1
 * (sub_00030360 takes the way out it has for a character with no shadow) */
int tt_shadows_off(void)
{
    static int off = -1;
    if (__builtin_expect(off < 0, 0)) { const char *e = getenv("TT_SHADOWS"); off = !(e && e[0] == '1'); }
    return off;
}


/* (v209) 0x0004BC20: the rain (and whatever else it draws as lines). For each of the [0x40F834] items at
 * [0x40F848] (0x30 bytes each) two vertices -- position and colour -- go into the buffer at [0x40F8F0], and
 * the lot is drawn as a line list (0x1181A0, type 2, 16-byte vertices). The lifted loop made the two colours
 * again for every item: eight calls of the float-to-integer routine (0xFF7BC) each. In the first level of
 * Helm's Deep that was 13.8% of the game thread (v208 log: 12 ms of a 91 ms frame). Here the colours are made
 * once -- nothing in the loop writes what they are made from -- and the loop runs on host pointers; the
 * positions go through the same double-then-float steps. TT_NATIVE_RAIN=0: the lifted one. */
extern void sub_0004BC20_gen(void);
extern void sub_001181A0(void);
static inline uint32_t nh_ftol32(double x)
{
    int64_t r = (x > -9223372036854775808.0 && x < 9223372036854775808.0) ? (int64_t)x : INT64_MIN;
    return (uint32_t)(uint64_t)r;
}
void tt_nat_0004BC20(void)
{
    RECOMP_CTX_DECL;
    static int on = -1;
    const uint32_t sp = g_esp, n = RD32(XBOX_PTR(0x0040F834u));
    uint32_t items, out, i, c1, c2, base;
    if (__builtin_expect(on < 0, 0)) { const char *e = getenv("TT_NATIVE_RAIN"); on = !(e && e[0] == '0'); }
    items = RD32(XBOX_PTR(0x0040F848u)); out = RD32(XBOX_PTR(0x0040F8F0u));
    if (!on || n == 0 || n > 0x10000u
        || nh_overlap(out, n * 0x20u, 0x0040F800u, 0x100u) || nh_overlap(out, n * 0x20u, items, n * 0x30u)
        || nh_overlap(out, n * 0x20u, sp - 0x100u, 0x104u) || nh_overlap(items, n * 0x30u, sp - 0x100u, 0x104u)) {
        sub_0004BC20_gen();
        return;
    }
    {
        const double K = (double)RDF(XBOX_PTR(0x00170458u));
        const uint32_t a = nh_ftol32((double)RDF(XBOX_PTR(0x0040F890u)) * K) & 0xFFu;
        const uint32_t b1 = nh_ftol32((double)RDF(XBOX_PTR(0x0040F840u)) * K), b2 = nh_ftol32((double)RDF(XBOX_PTR(0x0040F844u)) * K);
        const uint32_t c = nh_ftol32((double)RDF(XBOX_PTR(0x0040F894u)) * K) & 0xFFu, d = nh_ftol32((double)RDF(XBOX_PTR(0x0040F898u)) * K) & 0xFFu;
        c1 = ((((a | (b1 << 8)) << 8) | c) << 8) | d;
        c2 = ((((a | (b2 << 8)) << 8) | c) << 8) | d;
    }
    {
        const uint8_t *it = (const uint8_t *)XBOX_PTR(items);
        uint8_t *o = (uint8_t *)XBOX_PTR(out);
        for (i = 0; i < n; i++, it += 0x30, o += 0x20) {
            const double s = (double)RDF(it + 0x28) * (double)RDF(it + 0x20);
            const float ax = (float)(s * (double)RDF(it + 0x10)), ay = (float)(s * (double)RDF(it + 0x14));
            const double x0 = (double)ax + (double)RDF(it), y0 = (double)ay + (double)RDF(it + 4);
            const float z0 = (float)(s * (double)RDF(it + 0x18) + (double)RDF(it + 8));
            const float x1 = (float)(x0 + (double)RDF(it + 0x10)), y1 = (float)(y0 + (double)RDF(it + 0x14));
            const float z1 = (float)((double)z0 + (double)RDF(it + 0x18));
            WRF(o, (float)x0); WRF(o + 4, (float)y0); WRF(o + 8, z0); WR32(o + 0xC, c1);
            WRF(o + 0x10, x1); WRF(o + 0x14, y1); WRF(o + 0x18, z1); WR32(o + 0x1C, c2);
        }
    }
    /* the draw, as the lifted tail makes it: (2, 2n, the buffer, 0x10), the callee takes its 16 bytes */
    base = sp - 0x34u;
    WR32(XBOX_PTR(base - 4u), g_ebx); WR32(XBOX_PTR(base - 8u), g_edi);
    WR32(XBOX_PTR(base - 12u), 0x10u); WR32(XBOX_PTR(base - 16u), out);
    WR32(XBOX_PTR(base - 20u), n * 2u); WR32(XBOX_PTR(base - 24u), 2u);
    WR32(XBOX_PTR(base - 28u), 0x0004BDCBu);
    g_eax = n; g_ecx = out; g_edx = n * 2u;
    g_ebp = g_seh_ebp = base - 24u;   /* (as the last float-to-integer call left the frame registers: its esp - 4) */
    g_fp_stack[(g_fp_top + 7u) & 7u] = (double)RDF(XBOX_PTR(0x0040F898u)) * (double)RDF(XBOX_PTR(0x00170458u));   /* (and its value in the x87 slot it was pushed to) */
    g_esp = base - 28u;
    sub_001181A0();
    g_esp = g_esp + 8u + 0x34u + 4u;
}



/* (v214) A mesh's reach: every point a line could be found to hit on it. A triangle takes a hit point H
 * (on its plane, P + tD with t in [-0.001, 1]) when (H - V_i) . (N x e_i') >= -0.005 for its three edges
 * (0x3FD30's tests): three half-planes, bounded -- a triangle a little larger than the one stored -- when
 * each pair's corner keeps the third. Their corners, over all the mesh's triangles, give a box; a line whose
 * stretch lies wholly outside it can hit nothing there, and 0x458D0 is left before the per-triangle tests.
 * The line's record is then exactly as the tests would have left it (no hit is nearer); what differs is only
 * scratch the callers never read (ecx, the x87 flags, the stack below). A mesh with a triangle the box cannot
 * hold (unbounded, degenerate, not a number) is never skipped. Static meshes: the box is checked against the
 * triangles' words every 4th use, and a mesh found changed twice is no longer skipped at all.
 * TT_MESHCULL=0: off. [MESHCULL] every 10 s. */
typedef struct { uint32_t mesh, tri, count, sum, uses, changes; int state; float lo[3], hi[3]; } NhBox;   /* state 0 empty, 1 box, 2 never */
#define NH_NBOX 2048
static NhBox g_nh_box[NH_NBOX];
unsigned g_nh_cull_n[3];   /* calls, skipped, triangles not tested */
int g_nh_cull_on = -1;
static uint32_t nh_mesh_sum(const uint8_t *t, uint32_t n)
{
    const uint32_t *w = (const uint32_t *)t; uint32_t i, a = 0x9E3779B9u, b = 0;
    for (i = 0; i < n * 20u; i++) { a += w[i]; b ^= a * 0x01000193u; }
    return a ^ b;
}
static int nh_tri_reach(const uint8_t *t, double lo[3], double hi[3])
{
    double V[3][3], w[3], e1[3], nn[3], c[3], N[3], C[3][3], d[3], s;
    int i, j, k, q;
    for (i = 0; i < 3; i++) { for (k = 0; k < 3; k++) V[i][k] = RDF(t + 0x10 * i + 4 * k); w[i] = RDF(t + 0x10 * i + 0xC); }
    s = RDF(t + 0x3C);
    for (k = 0; k < 3; k++) { e1[k] = V[1][k] - V[0][k]; nn[k] = V[1][k] - V[2][k]; }
    c[0] = e1[2] * nn[1] - e1[1] * nn[2]; c[1] = e1[0] * nn[2] - e1[2] * nn[0]; c[2] = e1[1] * nn[0] - e1[0] * nn[1];
    for (k = 0; k < 3; k++) N[k] = s * c[k];
    for (i = 0; i < 3; i++) {
        const int a = i, b = (i + 1) % 3;
        double e[3];
        for (k = 0; k < 3; k++) e[k] = (V[b][k] - V[a][k]) * w[a];
        C[i][0] = N[1] * e[2] - N[2] * e[1]; C[i][1] = N[2] * e[0] - N[0] * e[2]; C[i][2] = N[0] * e[1] - N[1] * e[0];
        d[i] = C[i][0] * V[a][0] + C[i][1] * V[a][1] + C[i][2] * V[a][2] - 0.005;
    }
    {   /* bounded (a triangle, or nothing) only if the three edge normals are not all on one side: the signs of
         * N . (C_i x C_j) for consecutive pairs all the same */
        double sg[3];
        for (q = 0; q < 3; q++) {
            const double *a = C[q], *b = C[(q + 1) % 3];
            sg[q] = N[0] * (a[1] * b[2] - a[2] * b[1]) + N[1] * (a[2] * b[0] - a[0] * b[2]) + N[2] * (a[0] * b[1] - a[1] * b[0]);
        }
        if (!((sg[0] > 0 && sg[1] > 0 && sg[2] > 0) || (sg[0] < 0 && sg[1] < 0 && sg[2] < 0))) return 0;
    }
    {
        const double dn = N[0] * V[0][0] + N[1] * V[0][1] + N[2] * V[0][2];
        double Q[3][3];
        for (q = 0; q < 3; q++) {
            const int a = q, b = (q + 1) % 3;
            const double *r0 = C[a], *r1 = C[b], *r2 = N;
            const double det = r0[0] * (r1[1] * r2[2] - r1[2] * r2[1]) - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0]) + r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]);
            const double y0 = d[a], y1 = d[b], y2 = dn;
            double sc = 0;
            for (k = 0; k < 3; k++) sc += fabs(r0[k]) * fabs(r1[k]) * fabs(r2[k]);
            if (!(fabs(det) > 1e-12 * (sc + 1e-30)) || !isfinite(det)) return 0;
            Q[q][0] = (y0 * (r1[1] * r2[2] - r1[2] * r2[1]) - r0[1] * (y1 * r2[2] - r1[2] * y2) + r0[2] * (y1 * r2[1] - r1[1] * y2)) / det;
            Q[q][1] = (r0[0] * (y1 * r2[2] - r1[2] * y2) - y0 * (r1[0] * r2[2] - r1[2] * r2[0]) + r0[2] * (r1[0] * y2 - y1 * r2[0])) / det;
            Q[q][2] = (r0[0] * (r1[1] * y2 - y1 * r2[1]) - r0[1] * (r1[0] * y2 - y1 * r2[0]) + y0 * (r1[0] * r2[1] - r1[1] * r2[0])) / det;
            /* (the region is the triangle of these corners, or empty: the corners hold it either way) */
            for (k = 0; k < 3; k++) if (!isfinite(Q[q][k]) || fabs(Q[q][k]) > 1e6) return 0;
        }
        for (q = 0; q < 3; q++) for (k = 0; k < 3; k++) { if (Q[q][k] < lo[k]) lo[k] = Q[q][k]; if (Q[q][k] > hi[k]) hi[k] = Q[q][k]; }
        (void)j;
    }
    return 1;
}
void nh_box_reset(void) { memset(g_nh_box, 0, sizeof g_nh_box); }   /* (the host check) */
static NhBox *nh_mesh_box(uint32_t mesh, uint32_t tri, uint32_t n)
{
    uint32_t h = (mesh * 2654435761u) >> 21, i;
    NhBox *b = NULL;
    for (i = 0; i < 8; i++) {
        NhBox *c = &g_nh_box[(h + i) & (NH_NBOX - 1)];
        if (c->mesh == mesh || c->state == 0) { b = c; break; }
    }
    if (!b) b = &g_nh_box[h & (NH_NBOX - 1)];
    if (b->mesh != mesh || b->tri != tri || b->count != n || b->state == 0) {
        b->mesh = mesh; b->tri = tri; b->count = n; b->uses = 0; b->changes = 0; b->state = 3;   /* (3: to be made) */
    }
    if (b->state == 2) return b;
    if (b->state == 1 && (++b->uses & 3u) == 0) {
        const uint32_t sm = nh_mesh_sum((const uint8_t *)XBOX_PTR(tri), n);
        if (sm != b->sum) { if (++b->changes >= 2) { b->state = 2; return b; } b->state = 3; }
    }
    if (b->state == 3) {
        const uint8_t *t = (const uint8_t *)XBOX_PTR(tri);
        double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 }, mx = 0;
        uint32_t k;
        int ok = 1;
        for (k = 0; k < n && ok; k++) ok = nh_tri_reach(t + 0x50u * k, lo, hi);
        if (!ok || n == 0) { b->state = 2; return b; }
        for (k = 0; k < 3; k++) { if (fabs(lo[k]) > mx) mx = fabs(lo[k]); if (fabs(hi[k]) > mx) mx = fabs(hi[k]); }
        for (k = 0; k < 3; k++) { const double m = 0.05 + 1e-4 * mx; b->lo[k] = (float)(lo[k] - m); b->hi[k] = (float)(hi[k] + m); }
        b->sum = nh_mesh_sum(t, n);
        b->state = 1;
    }
    return b;
}
/* (v212) 0x000458D0: a line against every triangle of a mesh (0x50 bytes each, [mesh] of them at [mesh+4]),
 * keeping the nearest hit in the line's record (flag [+0x6C] bit 0, distance [+0x5C], normal [+0x4C..0x58],
 * who [+0x60], which triangle [+0x64]). Up to 5800 triangle tests a frame in Helm's Deep, where it was 6-7% of
 * the game thread with the tests inside it. The loop by hand: the same calls to the line test (0x3FD30, itself
 * by hand) with the same stack and registers, the same writes, without the lifted loop's spills and pushes.
 * TT_NATIVE_MESHRAY=0: the lifted one. */
extern void sub_000458D0_gen(void);
extern void sub_0003FD30(void);
void tt_nat_000458D0(void)
{
    int fast = 0;
    RECOMP_CTX_DECL;
    static int on = -1;
    const uint32_t sp0 = g_esp, mesh = g_ecx;
    uint32_t count, arg1, ray, tri, i, ecx_cur = mesh;
    const uint32_t E = sp0 - 0x24u, aTP = sp0 + 8u, aN = sp0 - 0x10u;
    if (__builtin_expect(on < 0, 0)) { const char *e = getenv("TT_NATIVE_MESHRAY"); on = !(e && e[0] == '0'); }
    count = RD32(XBOX_PTR(mesh));
    arg1 = RD32(XBOX_PTR(sp0 + 4u)); ray = RD32(XBOX_PTR(sp0 + 8u));
    tri = RD32(XBOX_PTR(mesh + 4u));
    if (!on || nh_overlap(mesh, 8, sp0 - 0x100u, 0x110u) || nh_overlap(ray, 0x70, sp0 - 0x100u, 0x110u)
        || ((int32_t)count > 0 && (count > 0x100000u || nh_overlap(tri, count * 0x50u, sp0 - 0x100u, 0x110u)))) {
        sub_000458D0_gen();
        return;
    }
    if ((int32_t)count <= 0) { g_eax = count; g_esp = sp0 + 12u; return; }
    {   /* (the lifted frame: what it keeps there, for anything that reads it) */
        WR32(XBOX_PTR(sp0 - 0x14u), mesh);
    }
    if (__builtin_expect(g_nh_cull_on < 0, 0)) { const char *e = getenv("TT_MESHCULL"); g_nh_cull_on = e && e[0] == '1'; }   /* (v215: off unless TT_MESHCULL=1 -- the ladders of Helm's Deep's walls never came with it (moving meshes: a box stale for its first uses), and it saved little: 3 of ~30 mesh tests a frame) */
    g_nh_cull_n[0]++;
    if (g_nh_cull_on) {   /* (v214) the line wholly outside the mesh's reach: nothing to hit */
        const NhBox *b = nh_mesh_box(mesh, tri, count);
        if (b->state == 1) {
            const uint8_t *r = (const uint8_t *)XBOX_PTR(ray);
            int k, out = 0;
            for (k = 0; k < 3 && !out; k++) {
                const float p = RDF(r + 4 * k), dd = RDF(r + 0x20 + 4 * k);
                const float a = p - 0.0011f * dd, z = p + dd;
                const float mn = a < z ? a : z, mxv = a < z ? z : a;
                if (!(mxv >= b->lo[k] && mn <= b->hi[k])) out = 1;   /* (a NaN never skips) */
                if (isnan(p) || isnan(dd)) { out = 0; break; }
            }
            if (out) {
                g_nh_cull_n[1]++; g_nh_cull_n[2] += count;
                g_eax = count; g_edx = mesh; g_ecx = tri + 0x50u * (count - 1u);
                g_esp = sp0 + 12u;
                return;
            }
        }
    }
    {   /* (v226) the faces the ray's mask turns away, passed in a tight loop: one host pointer for the whole array
         * (when it is one block of host memory), the mask and the count read once (nothing is written between two
         * such faces), the faces 8 ahead fetched while these are looked at. Only a face that is tested goes the
         * full way below (and the mask and count are read again after it, as the lifted loop does). */
        const uint8_t *hp0 = (const uint8_t *)XBOX_PTR(tri);
        const int lin = (int32_t)count > 0 && (const uint8_t *)XBOX_PTR(tri + 0x50u * (count - 1u)) == hp0 + 0x50u * (count - 1u);
        fast = lin;
    }
    for (i = 0; ; ) {
        if (fast) {
            const uint8_t m = *(const uint8_t *)XBOX_PTR(ray + 0x44u);
            const uint8_t *hp = (const uint8_t *)XBOX_PTR(tri);
            while ((int32_t)i < (int32_t)count && (m & hp[0x4C])) {
                __builtin_prefetch(hp + 8u * 0x50u + 0x4Cu);
                hp += 0x50u; tri += 0x50u; i++;
            }
            if (!((int32_t)i < (int32_t)count)) break;
        }
        {
        const uint8_t fl = *(const uint8_t *)XBOX_PTR(tri + 0x4Cu);
        if (!(*(const uint8_t *)XBOX_PTR(ray + 0x44u) & fl)) {
            WR32(XBOX_PTR(E - 0x4u), aN); WR32(XBOX_PTR(E - 0x8u), aTP);
            WR32(XBOX_PTR(E - 0xCu), ray + 0x20u); WR32(XBOX_PTR(E - 0x10u), ray);
            WR32(XBOX_PTR(E - 0x14u), 0x00045914u);
            g_esp = E - 0x14u; g_ecx = tri; g_eax = ray + 0x20u; g_edx = aTP;
            {   const uint32_t sb = g_ebx, ss = g_esi, sd = g_edi;
                g_ebx = arg1; g_esi = ray; g_edi = tri;
                sub_0003FD30();
                g_ebx = sb; g_esi = ss; g_edi = sd; }
            ecx_cur = g_ecx;
            if (g_eax & 0xFFu) {
                const uint32_t f = RD32(XBOX_PTR(ray + 0x6Cu));
                int upd = 0;
                if (!(f & 1u)) {
                    WR32(XBOX_PTR(ray + 0x6Cu), f | 1u);
                    upd = 1; ecx_cur = RD32(XBOX_PTR(aN + 12u));
                } else {
                    const int cmp = RECOMP_FCMP((double)RDF(XBOX_PTR(aTP)), (double)RDF(XBOX_PTR(ray + 0x5Cu)));
                    g_fp_cmp = cmp; g_fp_cc = RECOMP_FCMP_CC(cmp);
                    if (cmp == -1) { upd = 1; ecx_cur = RD32(XBOX_PTR(aN + 8u)); }
                }
                if (upd) {
                    WR32(XBOX_PTR(ray + 0x5Cu), RD32(XBOX_PTR(aTP)));
                    WR32(XBOX_PTR(ray + 0x4Cu), RD32(XBOX_PTR(aN)));
                    WR32(XBOX_PTR(ray + 0x50u), RD32(XBOX_PTR(aN + 4u)));
                    WR32(XBOX_PTR(ray + 0x54u), RD32(XBOX_PTR(aN + 8u)));
                    WR32(XBOX_PTR(ray + 0x58u), RD32(XBOX_PTR(aN + 12u)));
                    WR32(XBOX_PTR(ray + 0x60u), arg1);
                    WR32(XBOX_PTR(ray + 0x64u), tri);
                }
            }
        }
        }
        i++; tri += 0x50u;
        count = RD32(XBOX_PTR(RD32(XBOX_PTR(sp0 - 0x14u))));
        if (!((int32_t)i < (int32_t)count)) break;
    }
    g_eax = count; g_edx = RD32(XBOX_PTR(sp0 - 0x14u)); g_ecx = ecx_cur;
    g_esp = sp0 + 12u;
}

/* (v225) THE SECTORS A RAY CROSSES, written by hand. 0x46120 (thiscall, the sector node; its one argument the ray
 * record) tests the node's box against the ray and, when it is crossed, lists the node (when it holds meshes,
 * +0x48) at 0x3C3DAC (count at 0x3C3DA8) and walks its children (+0x20.., count +0x44). The test is 0x45EC0 (the box
 * +0x00 / +0x10 grown by the ray's radius +0x30 plus 0x18AC68, its w by 0x18AC38) and 0x45C10, a segment against a
 * box by separating axes (the segment's half +0x00 .. +0x10 by 0x18AC34, the box's half extents, the three
 * cross-product axes). Here the same arithmetic in the same order, doubles where the x87 kept a value on its stack,
 * floats where it stored one, and the same NaN outcomes -- host-checked against the lifted three. Only the caller
 * 0x44760's walk (from 0x4478E) is taken here: its registers and x87 slots after are dead there (0x46040 reloads all).
 * TT_NATIVE_SECT=0: the lifted code. */
static int nh_sect_box(uint32_t a, uint32_t b, float R, uint32_t nd)
{
    const uintptr_t k0 = RECOMP_K0;
    const float K = RDF(k0 + 0x18AC34u), K68 = RDF(k0 + 0x18AC68u);
    const float P0 = RDF(XBOX_PTR(a)), P1 = RDF(XBOX_PTR(a + 4)), P2 = RDF(XBOX_PTR(a + 8));
    const float D0 = (float)((double)RDF(XBOX_PTR(b)) - P0), D1 = (float)((double)RDF(XBOX_PTR(b + 4)) - P1), D2 = (float)((double)RDF(XBOX_PTR(b + 8)) - P2);
    const float r = (float)((double)R + K68);
    const float mn0 = (float)((double)RDF(XBOX_PTR(nd)) - r), mn1 = (float)((double)RDF(XBOX_PTR(nd + 4)) - r), mn2 = (float)((double)RDF(XBOX_PTR(nd + 8)) - r);
    const float mx0 = (float)((double)RDF(XBOX_PTR(nd + 16)) + r), mx1 = (float)((double)RDF(XBOX_PTR(nd + 20)) + r), mx2 = (float)((double)RDF(XBOX_PTR(nd + 24)) + r);
    const double h0 = (double)D0 * K;
    const float h1 = (float)((double)D1 * K), h2 = (float)((double)D2 * K);
    const double c0 = (double)P0 + h0, c1 = (double)P1 + h1;
    const float c2 = (float)((double)P2 + h2);
    const float e0 = (float)(((double)mx0 - mn0) * K), e1 = (float)(((double)mx1 - mn1) * K);
    const float e2 = (float)((double)(float)((double)mx2 - mn2) * K);
    const double m0 = (double)mn0 + e0;
    const float m1 = (float)((double)mn1 + e1), m2 = (float)((double)mn2 + e2);
    const float t0 = (float)(c0 - m0), t1 = (float)(c1 - m1);
    const double t2d = (double)c2 - m2;
    const float t2 = (float)t2d;
    const float w0 = (float)(t2d * h1 - (double)t1 * h2);
    const float w1 = (float)((double)h2 * t0 - (double)t2 * h0);
    const float w2 = (float)((double)t1 * h0 - (double)h1 * t0);
    const float ah0 = (float)(!(h0 >= 0.0) ? -h0 : h0);
    const float ah1 = !(h1 >= 0.0f) ? -h1 : h1, ah2 = !(h2 >= 0.0f) ? -h2 : h2;
    const double at0 = !(t0 >= 0.0f) ? -(double)t0 : (double)t0;
    const float at1 = !(t1 >= 0.0f) ? -t1 : t1, at2 = !(t2 >= 0.0f) ? -t2 : t2;
    double aw, sm;
    if (at0 > (double)ah0 + e0) return 0;
    if ((double)e1 + ah1 < (double)at1) return 0;
    if ((double)e2 + ah2 < (double)at2) return 0;
    aw = !(w0 >= 0.0f) ? -(double)w0 : (double)w0;
    if (aw > (double)e2 * ah1 + (double)e1 * ah2) return 0;
    aw = !(w1 >= 0.0f) ? -(double)w1 : (double)w1;
    if (aw > (double)e2 * ah0 + (double)ah2 * e0) return 0;
    aw = (double)(!(w2 >= 0.0f) ? -w2 : w2);
    sm = (double)e1 * ah0 + (double)ah1 * e0;
    return !(sm < aw);
}
static void nh_sect_walk(uint32_t nd, uint32_t ray, int depth)
{
    int32_t k;
    if (!nh_sect_box(ray, ray + 0x10u, RDF(XBOX_PTR(ray + 0x30u)), nd)) return;
    if (RD32(XBOX_PTR(nd + 0x48u))) {
        const uint32_t n = RD32(XBOX_PTR(0x3C3DA8u));
        WR32(XBOX_PTR(0x3C3DACu + 4u * n), nd);
        WR32(XBOX_PTR(0x3C3DA8u), n + 1u);
    }
    if (depth > 64) return;
    for (k = 0; k < (int32_t)RD32(XBOX_PTR(nd + 0x44u)); k++) {
        const uint32_t c = RD32(XBOX_PTR(nd + 0x20u + 4u * (uint32_t)k));
        if (c) nh_sect_walk(c, ray, depth + 1);
    }
}
/* 1: done here (the caller pops nothing more: ret 4 done) */
int tt_nat_00046120(void)
{
    RECOMP_CTX_DECL;
    static int on = -1;
    const uint32_t ray = RD32(XBOX_PTR(g_esp + 4u));
    if (__builtin_expect(on < 0, 0)) { const char *e = getenv("TT_NATIVE_SECT"); on = nat_on() && !(e && e[0] == '0'); }
    if (!on || g_ecx < 0x10000u || ray < 0x10000u) return 0;
    nh_sect_walk(g_ecx, ray, 0);
    g_esp += 8u;   /* (ret 4) */
    return 1;
}

/* (v228) 0x43D00: a line from a point (arg 1) along a direction (arg 2) walked across the ground's height grid
 * ([[0x3C35A0]+0x54]: +0 columns, +4 rows, +8 cells a unit, +0x10/+0x14 its corner, +0x1C the heights, int16 x 1/12),
 * cell by cell (the line's crossing of the next x or y edge), until it is under a cell's height (that height back on
 * the x87 stack) or leaves the grid (the point's own z back). In Helm's Deep's rain the hottest function of the game
 * thread (4-8% own). The same doubles, float stores, truncating conversions and compares as the lifted code; eax / ecx /
 * edx, the x87 compare state, the argument slot it writes its step into, g_ebp / g_seh_ebp as it leaves them.
 * Host-checked against the lifted one (work/nt run_43d). 1: done (ret). */
int tt_nat_00043D00(void)
{
    RECOMP_CTX_DECL;
    static int on = -1;
    const uint32_t sp = g_esp, P = RD32(XBOX_PTR(sp + 4u)), D = RD32(XBOX_PTR(sp + 8u));
    uint32_t G, r_a, r_c, r_b, r_di, W, H, hp;
    uint16_t cw0, sw;
    int32_t cmp;
    float px, py, cs, K, Dx, Dy, Dz, ox, oy, sc, a, tAf, tBf, hf;
    double z, b1, tA, tB;
    if (__builtin_expect(on < 0, 0)) { const char *e = getenv("TT_NATIVE_GRID"); on = nat_on() && !(e && e[0] == '0'); }
    if (!on || P < 0x10000u || D < 0x10000u) return 0;
    G = RD32(XBOX_PTR(0x3C35A0u));
    if (G < 0x10000u) return 0;
    G = RD32(XBOX_PTR(G + 0x54u));
    if (G < 0x10000u) return 0;
    if (!(*(const uint8_t *)XBOX_PTR(0x3C3644u) & 1u)) {
        const double f = (double)RDF(XBOX_PTR(0x18AC38u)) / (double)RDF(XBOX_PTR(G + 8u));
        WR32(XBOX_PTR(0x3C3644u), RD32(XBOX_PTR(0x3C3644u)) | 1u);
        WRF(XBOX_PTR(0x3C3640u), (float)f);
    }
    cw0 = g_fp_control_word;
    sc = RDF(XBOX_PTR(G + 8u)); ox = RDF(XBOX_PTR(G + 0x10u)); oy = RDF(XBOX_PTR(G + 0x14u));
    px = RDF(XBOX_PTR(P)); py = RDF(XBOX_PTR(P + 4u));
    {
        const float fx = (float)(((double)px - (double)ox) * (double)sc);
        const float fy = (float)(((double)py - (double)oy) * (double)sc);
        r_b = (uint32_t)(int32_t)recomp_fist((double)fx, (uint16_t)(cw0 | 0xC00u), 32);
        r_di = (uint32_t)(int32_t)recomp_fist((double)fy, (uint16_t)(cw0 | 0xC00u), 32);
    }
    g_ebp = sp - 4u; g_seh_ebp = sp - 4u;
    W = RD32(XBOX_PTR(G));
    r_a = r_b; r_c = P;
    if ((int32_t)r_b >= (int32_t)W) goto out_z;
    z = (double)RDF(XBOX_PTR(P + 8u));
    cs = RDF(XBOX_PTR(0x3C3640u)); K = RDF(XBOX_PTR(0x18AC58u));
    Dx = RDF(XBOX_PTR(D)); Dy = RDF(XBOX_PTR(D + 4u)); Dz = RDF(XBOX_PTR(D + 8u));
    H = RD32(XBOX_PTR(G + 4u)); hp = RD32(XBOX_PTR(G + 0x1Cu));
    {
        const float hs = RDF(XBOX_PTR(0x171070u));
        const uint16_t t2 = (uint16_t)((((uint32_t)g_fp_top - 2u) & 7u) << 11), t1 = (uint16_t)((((uint32_t)g_fp_top - 1u) & 7u) << 11);
        for (;;) {
            if ((int32_t)r_b < 0) goto out_z;
            r_c = H;
            if ((int32_t)r_di >= (int32_t)r_c || (int32_t)r_di < 0) goto out_z;
            a = (float)((double)px - ((double)(int32_t)r_b * (double)cs + (double)ox));
            b1 = (double)py - ((double)(int32_t)r_di * (double)cs + (double)oy);
            if (RECOMP_FCMP((double)a, (double)K) == -1) a = 0.0f;
            if (RECOMP_FCMP(b1, (double)K) == -1) b1 = (double)K;
            if (RECOMP_FCMP((double)Dx, (double)K) == -1) tA = (double)a / -(double)Dx;
            else tA = ((double)cs - (double)a) / (double)Dx;
            tAf = (float)tA;
            if (RECOMP_FCMP((double)Dy, (double)K) == -1) tB = b1 / -(double)Dy;
            else tB = ((double)cs - b1) / (double)Dy;
            tBf = (float)tB;
            r_c = (uint32_t)(int32_t)RD16(XBOX_PTR(hp + 2u * (H * r_b + r_di)));
            r_c = (uint32_t)(int32_t)(int16_t)r_c;
            hf = (float)((double)(int32_t)r_c * (double)hs);
            WRF(XBOX_PTR(sp + 8u), tAf);
            if (RECOMP_FCMP((double)tAf, (double)tBf) == -1) {
                z = z + (double)Dz * (double)tAf;
                cmp = RECOMP_FCMP(z, (double)hf);
                if (cmp == -1 || cmp == 0) { sw = (uint16_t)(t1 | RECOMP_FCMP_CC(cmp)); r_a = (hp & 0xFFFF0000u) | sw; goto out_hit; }
                cmp = RECOMP_FCMP((double)Dx, (double)K);
                if (cmp == -1) r_b--; else r_b++;
                px = (float)((double)tAf * (double)Dx + (double)px);
                py = (float)((double)tAf * (double)Dy + (double)py);
            } else {
                z = z + (double)Dz * (double)tBf;
                cmp = RECOMP_FCMP(z, (double)hf);
                if (cmp == -1 || cmp == 0) { sw = (uint16_t)(t1 | RECOMP_FCMP_CC(cmp)); r_a = (hp & 0xFFFF0000u) | sw; goto out_hit; }
                cmp = RECOMP_FCMP((double)Dy, (double)K);
                if (cmp == -1) r_di--; else r_di++;
                px = (float)((double)tBf * (double)Dx + (double)px);
                py = (float)((double)tBf * (double)Dy + (double)py);
            }
            g_fp_cmp = cmp; g_fp_cc = RECOMP_FCMP_CC(cmp);
            r_a = W;
            if ((int32_t)r_b >= (int32_t)r_a) goto out_z;
        }
        (void)t2;
out_hit:
        g_fp_cmp = cmp; g_fp_cc = RECOMP_FCMP_CC(cmp);
        g_eax = r_a; g_ecx = r_c; g_edx = G;
        g_fp_top = (g_fp_top + 7u) & 7u; g_fp_stack[g_fp_top] = (double)hf;
        g_esp = sp + 4u;
        return 1;
    }
out_z:
    g_eax = r_a; g_ecx = r_c; g_edx = P;
    g_fp_top = (g_fp_top + 7u) & 7u; g_fp_stack[g_fp_top] = (double)RDF(XBOX_PTR(P + 8u));
    g_esp = sp + 4u;
    return 1;
}

/* (v225) room for vita-elf-create: the code segment had grown to 3.2 KB short of the data segment's 64 KB boundary,
 * and the SCE data it appends there needs 4.5 KB ("segment 1 overlaps"). 4 KB more of code moves the data segment to
 * the next boundary (some 60 KB of room after it). Never read. */
__attribute__((section(".text.tt_pad"), used, aligned(4))) const unsigned char tt_text_pad[4096] = { 0 };
