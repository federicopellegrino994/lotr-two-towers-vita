/* (v153) 0x00048CD0, the particle quad builder, written out natively.
 *
 * thiscall, ecx a particle with four source vertices of 0x30 bytes going
 * down from +0xA0 (position +0, uv +0x10, colour floats +0x20/+0x24/+0x28).
 * Appends one quad (4 x 0x18: x y z colour u v) at [0x407E9C] + count*0x60,
 * count [0x407E30] < 0x600, vertices written in the order 1 0 2 3. Colours
 * 0xFF r g b, each channel the low byte of _ftol2(c * [0x170458]). The odd
 * vertices are turned about the even one before them: the offset (d, 1)
 * through a matrix picked by _ftol2(x*y*z)*7 - 1 (16 of them, at 0x408170 or
 * 0x408710 by [this+4] < 4), then through 0x407EB0 + ([this+4] & 3)*64, and
 * added back. 11-12% of the game thread in a crowd (the v152 profile), almost
 * all of it the lifted code's bookkeeping and its 14 calls a particle.
 * The same arithmetic and rounding as the lifted form (the float sums done in
 * double and stored to float equal the float sums); the x87 slots below the
 * top, g_ebp and the scratch registers are left as the lifted code leaves
 * them (with the native _ftol2). Checked against it on the host. */
static inline uint32_t n48_ftol(double x)
{
    int64_t r = (x > -9223372036854775808.0 && x < 9223372036854775808.0) ? (int64_t)x : INT64_MIN;
    return (uint32_t)(uint64_t)r;
}
static inline void n48_vm4(float *o, const float *a, uint32_t m)
{
#if defined(__vita__)
    /* (v158) by lanes of one register (no NEON -> ARM transfers); VMLA rounds the product first: the same sums */
    const float *M = (const float *)XBOX_PTR(m);
    const float32x4_t v = vld1q_f32(a);
    const float32x2_t lo = vget_low_f32(v), hi = vget_high_f32(v);
    float32x4_t r = vmulq_lane_f32(vld1q_f32(M), lo, 0);
    r = vmlaq_lane_f32(r, vld1q_f32(M + 4), lo, 1);
    r = vmlaq_lane_f32(r, vld1q_f32(M + 8), hi, 0);
    r = vmlaq_lane_f32(r, vld1q_f32(M + 12), hi, 1);
    vst1q_f32(o, r);
#else
    int i;
    for (i = 0; i < 4; i++) {
        float r = MEMF(m + 4u * i) * a[0];
        r = r + MEMF(m + 16u + 4u * i) * a[1];
        r = r + MEMF(m + 32u + 4u * i) * a[2];
        r = r + MEMF(m + 48u + 4u * i) * a[3];
        o[i] = r;
    }
#endif
}
/* (v158) the guest registers: on the Vita the thread's struct (recomp_ctx), not emutls */
#if defined(__vita__)
#define N48_CTX RecompRegs *const _n48c = recomp_ctx()
#define N_EAX (_n48c->r_eax)
#define N_ECX (_n48c->r_ecx)
#define N_EDX (_n48c->r_edx)
#define N_ESP (_n48c->r_esp)
#define N_EBP (_n48c->r_ebp)
#define N_SEH (_n48c->r_seh_ebp)
#define N_FPT (_n48c->r_fp_top)
#define N_FPS (_n48c->r_fp_stack)
#else
#define N48_CTX (void)0
#define N_EAX g_eax
#define N_ECX g_ecx
#define N_EDX g_edx
#define N_ESP g_esp
#define N_EBP g_ebp
#define N_SEH g_seh_ebp
#define N_FPT g_fp_top
#define N_FPS g_fp_stack
#endif
static int tt_n48_native(void)
{
    N48_CTX;
    static const uint32_t offs[4] = { 0x18u, 0u, 0x30u, 0x48u };
    const uint32_t e0 = N_ESP, th = N_ECX, S1 = e0 - 0x98u;
    const uint32_t cnt = MEM32(0x407E30u);
    uint32_t q, j, sel = 0;
    float K, cx = 0.0f, cy = 0.0f, cz = 0.0f;
    double s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int odd = 0;
    if (th < 0x10000u) return 0;
    if ((int32_t)cnt >= 0x600) {
        N_ECX = MEM32(0x407E98u);
        N_EAX = N_ECX + 0x570u;
        N_ESP = e0 + 4u;
        return 1;
    }
    q = cnt * 0x60u + MEM32(0x407E9Cu);
    if (q < 0x10000u) return 0;
    K = MEMF(0x170458u);
    for (j = 0; j < 4; j++) {
        const uint32_t p = th + 0xA0u - 0x30u * j, v = q + offs[j];
        uint32_t r, g, b;
        float x, y, z;
        MEMF(v + 0x10) = MEMF(p + 0x10);
        MEMF(v + 0x14) = MEMF(p + 0x14);
        r = n48_ftol((double)MEMF(p + 0x20) * (double)K);
        g = n48_ftol((double)MEMF(p + 0x24) * (double)K);
        b = n48_ftol((double)MEMF(p + 0x28) * (double)K);
        MEM32(v + 0xC) = (((((r | 0xFFFFFF00u) << 8) | (g & 0xFFu)) << 8) | (b & 0xFFu));
        x = MEMF(p); y = MEMF(p + 4); z = MEMF(p + 8);
        if (!(j & 1u)) {
            cx = x; cy = y; cz = z;
            MEMF(v) = x; MEMF(v + 4) = y; MEMF(v + 8) = z;
        } else {
            const int32_t t4 = (int32_t)MEM32(th + 4);
            const double prod = ((double)z * (double)y) * (double)x;
            const uint32_t k = n48_ftol(prod) * 7u;
            const uint32_t M = (((k - 1u) & 0xFu) << 6) + (t4 < 4 ? 0x408170u : 0x408710u);
            const uint32_t M2 = (((uint32_t)t4 & 3u) << 6) + 0x407EB0u;
            float a[4], o1[4], o2[4];
            a[0] = x - cx; a[1] = y - cy; a[2] = z - cz; a[3] = 1.0f;
            n48_vm4(o1, a, M);
            a[0] = o1[0]; a[1] = o1[1]; a[2] = o1[2]; a[3] = 1.0f;
            n48_vm4(o2, a, M2);
            MEMF(v) = o2[0] + cx; MEMF(v + 4) = o2[1] + cy; MEMF(v + 8) = o2[2] + cz;
            s1 = (double)y - (double)cy; s2 = (double)y; s3 = prod; odd = 1;
            sel = S1 + 0x78u;
        }
    }
    MEM32(0x407E30u) = MEM32(0x407E30u) + 1u;
    if (odd) {
        N_FPS[(N_FPT + 7u) & 7u] = s1;
        N_FPS[(N_FPT + 6u) & 7u] = s2;
        N_FPS[(N_FPT + 5u) & 7u] = s3;
        N_EBP = N_SEH = S1 - 8u;
    }
    N_EAX = 0xFFFFFFB8u;
    N_ECX = sel;
    N_EDX = th - 0x20u;
    N_ESP = e0 + 4u;
    return 1;
}
