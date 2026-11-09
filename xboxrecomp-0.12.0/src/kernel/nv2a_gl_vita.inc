/*
 * nv2a_gl_vita.inc -- the OpenGL renderer (nv2a_gl.inc) on vitaGL (PS Vita).
 *
 * Included by nv2a_gl.inc in place of SDL's OpenGL loader when building with
 * vitasdk. nv2a_gl.inc keeps its design (decoded state from nv2a_sw3d.inc,
 * vertices transformed on the CPU, one GL texture per guest surface, one
 * specialized fragment shader per combiner program); what vitaGL does not
 * have is filled in here:
 *
 *   - Shaders are Cg (vitaShaRK compiles them on the console, cached in
 *     ux0:data/twotowers/shader_cache); GL_VERTEX_SHADER/GL_FRAGMENT_SHADER
 *     become the CG types, multi-part sources are joined.
 *   - No uniform blocks (glUniformBlockBinding is a stub): gpu_ub() data is
 *     kept here per block and sent before each draw with glUniform4fv to the
 *     named uniforms the current program has (vp, clear_color, post, c0_k,
 *     c1_k, ff0, ...). The integer rows of the combiner block become floats.
 *   - Depth/stencil surfaces are renderbuffers (vitaGL attaches only colour
 *     textures); they cannot be sampled.
 *   - The full-screen-triangle programs read a position attribute (no
 *     gl_VertexID).
 *   - Surface orientation: OpenGL keeps clip y = -1 at a framebuffer
 *     object's row 0; vitaGL may not. A self-test at start-up draws into a
 *     texture and looks at its memory; if row 0 holds clip y = +1, every
 *     draw into a surface is flipped back (clip y, front face, scissor,
 *     viewport, blit rows), so a surface's row 0 is still the guest's top row
 *     and uploads, samples and read-backs need no change.
 *     RECOMP_VGL_FLIP=0/1 overrides the test.
 *   - No glBlendColor (constant-colour blend factors fall back to ONE/ZERO),
 *     no glDrawBuffer, no MSAA, no derivatives (no FXAA or sharpening).
 */
#include <vitaGL.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>

#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD 0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD 0x813B
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#endif
#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

/* the picture owner (fb_present.c): vitaGL is started by whichever of the
 * renderer and the picture thread asks first */
extern int xbox_VitaGLAcquire(void);
extern void xbox_VitaGLRelease(void);

static int g_vgl_flip;                 /* see the top of this file */

/* Start-up trace: the first GL calls after the renderer starts, each written
 * to the log at once (the log is otherwise flushed twice a second, and a
 * crash loses the last half second). [VGLT] <line in nv2a_gl.inc> <call>.
 * RECOMP_VGL_TRACE=<n> calls (default 0: off). */
#include <stdarg.h>
static int g_vgl_tr = -1;
static unsigned g_vgl_tr_n;
static void vgl_tr_init(void)
{
    const char *e = getenv("RECOMP_VGL_TRACE");
    g_vgl_tr = e && *e ? atoi(e) : 0;   /* (off unless asked: each line is a write to the card) */
}
static void vgl_tr(const char *fn, int line)
{
    if (g_vgl_tr <= 0) return;
    g_vgl_tr--;
    fprintf(stderr, "  [VGLT] %u %d %s\n", g_vgl_tr_n++, line, fn);
    fflush(stderr);
    if (!g_vgl_tr) { fprintf(stderr, "  [VGLT] trace over\n"); fflush(stderr); }
}
static void vgl_trf(const char *fmt, ...)
{
    va_list ap;
    if (g_vgl_tr <= 0) return;
    fprintf(stderr, "  [VGLT] %u ", g_vgl_tr_n++);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
#define VGLT(fn) vgl_tr(fn, __LINE__)

/* ---- framebuffer objects: what is bound, and how tall ---- */

#define VFB_N 256
static struct { GLuint fbo; uint32_t h; } g_vfb[VFB_N];
#define VTX_N 2048
static struct { GLuint name; uint32_t h; } g_vtx[VTX_N];   /* colour textures' heights */
static GLuint g_vgl_rfb, g_vgl_dfb;   /* bound read / draw framebuffers */

static void vtx_note(GLuint t, uint32_t h)
{
    uint32_t i, s = (t * 2654435761u) & (VTX_N - 1);
    for (i = 0; i < VTX_N; i++) {
        uint32_t k = (s + i) & (VTX_N - 1);
        if (!g_vtx[k].name || g_vtx[k].name == t) { g_vtx[k].name = t; g_vtx[k].h = h; return; }
    }
}
static uint32_t vtx_h(GLuint t)
{
    uint32_t i, s = (t * 2654435761u) & (VTX_N - 1);
    for (i = 0; i < VTX_N; i++) {
        uint32_t k = (s + i) & (VTX_N - 1);
        if (g_vtx[k].name == t) return g_vtx[k].h;
        if (!g_vtx[k].name) return 0;
    }
    return 0;
}
static void vfb_note(GLuint fbo, uint32_t h)
{
    int i, v = -1;
    for (i = 0; i < VFB_N; i++) {
        if (g_vfb[i].fbo == fbo) { g_vfb[i].h = h; return; }
        if (v < 0 && !g_vfb[i].fbo) v = i;
    }
    if (v >= 0) { g_vfb[v].fbo = fbo; g_vfb[v].h = h; }
}
static void vfb_forget(GLuint fbo)
{
    int i;
    for (i = 0; i < VFB_N; i++) if (g_vfb[i].fbo == fbo) g_vfb[i].fbo = 0;
}
static uint32_t vfb_h(GLuint fbo)
{
    int i;
    if (!fbo) return 0;
    for (i = 0; i < VFB_N; i++) if (g_vfb[i].fbo == fbo) return g_vfb[i].h;
    return 0;
}

static void vgl_bind_fb(GLenum t, GLuint f)
{
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) g_vgl_dfb = f;
    if (t == GL_FRAMEBUFFER || t == GL_READ_FRAMEBUFFER) g_vgl_rfb = f;
    glBindFramebuffer(t, f);
}
static void vgl_scissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    uint32_t H;
    if (g_vgl_flip && (H = vfb_h(g_vgl_dfb)) != 0) y = (GLint)H - y - h;
    glScissor(x, y, w, h);
}
static void vgl_viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    uint32_t H;
    if (g_vgl_flip && (H = vfb_h(g_vgl_dfb)) != 0) y = (GLint)H - y - h;
    glViewport(x, y, w, h);
}
static void vgl_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                     GLbitfield mask, GLenum filter)
{
    if (g_vgl_flip) {
        uint32_t hs = vfb_h(g_vgl_rfb), hd = vfb_h(g_vgl_dfb);
        GLint t;
        if (hs) { t = sy0; sy0 = (GLint)hs - sy1; sy1 = (GLint)hs - t; }
        if (hd) { t = dy0; dy0 = (GLint)hd - dy1; dy1 = (GLint)hd - t; }
    }
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

/* ---- shaders: Cg, one source string ---- */

#define VGL_IDS 8192
static uint8_t g_vgl_vin_sh[VGL_IDS], g_vgl_vin_pr[VGL_IDS];
static GLuint vgl_create_shader(GLenum t)
{
    return glCreateShader(t == GL_VERTEX_SHADER ? GL_CG_VERTEX_SHADER_EXT
                          : t == GL_FRAGMENT_SHADER ? GL_CG_FRAGMENT_SHADER_EXT : t);
}
static void vgl_shader_source(GLuint s, GLsizei n, const GLchar *const *src, const GLint *len)
{
    size_t tot = 0, at = 0;
    GLsizei i;
    char *b;
    GLint L;
    for (i = 0; i < n; i++) tot += len && len[i] >= 0 ? (size_t)len[i] : strlen(src[i]);
    b = (char *)malloc(tot + 1);
    if (!b) return;
    for (i = 0; i < n; i++) {
        size_t l = len && len[i] >= 0 ? (size_t)len[i] : strlen(src[i]);
        memcpy(b + at, src[i], l); at += l;
    }
    b[at] = 0;
    L = (GLint)at;
    if (s < VGL_IDS) g_vgl_vin_sh[s] = strstr(b, "float4 vin0") != NULL || strstr(b, "uniform float4 c[192]") != NULL;
    {
        const GLchar *one = b;
        glShaderSource(s, 1, &one, &L);
    }
    free(b);
}
/* (which shaders are vertex programs, which programs have one attached:
 * their inputs are vin<i>, the others' the CPU path's a_*) */
static void vgl_attach(GLuint p, GLuint s)
{
    if (p < VGL_IDS && s < VGL_IDS && g_vgl_vin_sh[s]) g_vgl_vin_pr[p] = 1;
    glAttachShader(p, s);
}
/* every program gets its attribute locations before linking */
static void vgl_link(GLuint p)
{
    static const char *nm[8] = { "a_p", "a_d0", "a_d1", "a_fog", "a_t0", "a_t1", "a_t2", "a_t3" };
    int i;
    static const char *vin[16] = { "vin0", "vin1", "vin2", "vin3", "vin4", "vin5", "vin6", "vin7",
                                   "vin8", "vin9", "vin10", "vin11", "vin12", "vin13", "vin14", "vin15" };
    if (p < VGL_IDS && g_vgl_vin_pr[p])
        for (i = 0; i < 16; i++) glBindAttribLocation(p, (GLuint)i, vin[i]);   /* (vertex programs: nv2a_gl.inc hwvp_translate_cg) */
    else
        for (i = 0; i < 8; i++) glBindAttribLocation(p, (GLuint)i, nm[i]);
    glLinkProgram(p);
    if (p < VGL_IDS) g_vgl_vin_pr[p] = 0;   /* (ids come back after a delete) */
}

/* ---- uniform blocks as plain uniforms ---- */

enum { VB_VSC = 0, VB_PSC = 1, VB_CLC = 2, VB_POST = 3, VB_VPC = 4, VB_N = 5 };
#define VB_ROWS 192   /* (VPC: the vertex program's 192 constants) */
static float g_vub[VB_N][VB_ROWS][4];
static uint32_t g_vub_gen[VB_N] = { 1, 1, 1, 1, 1 };

/* the combiner block's rows that hold integers (gpu_bind_stages' layout:
 * st[8] x {cin, ain, f1, f2, f3, c0, c1}, fin0, fin1, misc, ff0, ff1, fogc,
 * fogp, smode, sflags, sscale[4], atest, sdot, sinp) */
static int psc_int_row(int r)
{
    if (r < 56) return (r % 7) < 5;
    return r == 56 || r == 57 || r == 58 || r == 63 || r == 64 || r >= 69;
}
static void vgl_ub_set(int binding, const void *data, uint32_t bytes)
{
    uint32_t rows = bytes / 16, r, k;
    if (binding < 0 || binding >= VB_N) return;
    if (rows > VB_ROWS) rows = VB_ROWS;
    if (binding == VB_PSC) {
        const int32_t *iv = (const int32_t *)data;
        const float *fv = (const float *)data;
        for (r = 0; r < rows; r++)
            for (k = 0; k < 4; k++)
                g_vub[binding][r][k] = psc_int_row((int)r) ? (float)iv[r * 4 + k] : fv[r * 4 + k];
    } else memcpy(g_vub[binding], data, rows * 16);
    g_vub_gen[binding]++;
    if (!g_vub_gen[binding]) g_vub_gen[binding] = 1;
}

/* the uniforms a program may have, and where their values come from */
typedef struct { const char *name; uint8_t block, row; uint16_t n; } VUni;   /* (n: rows, 0 = 1) */
static const VUni g_vuni[] = {
    { "vp", VB_VSC, 0 }, { "clear_color", VB_CLC, 0 }, { "clc_f", VB_CLC, 0 }, { "post", VB_POST, 0 },
    { "c0_0", VB_PSC, 5 }, { "c0_1", VB_PSC, 12 }, { "c0_2", VB_PSC, 19 }, { "c0_3", VB_PSC, 26 },
    { "c0_4", VB_PSC, 33 }, { "c0_5", VB_PSC, 40 }, { "c0_6", VB_PSC, 47 }, { "c0_7", VB_PSC, 54 },
    { "c1_0", VB_PSC, 6 }, { "c1_1", VB_PSC, 13 }, { "c1_2", VB_PSC, 20 }, { "c1_3", VB_PSC, 27 },
    { "c1_4", VB_PSC, 34 }, { "c1_5", VB_PSC, 41 }, { "c1_6", VB_PSC, 48 }, { "c1_7", VB_PSC, 55 },
    { "ff0", VB_PSC, 59 }, { "ff1", VB_PSC, 60 }, { "fogc", VB_PSC, 61 }, { "fogp", VB_PSC, 62 },
    { "ss0", VB_PSC, 65 }, { "ss1", VB_PSC, 66 }, { "ss2", VB_PSC, 67 }, { "ss3", VB_PSC, 68 },
    { "atest", VB_PSC, 69 },
    { "c", VB_VPC, 0, 192 },
};
#define VUNI_N ((int)(sizeof g_vuni / sizeof g_vuni[0]))

typedef struct {
    GLuint prog;
    uint8_t tri, known;
    GLint loc[VUNI_N];
    uint32_t gen[VUNI_N];
} VProg;
#define VPROG_N 4096
static VProg *g_vprog;
static GLuint g_vgl_prog;
static VProg *g_vgl_cur;

static VProg *vprog_get(GLuint p, int make)
{
    uint32_t i, s;
    if (!p) return NULL;
    if (!g_vprog) { g_vprog = (VProg *)calloc(VPROG_N, sizeof *g_vprog); if (!g_vprog) return NULL; }
    s = (p * 2654435761u) & (VPROG_N - 1);
    for (i = 0; i < VPROG_N; i++) {
        VProg *e = &g_vprog[(s + i) & (VPROG_N - 1)];
        if (e->prog == p) return e;
        if (!e->prog) {
            if (!make) return NULL;
            memset(e, 0, sizeof *e);
            e->prog = p;
            return e;
        }
    }
    return NULL;
}
static void vprog_locs(VProg *e)
{
    int u;
    for (u = 0; u < VUNI_N; u++) { e->loc[u] = glGetUniformLocation(e->prog, g_vuni[u].name); e->gen[u] = 0; }
    e->known = 1;
}
static void vgl_mark_tri(GLuint p)
{
    VProg *e = vprog_get(p, 1);
    if (e) e->tri = 1;
}
static void vgl_use(GLuint p)
{
    g_vgl_prog = p;
    g_vgl_cur = vprog_get(p, 1);
    glUseProgram(p);
}
static void vgl_ub_flush(void)
{
    VProg *e = g_vgl_cur;
    int u;
    if (!e || e->prog != g_vgl_prog) return;
    if (!e->known) vprog_locs(e);
    for (u = 0; u < VUNI_N; u++) {
        const VUni *U = &g_vuni[u];
        if (e->loc[u] < 0 || e->gen[u] == g_vub_gen[U->block]) continue;
        glUniform4fv(e->loc[u], U->n ? U->n : 1, g_vub[U->block][U->row]);
        e->gen[u] = g_vub_gen[U->block];
    }
}

/* the full-screen triangle (gl_VertexID's corners in the desktop shader) */
static const float g_vgl_tri_xy[6] = { 0, 0, 2, 0, 0, 2 };
static void vgl_draw_arrays(GLenum m, GLint f, GLsizei c)
{
    vgl_ub_flush();
    if (g_vgl_cur && g_vgl_cur->tri) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, g_vgl_tri_xy);
    }
    glDrawArrays(m, f, c);
}
static void vgl_draw_elements(GLenum m, GLsizei c, GLenum t, const void *idx)
{
    vgl_trf("glDrawElements %d indices", (int)c);
    vgl_ub_flush();
    glDrawElements(m, c, t, idx);
}
/* u_ys = +1 draws into a surface: flipped when surfaces are */
static void vgl_uniform1f(GLint l, GLfloat v)
{
    if (g_vgl_flip && v == 1.0f && g_vgl_dfb && g_vgl_cur && g_vgl_cur->tri) v = -1.0f;
    glUniform1f(l, v);
}
/* constant-colour blend factors: vitaGL has no blend colour */
static GLenum vgl_bf(GLenum f)
{
    static int said;
    if (f >= 0x8001 && f <= 0x8004) {
        if (!said++) fprintf(stderr, "  [VGL] constant-colour blending is not available: drawn as ONE/ZERO\n");
        return (f == 0x8001 || f == 0x8003) ? GL_ONE : GL_ZERO;
    }
    return f;
}
static void vgl_blend_func(GLenum s, GLenum d, GLenum sa, GLenum da)
{
    glBlendFuncSeparate(vgl_bf(s), vgl_bf(d), vgl_bf(sa), vgl_bf(da));
}

/* ---- the Cg shaders ---- */

/* CPU-transformed vertices: window x*w, y*w, z*w, w in a_p (see the GLSL) */
static const char s_cg_vs_main[] =
"void main(float4 a_p, float4 a_d0, float4 a_d1, float4 a_fog, float4 a_t0, float4 a_t1, float4 a_t2, float4 a_t3,\n"
"  uniform float4 vp,\n"
"  float4 out gl_Position : POSITION, float4 out v_t0 : TEXCOORD0, float4 out v_t1 : TEXCOORD1,\n"
"  float4 out v_t2 : TEXCOORD2, float4 out v_t3 : TEXCOORD3, float4 out v_d0 : TEXCOORD4,\n"
"  float4 out v_d1 : TEXCOORD5, float4 out v_fog : TEXCOORD6) {\n"
"  gl_Position = float4(a_p.x * vp.x - a_p.w, (a_p.y * vp.y - a_p.w) * vp.w, 2.0 * a_p.z * vp.z - a_p.w, a_p.w);\n"
"  v_d0 = a_d0; v_d1 = a_d1; v_fog = a_fog; v_t0 = a_t0; v_t1 = a_t1; v_t2 = a_t2; v_t3 = a_t3;\n"
"}\n";

/* until a program's own fragment shader is compiled (or if it fails) */
static const char s_cg_ps_main[] =
"float4 main(float4 v_d0 : TEXCOORD4) : COLOR { return v_d0; }\n";

/* full-screen triangles; BLIT / CLEAR / PRESENT defined before */
static const char s_cg_vs_tri[] =
"void main(float2 a_p, uniform float4 clear_color, uniform float u_ys,\n"
"  float2 out v_uv : TEXCOORD0, float4 out gl_Position : POSITION) {\n"
"  gl_Position = float4(a_p.x * 2.0 - 1.0, (a_p.y * 2.0 - 1.0) * u_ys, 0.0, 1.0);\n"
"#ifdef BLIT\n"
"  v_uv = clear_color.zw + a_p * clear_color.xy;\n"
"#else\n"
"  v_uv = a_p * clear_color.xy;\n"
"#endif\n"
"}\n";
static const char s_cg_ps_tri[] =
"uniform sampler2D tx0;\n"
"uniform float4 clc_f;\n"
"uniform float4 post;\n"
"float4 main(float2 v_uv : TEXCOORD0) : COLOR {\n"
"#if defined(CLEAR)\n"
"  return clc_f;\n"
"#elif defined(PRESENT)\n"
"  float3 c = saturate(tex2D(tx0, v_uv).rgb);\n"
"  c = pow(c, float3(1.0 / post.x, 1.0 / post.x, 1.0 / post.x));\n"
"  float l = dot(c, float3(0.299, 0.587, 0.114));\n"
"  return float4(saturate(float3(l, l, l) + (c - float3(l, l, l)) * post.z), 1.0);\n"
"#else\n"
"  return tex2D(tx0, v_uv);\n"
"#endif\n"
"}\n";

/* the specialized combiner shaders' declarations */
static const char s_cg_ps_head[] =
"uniform sampler2D tx0; uniform sampler2D tx1; uniform sampler2D tx2; uniform sampler2D tx3;\n"
"uniform float4 c0_0, c0_1, c0_2, c0_3, c0_4, c0_5, c0_6, c0_7;\n"
"uniform float4 c1_0, c1_1, c1_2, c1_3, c1_4, c1_5, c1_6, c1_7;\n"
"uniform float4 ff0, ff1, fogc, fogp, ss0, ss1, ss2, ss3, atest;\n"
"float3 f3(float x) { return float3(x, x, x); }\n"
"float3 dnorm3(float4 c) { float3 b = floor(c.rgb * 255.0 + 0.5); return (b - 256.0 * step(float3(127.5, 127.5, 127.5), b)) / 127.0; }\n";

/* hwps_translate (nv2a_gl.inc) in Cg: the same program, written for sce Cg */
/* (vector arguments get vector constants: no scalar promotion in calls) */
static const char *cg_cmap_v(int m, const char *x, int vec, char *b, size_t n)
{
    const char *z = vec ? "f3(0.0)" : "0.0";
    switch (m & 7) {
    case 0: snprintf(b, n, "max(%s, %s)", x, z); break;
    case 1: snprintf(b, n, "(1.0 - saturate(%s))", x); break;
    case 2: snprintf(b, n, "(2.0 * max(%s, %s) - 1.0)", x, z); break;
    case 3: snprintf(b, n, "(-2.0 * max(%s, %s) + 1.0)", x, z); break;
    case 4: snprintf(b, n, "(max(%s, %s) - 0.5)", x, z); break;
    case 5: snprintf(b, n, "(-max(%s, %s) + 0.5)", x, z); break;
    case 6: snprintf(b, n, "(%s)", x); break;
    default: snprintf(b, n, "(-(%s))", x); break;
    }
    return b;
}
static const char *cg_cmap(int m, const char *x, char *b, size_t n) { return cg_cmap_v(m, x, 0, b, n); }
static const char *cg_crgb(int c, char *b, size_t n)
{
    char x[32], t[96];
    if ((c >> 4) & 1) { snprintf(x, sizeof x, "R%d.a", c & 15); snprintf(b, n, "f3(%s)", cg_cmap(c >> 8, x, t, sizeof t)); }
    else { snprintf(x, sizeof x, "R%d.rgb", c & 15); cg_cmap_v(c >> 8, x, 1, b, n); }
    return b;
}
static const char *cg_calp(int c, char *b, size_t n)
{
    char x[32];
    snprintf(x, sizeof x, "R%d.%c", c & 15, ((c >> 4) & 1) ? 'a' : 'b');
    return cg_cmap(c >> 8, x, b, n);
}
static const char *cg_cop_v(int op, const char *x, int vec, char *b, size_t n)
{
    const char *lo = vec ? "f3(-1.0)" : "-1.0", *hi = vec ? "f3(1.0)" : "1.0";
    switch (op) {
    case 1: snprintf(b, n, "clamp((%s) - 0.5, %s, %s)", x, lo, hi); break;
    case 2: snprintf(b, n, "clamp((%s) * 2.0, %s, %s)", x, lo, hi); break;
    case 3: snprintf(b, n, "clamp(((%s) - 0.5) * 2.0, %s, %s)", x, lo, hi); break;
    case 4: snprintf(b, n, "clamp((%s) * 4.0, %s, %s)", x, lo, hi); break;
    case 6: snprintf(b, n, "clamp((%s) * 0.5, %s, %s)", x, lo, hi); break;
    default: snprintf(b, n, "clamp(%s, %s, %s)", x, lo, hi); break;
    }
    return b;
}
static const char *cg_cop(int op, const char *x, char *b, size_t n) { return cg_cop_v(op, x, 0, b, n); }
static const char *cg_dnorm(int m, const char *c, char *b, size_t n)
{
    if (m == 0) snprintf(b, n, "%s.rgb", c);
    else if (m == 3) snprintf(b, n, "dnorm3(%s)", c);
    else snprintf(b, n, "(%s.rgb * 2.0 - 1.0)", c);
    return b;
}

/* Deletions wait for the GPU. vitaGL destroys a framebuffer's render target
 * at once, and scenes still queued on the GPU may be using it: the next
 * sceGxmBeginScene then failed (unchecked in vitaGL's scene_reset) and the
 * first clear or draw after it crashed. Seen right after start-up (the
 * self-test's framebuffer) and when the title made new targets -- timing
 * dependent: the slow traced build never crashed. Framebuffers, textures and
 * renderbuffers go into a queue and are deleted VGL_DEL_FRAMES presented
 * frames later; a full queue waits for the GPU (glFinish) and empties. */
#define VGL_DEL_FRAMES 3u
#define VDEL_N 1024
enum { VDEL_FBO, VDEL_TEX, VDEL_RB };
static struct { GLuint name; uint8_t kind; uint32_t frame; } g_vdel[VDEL_N];
static int g_vdel_n;
static uint32_t g_vgl_frame;
static unsigned g_vdel_total[3];   /* (framebuffers, textures, renderbuffers deleted: the memory report) */
static void vdel_now(int i)
{
    GLuint n = g_vdel[i].name;
    if (g_vdel[i].kind == VDEL_FBO) glDeleteFramebuffers(1, &n);
    else if (g_vdel[i].kind == VDEL_TEX) glDeleteTextures(1, &n);
    else glDeleteRenderbuffers(1, &n);
}
static void vgl_del_pending(int all)
{
    int i, j = 0;
    for (i = 0; i < g_vdel_n; i++) {
        if (all || g_vdel[i].frame + VGL_DEL_FRAMES <= g_vgl_frame) vdel_now(i);
        else g_vdel[j++] = g_vdel[i];
    }
    g_vdel_n = j;
}
static void vgl_defer(int kind, GLuint name)
{
    if (!name) return;
    if (g_vdel_n == VDEL_N) { glFinish(); vgl_del_pending(1); }
    g_vdel[g_vdel_n].name = name; g_vdel[g_vdel_n].kind = (uint8_t)kind; g_vdel[g_vdel_n].frame = g_vgl_frame;
    g_vdel_total[kind]++;
    g_vdel_n++;
}
/* after each presented frame (gpu_present) */
static void vgl_frame_done(void)
{
    g_vgl_frame++;
    vgl_del_pending(0);
}
static void vgl_del_fbs(GLsizei n, const GLuint *f)
{
    GLsizei i;
    for (i = 0; i < n; i++) {
        vfb_forget(f[i]);
        if (g_vgl_dfb == f[i]) g_vgl_dfb = 0;
        if (g_vgl_rfb == f[i]) g_vgl_rfb = 0;
        vgl_defer(VDEL_FBO, f[i]);
    }
}
static void vgl_del_texs(GLsizei n, const GLuint *t)
{
    GLsizei i;
    for (i = 0; i < n; i++) vgl_defer(VDEL_TEX, t[i]);
}
static void vgl_del_rbs(GLsizei n, const GLuint *r)
{
    GLsizei i;
    for (i = 0; i < n; i++) vgl_defer(VDEL_RB, r[i]);
}

/* From here on nv2a_gl.inc's GL calls go through the above (and the trace). */
#define glBindFramebuffer(t, f) (VGLT("glBindFramebuffer"), vgl_bind_fb(t, f))
#define glDeleteFramebuffers(n, f) (VGLT("glDeleteFramebuffers"), vgl_del_fbs(n, f))
#define glDeleteTextures(n, t) vgl_del_texs(n, t)
#define glDeleteRenderbuffers(n, r) vgl_del_rbs(n, r)
#define glScissor(x, y, w, h) vgl_scissor(x, y, w, h)
#define glViewport(x, y, w, h) vgl_viewport(x, y, w, h)
#define glBlitFramebuffer(a, b, c, d, e, f, g, h, m, fl) (VGLT("glBlitFramebuffer"), vgl_blit(a, b, c, d, e, f, g, h, m, fl))
#define glCreateShader(t) (VGLT("glCreateShader"), vgl_create_shader(t))
#define glShaderSource(s, n, src, len) vgl_shader_source(s, n, src, len)
#define glCompileShader(s) (VGLT("glCompileShader"), glCompileShader(s))
#define glLinkProgram(p) (VGLT("glLinkProgram"), vgl_link(p))
#define glAttachShader(p, s) (VGLT("glAttachShader"), vgl_attach(p, s))
#define glUseProgram(p) (VGLT("glUseProgram"), vgl_use(p))
#define glDrawArrays(m, f, c) (VGLT("glDrawArrays"), vgl_draw_arrays(m, f, c))
#define glUniform1f(l, v) vgl_uniform1f(l, v)
#define glBlendFuncSeparate(a, b, c, d) (VGLT("glBlendFuncSeparate"), vgl_blend_func(a, b, c, d))
#define glBlendColor(r, g, b, a) ((void)0)
#define glDrawBuffer(b) ((void)0)
#define glRenderbufferStorageMultisample(t, n, f, w, h) ((void)0)
#define glUniformBlockBinding(p, i, b) ((void)0)
#define VGLT_WRAP(fn) (VGLT(#fn), fn)
#define glClear(m) (VGLT("glClear"), glClear(m))
#define glStencilMask(m) (VGLT("glStencilMask"), glStencilMask(m))
#define glStencilFuncSeparate(a, b, c, d) (VGLT("glStencilFuncSeparate"), glStencilFuncSeparate(a, b, c, d))
#define glStencilOpSeparate(a, b, c, d) (VGLT("glStencilOpSeparate"), glStencilOpSeparate(a, b, c, d))
#define glDepthMask(m) (VGLT("glDepthMask"), glDepthMask(m))
#define glDepthFunc(m) (VGLT("glDepthFunc"), glDepthFunc(m))
#define glColorMask(a, b, c, d) (VGLT("glColorMask"), glColorMask(a, b, c, d))
#define glEnable(c) (VGLT("glEnable"), glEnable(c))
#define glDisable(c) (VGLT("glDisable"), glDisable(c))
#define glTexImage2D(a, b, c, d, e, f, g, h, i) (VGLT("glTexImage2D"), glTexImage2D(a, b, c, d, e, f, g, h, i))
#define glTexSubImage2D(a, b, c, d, e, f, g, h, i) (VGLT("glTexSubImage2D"), glTexSubImage2D(a, b, c, d, e, f, g, h, i))
#define glFramebufferTexture2D(a, b, c, d, e) (VGLT("glFramebufferTexture2D"), glFramebufferTexture2D(a, b, c, d, e))
#define glFramebufferRenderbuffer(a, b, c, d) (VGLT("glFramebufferRenderbuffer"), glFramebufferRenderbuffer(a, b, c, d))
#define glRenderbufferStorage(a, b, c, d) (VGLT("glRenderbufferStorage"), glRenderbufferStorage(a, b, c, d))
#define glCheckFramebufferStatus(t) (VGLT("glCheckFramebufferStatus"), glCheckFramebufferStatus(t))
#define glGenerateMipmap(t) (VGLT("glGenerateMipmap"), glGenerateMipmap(t))
#define glBindTexture(t, x) (VGLT("glBindTexture"), glBindTexture(t, x))
#define glBindSampler(u, x) (VGLT("glBindSampler"), glBindSampler(u, x))
#define glBindVertexArray(v) (VGLT("glBindVertexArray"), glBindVertexArray(v))
#define glPixelStorei(a, b) (VGLT("glPixelStorei"), glPixelStorei(a, b))
#define glFinish() (VGLT("glFinish"), glFinish())
#define glClearDepth(z) (VGLT("glClearDepth"), glClearDepth(z))
#define glClearStencil(z) (VGLT("glClearStencil"), glClearStencil(z))
#define vglSwapBuffers(x) (VGLT("vglSwapBuffers"), vglSwapBuffers(x))

static void (*p_glMaxShaderCompilerThreadsKHR)(GLuint);   /* (desktop only: KHR_parallel_shader_compile) */
/* (nv2a_gl.inc, after its texture cache) */
static void vgl_mem_report(const char *why);
static void vgl_make_room(uint64_t need);
static unsigned g_vp_draws, g_vp_verts, g_vp_up, g_vp_upkb, g_vp_hw;   /* ([VGLPERF] counters: nv2a_gl.inc) */
