/*
 * nv2a_gl.inc -- OpenGL 3.3 backend for the pushbuffer executor (POSIX hosts).
 *
 * The same design as the Direct3D 11 backend (nv2a_d3d11.inc), call for call,
 * so the two draw the same picture: decoded state from nv2a_sw3d.inc, vertex
 * programs on the CPU or as generated shaders, register combiners interpreted
 * by a general fragment shader until a specialized one for the program is
 * ready, every colour and zeta surface the title draws into kept as a GL
 * texture found again by its guest address.
 *
 * Conventions that keep it identical to D3D:
 *   - Images are stored with their first row (the guest's top) at texel row
 *     0, and offscreen drawing puts window row y at framebuffer row y (the
 *     vertex shaders map window y to clip y without D3D's flip). Viewports,
 *     scissors, copies and read-backs then use the D3D numbers unchanged;
 *     only the final picture is flipped onto the window.
 *   - With that, a triangle D3D calls clockwise has positive area in GL's
 *     window coordinates: D3D "front = clockwise" is glFrontFace(GL_CCW).
 *   - Clip z: D3D keeps 0..w, GL -w..w: the shaders emit 2z - w, so depth is
 *     the same z/w.
 *   - The NV2A's blend, compare and stencil enums are OpenGL's own.
 *
 * The context is made by the window thread (fb_present.c) and made current
 * here, on the executor's thread, which is the only one that calls GL.
 */
#include <SDL.h>
#if defined(__vita__)
#include "nv2a_gl_vita.inc"
#include <arm_neon.h>   /* (v159: the index range) */
#include <psp2/kernel/clib.h>   /* (v216: the vertex cache heap) */
#else
#include <SDL_opengl.h>
#endif
#include "../video/gfx_menu.h"

extern void *xbox_FramebufferHwnd(void);
extern void *xbox_FramebufferGLContext(int ms);
extern void xbox_FramebufferSetGpuOwned(int on);

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* ---------------- GL entry points (through SDL, no link-time libGL) ------ */

#define GLFUNCS(X) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, ClearDepth, (GLdouble)) \
    X(void, ClearStencil, (GLint)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, StencilMask, (GLuint)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, StencilFuncSeparate, (GLenum, GLenum, GLint, GLuint)) \
    X(void, StencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendEquationSeparate, (GLenum, GLenum)) \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, CullFace, (GLenum)) \
    X(void, FrontFace, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(const GLubyte *, GetStringi, (GLenum, GLuint)) \
    X(void, Finish, (void)) \
    X(void, Flush, (void)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, ReadBuffer, (GLenum)) \
    X(void, DrawBuffer, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, CompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, GenSamplers, (GLsizei, GLuint *)) \
    X(void, DeleteSamplers, (GLsizei, const GLuint *)) \
    X(void, BindSampler, (GLuint, GLuint)) \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
    X(void, SamplerParameterf, (GLuint, GLenum, GLfloat)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void *, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
    X(GLboolean, UnmapBuffer, (GLenum)) \
    X(void, BindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLuint, GetUniformBlockIndex, (GLuint, const GLchar *)) \
    X(void, UniformBlockBinding, (GLuint, GLuint, GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))

#if !defined(__vita__)   /* (vitaGL: linked directly, nv2a_gl_vita.inc) */
#define GLX_DECL(ret, name, args) static ret (APIENTRY *p_gl##name) args;
GLFUNCS(GLX_DECL)
#undef GLX_DECL
static void (APIENTRY *p_glMaxShaderCompilerThreadsKHR)(GLuint);

#define glViewport p_glViewport
#define glScissor p_glScissor
#define glEnable p_glEnable
#define glDisable p_glDisable
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glClearDepth p_glClearDepth
#define glClearStencil p_glClearStencil
#define glColorMask p_glColorMask
#define glDepthMask p_glDepthMask
#define glStencilMask p_glStencilMask
#define glDepthFunc p_glDepthFunc
#define glStencilFuncSeparate p_glStencilFuncSeparate
#define glStencilOpSeparate p_glStencilOpSeparate
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glBlendEquationSeparate p_glBlendEquationSeparate
#define glBlendColor p_glBlendColor
#define glCullFace p_glCullFace
#define glFrontFace p_glFrontFace
#define glGetError p_glGetError
#define glGetIntegerv p_glGetIntegerv
#define glGetFloatv p_glGetFloatv
#define glGetString p_glGetString
#define glGetStringi p_glGetStringi
#define glFinish p_glFinish
#define glFlush p_glFlush
#define glPixelStorei p_glPixelStorei
#define glReadPixels p_glReadPixels
#define glReadBuffer p_glReadBuffer
#define glDrawBuffer p_glDrawBuffer
#define glGenTextures p_glGenTextures
#define glDeleteTextures p_glDeleteTextures
#define glBindTexture p_glBindTexture
#define glTexImage2D p_glTexImage2D
#define glTexSubImage2D p_glTexSubImage2D
#define glCompressedTexImage2D p_glCompressedTexImage2D
#define glTexParameteri p_glTexParameteri
#define glActiveTexture p_glActiveTexture
#define glGenerateMipmap p_glGenerateMipmap
#define glGenSamplers p_glGenSamplers
#define glDeleteSamplers p_glDeleteSamplers
#define glBindSampler p_glBindSampler
#define glSamplerParameteri p_glSamplerParameteri
#define glSamplerParameterf p_glSamplerParameterf
#define glGenFramebuffers p_glGenFramebuffers
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glBlitFramebuffer p_glBlitFramebuffer
#define glGenBuffers p_glGenBuffers
#define glDeleteBuffers p_glDeleteBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glMapBufferRange p_glMapBufferRange
#define glUnmapBuffer p_glUnmapBuffer
#define glBindBufferRange p_glBindBufferRange
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glDeleteShader p_glDeleteShader
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glDeleteProgram p_glDeleteProgram
#define glUseProgram p_glUseProgram
#define glGetUniformBlockIndex p_glGetUniformBlockIndex
#define glUniformBlockBinding p_glUniformBlockBinding
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform1f p_glUniform1f
#define glDrawArrays p_glDrawArrays
#define glDrawElementsBaseVertex p_glDrawElementsBaseVertex
#define glGenRenderbuffers p_glGenRenderbuffers
#define glDeleteRenderbuffers p_glDeleteRenderbuffers
#define glBindRenderbuffer p_glBindRenderbuffer
#define glRenderbufferStorageMultisample p_glRenderbufferStorageMultisample
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#endif /* !__vita__ */

static int gl_load(void)
{
#if !defined(__vita__)
#define GLX_LOAD(ret, name, args) \
    if (!(*(void **)&p_gl##name = SDL_GL_GetProcAddress("gl" #name))) { \
        fprintf(stderr, "  [GL] the driver lacks gl" #name "\n"); return 0; }
    GLFUNCS(GLX_LOAD)
#undef GLX_LOAD
#endif
    return 1;
}

static int gl_has_ext(const char *name)
{
    GLint n = 0, i;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return 1;
    }
    return 0;
}

/* ---------------- shared state ---------------- */

volatile uint64_t g_ws_cam_tick;
static int s_gpu_mode = -1;             /* -1 undecided, 0 software, 1 OpenGL */
static SDL_Window *g_win;
static SDL_GLContext g_glc;
static int g_sc_w, g_sc_h;              /* window drawable size */

/* Internal resolution and picture shape: as in nv2a_d3d11.inc. */
static int g_rs = 1;
static float g_rsf = 1.0f;   /* the scale itself (4.5: 2160 lines) */
extern float xbox_GfxScaleF(void);
static float g_rsx = 1.0f, g_rsy = 1.0f;
static float g_aspect = 4.0f / 3.0f;
/* (1.9) the frame-sized targets within the largest texture the card takes
 * (16384 wide): 12x at 32:9 would be 20480 -- both scales come down */
static int gl_max_tex(void)
{
    static GLint m;
    if (!m) { glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m); if (m < 2048) m = 16384; }
    return (int)m;
}
#if defined(__vita__)
/* (v146) The internal resolution on the Vita: in the open levels the GPU is
 * the limit (the render thread waits inside vitaGL's draws, the game waits
 * for the frame to be done -- a third of its time), and every 640x480 target
 * drawn at 0.75 is 480x360: 44% fewer pixels. Shown stretched as before.
 * TT_RES=0.5..1 (1: the full 640x480). */
static float vita_res_scale(void)
{
    static float f = -1.0f;
    if (f < 0.0f) {
        const char *e = getenv("TT_RES");
        f = e ? (float)atof(e) : 0.85f;   /* (v147: 0.85 -- 544x408; v146 had 0.75) */
        if (!e) { extern float xbox_GfxAspect(void); if (xbox_GfxAspect() > 1.34f) f = 0.75f; }   /* (v203: 16:9 at about the pixel count of 544x408; v204: 0.75 -- 640x360, whole numbers: at 0.74 (631.5 x 355.2) the last row and column showed stray dots) */
        if (!(f >= 0.5f)) f = 0.5f;
        if (f > 1.0f) f = 1.0f;
        { extern float xbox_GfxAspect(void); fprintf(stderr, "  [GL] internal resolution %.0f x %.0f (TT_RES %.2f)\n", 640.0f * f * (xbox_GfxAspect() > 1.34f ? xbox_GfxAspect() / (4.0f / 3.0f) : 1.0f), 480.0f * f, f); }
    }
    {   /* (v161) the dynamic resolution's step down, when it has taken one (vgl_dynres) */
        extern float g_vgl_dynres;
        if (g_vgl_dynres > 0.0f && g_vgl_dynres < f) return g_vgl_dynres;
    }
    return f;
}
#endif
static void rs_fit(void)
{
    float mx = (float)gl_max_tex(), k = 1.0f;
    if (640.0f * g_rsx > mx) k = mx / (640.0f * g_rsx);
    if (480.0f * g_rsy * k > mx) k = mx / (480.0f * g_rsy);
    if (k < 1.0f) {
        g_rsx *= k; g_rsy *= k;
        fprintf(stderr, "  [GPU] internal resolution held to %.0f x %.0f (the largest texture: %.0f)\n", 640.0f * g_rsx, 480.0f * g_rsy, mx);
        fflush(stderr);
    }
}
static int g_frame_3d, g_wide_prev;
static unsigned g_present_n;
#define GS_SCALED(w, h) ((w) >= 320 && (h) >= 200)
#define RSX_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsx : 1.0f) + 0.5f))
#define RSY_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsy : 1.0f) + 0.5f))
#define SX(s, v) ((uint32_t)((float)(v) * (float)(s)->pw / (float)(s)->w + 0.5f))
#define SY(s, v) ((uint32_t)((float)(v) * (float)(s)->ph / (float)(s)->h + 0.5f))

static int g_aniso = -1, g_mips = -1;
static float g_max_aniso = 1.0f;
static long g_gfx_seen = 1;
static int g_aa_on = -1, g_vsync_on = -1, g_stretch_on = -1, g_hwvp_user = 1;
/* Menus fill the screen: a frame that drew no 3D (menus, the upgrade and
 * results screens, loading screens) is drawn inside the TV safe area, the
 * full picture scaled by 593/640 about its middle with black round it; it
 * is shown that much larger, so the art reaches the screen's edges and the
 * margin falls outside. Movies and play are left as they are.
 * RECOMP_MENU_FILL=0: as before. */
static float menu_fill_zoom(int menu2d)
{
    /* (the F10 menu's "Menus"; [Display] menu_fill, RECOMP_MENU_FILL) */
    return (xbox_GfxGet(GFX_MENUFILL) == 0 && menu2d) ? 640.0f / 593.0f : 1.0f;
}

/* Movies in widescreen (1.7.8): with a wide picture chosen ([Display]
 * aspect wider than 4:3), a movie fills the window's width at its own shape,
 * the top and bottom cut -- the game's films are letterboxed inside 4:3
 * (about 1.85:1), so at 16:9 the picture fills the screen with nothing of it
 * lost and nothing stretched. Off: the films looked zoomed in; as before
 * (4:3, or stretched with "Stretch to fill") unless RECOMP_MOVIE_FILL=1. */
static int movie_fill(int movie, float ww, float wh, float *pw, float *ph)
{
    /* (the F10 menu's "Movies in widescreen", off by default: the films look
     * a little close; [Display] movie_fill, RECOMP_MOVIE_FILL) */
    if (xbox_GfxGet(GFX_MOVIEFILL) != 1 || !movie || g_aspect <= 1.34f || ww / wh <= 4.0f / 3.0f + 0.01f) return 0;
    *pw = ww; *ph = ww * 0.75f;
    return 1;
}

static int g_vsync_applied = -2;
static void gpu_filter_opts(void)
{
    if (g_aniso < 0) {
        const char *a = getenv("RECOMP_ANISO"), *m = getenv("RECOMP_MIPMAPS");
        g_aniso = a ? atoi(a) : 1;
        if (g_aniso < 1) g_aniso = 1;
        if (g_aniso > 16) g_aniso = 16;
#if defined(__vita__)
        g_mips = !(m && m[0] == '0');   /* (v198: on -- without them every far surface read its texture at full size, and the GPU was pixel-bound at 544x408; RECOMP_MIPMAPS=0 off) */
#else
        g_mips = !(m && m[0] == '0');
#endif
        fprintf(stderr, "  [GL] texture filtering: mipmaps %s, anisotropic x%d\n", g_mips ? "on" : "off", g_aniso);
    }
}

static float g_post[4] = { 1, 0, 1, 0 };   /* brightness (gamma), sharpening, saturation */

/* Uniform blocks: every program uses the same binding points. */
enum { UB_VSC = 0, UB_PSC = 1, UB_CLC = 2, UB_POST = 3, UB_VPC = 4 };

/* ---------------- streaming buffers ---------------- */

/* One ring per use: appended to without synchronisation, given a fresh
 * store when it wraps -- the GL counterpart of D3D's NO_OVERWRITE/DISCARD
 * scheme in nv2a_d3d11.inc. Returns the byte offset, or 0xFFFFFFFF. */
typedef struct { GLuint buf; GLenum target; uint32_t cap, pos; } GRing;
static GRing g_rvb = { 0, GL_ARRAY_BUFFER, 0, 0 }, g_rvbx = { 0, GL_ARRAY_BUFFER, 0, 0 };
static GRing g_rib = { 0, GL_ELEMENT_ARRAY_BUFFER, 0, 0 }, g_rub = { 0, GL_UNIFORM_BUFFER, 0, 0 };
static GLint g_ubo_align = 256;
static uint32_t gpu_ring(GRing *r, const void *data, uint32_t bytes, uint32_t align)
{
    uint32_t at;
    void *p;
    if (!r->buf || r->cap < bytes) {
        if (!r->buf) glGenBuffers(1, &r->buf);
        r->cap = bytes < (8u << 20) ? (8u << 20) : bytes * 2;
        glBindBuffer(r->target, r->buf);
        glBufferData(r->target, r->cap, NULL, GL_STREAM_DRAW);
        r->pos = r->cap;
    } else glBindBuffer(r->target, r->buf);
    if (!align) align = 1;
    at = (r->pos + align - 1) / align * align;
    if (at + bytes > r->cap) {
        glBufferData(r->target, r->cap, NULL, GL_STREAM_DRAW);   /* a fresh store */
        at = 0;
    }
    p = glMapBufferRange(r->target, at, bytes, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    if (!p) return 0xFFFFFFFFu;
    memcpy(p, data, bytes);
    glUnmapBuffer(r->target);
    r->pos = at + bytes;
    return at;
}

/* a uniform block's data, bound at its binding point */
static void gpu_ub(int binding, const void *data, uint32_t bytes)
{
#if defined(__vita__)
    vgl_ub_set(binding, data, bytes);
    return;
#endif
    uint32_t at = gpu_ring(&g_rub, data, bytes, (uint32_t)g_ubo_align);
    if (at != 0xFFFFFFFFu) glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)binding, g_rub.buf, at, bytes);
}

/* ---------------- surfaces ---------------- */

typedef struct {
    uint32_t base, w, h, is_depth;
    uint32_t pw, ph;                    /* allocated size: w, h times the scale */
    GLuint tex;
    uint32_t mem_sum;
    uint32_t pitch;
    uint64_t used;
    uint32_t synced_run;
    uint64_t wr;
    /* (1.9) MSAA: drawn into a multisampled renderbuffer (mrb), made into
     * the texture before anything reads it (ms_new); the texture written
     * another way is drawn into the renderbuffer first (ss_new) */
    GLuint mrb;
    uint8_t ms_new, ss_new;
} GSurf;
static int g_msaa = 1, g_msaa_bad;
static uint64_t g_wr_clock;
extern volatile uint32_t g_pb_runs;
#define GSURF_MAX 48
static GSurf g_surf[GSURF_MAX];
static uint64_t g_clock;

/* State shadow (what is bound), so a draw only sends what changed. Anything
 * that binds behind the draw path's back calls gpu_sh_reset. */
static struct {
    GLuint fbo; int tg;
    int vw, vh; int vp_ok;
    int sc[4]; int sc_ok;
    uint64_t blend; float bf[4]; int bs_ok;
    uint64_t ds[3]; int ds_ok;
    uint64_t rs; int rs_ok;
    GLuint tex[4], smp[4]; int tex_ok;
    float cbvs[4]; int cbvs_ok;
    unsigned char cbps[1152]; int cbps_ok;
    GLuint prog;
    int vao;
} g_sh;
static void gpu_sh_reset(void) { memset(&g_sh, 0, sizeof g_sh); }

#if defined(__vita__)
static void zrec_bound(GLuint fbo);
#endif
static void gpu_set_fbo(GLuint fbo)
{
    if (!g_sh.tg || g_sh.fbo != fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); g_sh.fbo = fbo; g_sh.tg = 1;
#if defined(__vita__)
        if (g_vgl_flip) g_sh.sc_ok = 0;   /* (a flipped scissor depends on the target's height) */
        zrec_bound(fbo);
#endif
    }
}

/* Framebuffer objects, one per (colour, depth) texture pair. */
#define FBO_MAX 64
static struct { GLuint col, dep, fbo; uint64_t used; uint32_t zseen, sseen; } g_fbo[FBO_MAX];
#if defined(__vita__)
/* (PS Vita) vitaGL's renderbuffers hold no storage: every framebuffer makes
 * its own depth buffer, so a zeta surface shared by two colour targets (the
 * game's two screen buffers) is two depth buffers here. A clear made on one
 * is carried to the others when they are next bound (gpu_set_fbo): each
 * zeta keeps its last depth and stencil clears with a generation, each
 * framebuffer the generations it has seen. */
static struct { GLuint dep; uint32_t zgen, sgen; float z; int st; } g_zrec[16];
static int zrec_find(GLuint dep)
{
    int i, v = -1;
    for (i = 0; i < 16; i++) if (g_zrec[i].dep == dep) return i;
    for (i = 0; i < 16; i++) if (!g_zrec[i].dep) { v = i; break; }
    if (v < 0) v = (int)(dep % 16);
    memset(&g_zrec[v], 0, sizeof g_zrec[v]);
    g_zrec[v].dep = dep; g_zrec[v].zgen = g_zrec[v].sgen = 1;
    return v;
}
static int fbo_find(GLuint fbo)
{
    int i;
    for (i = 0; i < FBO_MAX; i++) if (g_fbo[i].fbo == fbo && fbo) return i;
    return -1;
}
#if defined(__vita__)
/* (v65 diagnostics) the flight recorder: the last 4 frames' draws, clears and
 * depth clears in a small ring; a presented frame much darker than the ones
 * before it (the world gone for a frame, the "black flashes") writes them out
 * to ux0:data/twotowers/cap/flash_N.txt with the frame, up to 3 times */
volatile const char *g_vgl_where = "starting";   /* (v66: for the hang report; v71: the render thread's sampled zones, prof_vita.c) */
const char g_vw_method[] = "method", g_vw_block[] = "method block";
volatile uint32_t g_rt_meth;
#ifndef VWHERE
#define VWHERE(x) (g_vgl_where = (x))
#endif
typedef struct { uint32_t kind, a, b, c, d, e; } FrRec;
#define FR_N 2560
static FrRec g_fr[4][FR_N];
static uint32_t g_fr_n[4], g_fr_cur;
static float g_fr_mean[4];
static void fr_add(uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t i = g_fr_n[g_fr_cur];
    if (i >= FR_N) return;
    g_fr[g_fr_cur][i].kind = kind; g_fr[g_fr_cur][i].a = a; g_fr[g_fr_cur][i].b = b;
    g_fr[g_fr_cur][i].c = c; g_fr[g_fr_cur][i].d = d; g_fr[g_fr_cur][i].e = e;
    g_fr_n[g_fr_cur] = i + 1;
}
static GLint g_fr_vp[4];
#endif
static unsigned g_zprop_n;
static void vgl_zclear_quad(int which, float z, int stencil);
static void vgl_zc_viewport(GLuint fbo);
static void gpu_sh_reset(void);
static void zrec_cleared(GLuint fbo, int which, float z, int st)
{
    int f = fbo_find(fbo), r;
    if (f < 0 || !g_fbo[f].dep) return;
    r = zrec_find(g_fbo[f].dep);
    if (which & 1) { g_zrec[r].zgen++; g_zrec[r].z = z; g_fbo[f].zseen = g_zrec[r].zgen; }
    if (which & 2) { g_zrec[r].sgen++; g_zrec[r].st = st; g_fbo[f].sseen = g_zrec[r].sgen; }
}
static void zrec_bound(GLuint fbo)
{
    int f = fbo_find(fbo), r, which = 0;
    if (f < 0 || !g_fbo[f].dep) return;
    r = zrec_find(g_fbo[f].dep);
    if (g_fbo[f].zseen != g_zrec[r].zgen) which |= 1;
    if (g_fbo[f].sseen != g_zrec[r].sgen) which |= 2;
    g_fbo[f].zseen = g_zrec[r].zgen; g_fbo[f].sseen = g_zrec[r].sgen;
    if (!which || (g_zrec[r].zgen == 1 && g_zrec[r].sgen == 1)) return;   /* (never cleared yet) */
    fr_add(4, fbo, (uint32_t)which, (uint32_t)(g_zrec[r].z * 1e6f), (uint32_t)g_zrec[r].st, 0);
    if (g_zprop_n++ < 5) fprintf(stderr, "  [VGL] zeta clear carried to framebuffer %u (depth %d stencil %d)\n", fbo, which & 1, (which >> 1) & 1);
    glDisable(GL_SCISSOR_TEST);
    vgl_zc_viewport(fbo);
    vgl_zclear_quad(which, g_zrec[r].z, g_zrec[r].st);
    gpu_sh_reset();
    g_sh.fbo = fbo; g_sh.tg = 1;
}
#endif
static GLuint gpu_fbo(GLuint col, GLuint dep)
{
    int i, v = -1;
    for (i = 0; i < FBO_MAX; i++)
        if (g_fbo[i].fbo && g_fbo[i].col == col && g_fbo[i].dep == dep) { g_fbo[i].used = ++g_clock; return g_fbo[i].fbo; }
    for (i = 0; i < FBO_MAX; i++) {
        if (!g_fbo[i].fbo) { v = i; break; }
        if (v < 0 || g_fbo[i].used < g_fbo[v].used) v = i;
    }
#if defined(__vita__)
    /* (a fresh object for every pair: vitaGL's attachments are not swapped
     * on a framebuffer in use, and none is attached as 0) */
    if (g_fbo[v].fbo) { glDeleteFramebuffers(1, &g_fbo[v].fbo); g_fbo[v].fbo = 0; }
#endif
    if (!g_fbo[v].fbo) glGenFramebuffers(1, &g_fbo[v].fbo);
    g_fbo[v].zseen = g_fbo[v].sseen = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo[v].fbo);
#if defined(__vita__)
    if (col) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    if (dep) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dep);
    vfb_note(g_fbo[v].fbo, col ? vtx_h(col) : vtx_h(dep | 0x80000000u));
#else
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
#endif
    /* a zeta-only framebuffer has no colour to draw to (GL 3.3 calls it
     * incomplete otherwise) */
    glDrawBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
#if !defined(__vita__)
    glReadBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
#endif
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        static int said;
        if (st != GL_FRAMEBUFFER_COMPLETE && said++ < 5)
            fprintf(stderr, "  [GL] framebuffer incomplete (0x%04X), colour %u depth %u\n", st, col, dep);
    }
    g_sh.tg = 0;
    g_fbo[v].col = col; g_fbo[v].dep = dep; g_fbo[v].used = ++g_clock;
    return g_fbo[v].fbo;
}
/* a texture is going away: its framebuffers too */
static void gpu_fbo_forget(GLuint tex)
{
    int i;
    for (i = 0; i < FBO_MAX; i++)
        if (g_fbo[i].fbo && (g_fbo[i].col == tex || g_fbo[i].dep == tex)) {
            glDeleteFramebuffers(1, &g_fbo[i].fbo);
            memset(&g_fbo[i], 0, sizeof g_fbo[i]);
        }
    g_sh.tg = 0;
}

/* (1.9) framebuffers of multisampled renderbuffers */
static struct { GLuint col, dep, fbo; uint64_t used; } g_fboms[16];
static GLuint gpu_fbo_ms(GLuint col, GLuint dep)
{
    int i, v = -1;
    for (i = 0; i < 16; i++)
        if (g_fboms[i].fbo && g_fboms[i].col == col && g_fboms[i].dep == dep) { g_fboms[i].used = ++g_clock; return g_fboms[i].fbo; }
    for (i = 0; i < 16; i++) {
        if (!g_fboms[i].fbo) { v = i; break; }
        if (v < 0 || g_fboms[i].used < g_fboms[v].used) v = i;
    }
    if (!g_fboms[v].fbo) glGenFramebuffers(1, &g_fboms[v].fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fboms[v].fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, col);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dep);
    glDrawBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    glReadBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        static int said;
        if (st != GL_FRAMEBUFFER_COMPLETE && said++ < 5)
            fprintf(stderr, "  [GL] multisampled framebuffer incomplete (0x%04X)\n", st);
    }
    g_sh.tg = 0;
    g_fboms[v].col = col; g_fboms[v].dep = dep; g_fboms[v].used = ++g_clock;
    return g_fboms[v].fbo;
}
static void gpu_fbo_ms_forget(GLuint rb)
{
    int i;
    for (i = 0; i < 16; i++)
        if (g_fboms[i].fbo && (g_fboms[i].col == rb || g_fboms[i].dep == rb)) {
            glDeleteFramebuffers(1, &g_fboms[i].fbo);
            memset(&g_fboms[i], 0, sizeof g_fboms[i]);
        }
    g_sh.tg = 0;
}

static uint32_t gpu_mem_sum(uint32_t base, uint32_t bytes)
{
    const uint8_t *m = (const uint8_t *)XR_PTR(base);
    uint32_t h = 2166136261u, i, step = bytes / 1024;
    if (step < 4) step = 4;
    for (i = 0; i + 4 <= bytes && i < 1024 * step; i += step)
        h = (h ^ *(const uint32_t *)(m + (i & ~3u))) * 16777619u;
    return h;
}

/* A texture of size w x h: colour (BGRA8) or depth-stencil. */
static GLuint gpu_tex2d(uint32_t w, uint32_t h, int depth)
{
    GLuint t = 0;
#if defined(__vita__)
    /* vitaGL: a colour texture, or a depth-stencil renderbuffer (only colour
     * textures attach to its framebuffers) */
    if (depth) {
        glGenRenderbuffers(1, &t);
        glBindRenderbuffer(GL_RENDERBUFFER, t);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)w, (GLsizei)h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        vtx_note(t | 0x80000000u, h);
        return t;
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    vtx_note(t, h);
    g_sh.tex_ok = 0;
    return t;
#endif
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (depth)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    g_sh.tex_ok = 0;
    return t;
}

/* Scratch 1x texture for scaled uploads/read-backs. */
static GLuint g_one_tex; static uint32_t g_one_w, g_one_h;
static GLuint g_present_smp, g_copy_smp;
static GLuint g_vao, g_vao_x, g_vao_empty;
static GLuint g_tex_black;                 /* what a stage reads from a target being drawn */

/* the fixed shaders' programs (gpu_init) */
static GLuint g_pr_copy, g_pr_present, g_pr_fxaa, g_pr_blit, g_pr_clear;
static GLint g_loc_ys_copy, g_loc_ys_present, g_loc_ys_fxaa, g_loc_ys_blit;

static int gpu_one(uint32_t w, uint32_t h)
{
    if (g_one_tex && g_one_w >= w && g_one_h >= h) return 1;
    if (g_one_tex) { gpu_fbo_forget(g_one_tex); glDeleteTextures(1, &g_one_tex); g_one_tex = 0; }
    if (w < g_one_w) w = g_one_w;
    if (h < g_one_h) h = g_one_h;
    g_one_tex = gpu_tex2d(w, h, 0);
    g_one_w = w; g_one_h = h;
    return g_one_tex != 0;
}

/* Draw texture src (its top-left uw x uh of a sw x sh texture) onto the
 * top-left dw x dh of framebuffer fbo, alpha included. */
static void gpu_copy_scaled(GLuint src, uint32_t uw, uint32_t uh, uint32_t sw, uint32_t sh,
                            GLuint fbo, uint32_t dw, uint32_t dh)
{
    float uvs[4];
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLsizei)dw, (GLsizei)dh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    uvs[0] = (float)uw / (float)sw; uvs[1] = (float)uh / (float)sh; uvs[2] = uvs[3] = 0;
    gpu_ub(UB_CLC, uvs, 16);
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glBindSampler(0, g_copy_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    gpu_sh_reset();
}

static unsigned g_gsurf_gen = 1;   /* (v74: a surface made, changed or let go -- gpu_bind_targets' last answer is old) */
static void gsurf_free(GSurf *s)
{
    g_gsurf_gen++;
    if (s->mrb) { gpu_fbo_ms_forget(s->mrb); glDeleteRenderbuffers(1, &s->mrb); }
#if defined(__vita__)
    if (s->tex) { gpu_fbo_forget(s->tex); if (s->is_depth) glDeleteRenderbuffers(1, &s->tex); else glDeleteTextures(1, &s->tex); }
#else
    if (s->tex) { gpu_fbo_forget(s->tex); glDeleteTextures(1, &s->tex); }
#endif
    memset(s, 0, sizeof *s);
    g_sh.tex_ok = 0;
}

/* Guest pixels (w x h at pitch) into a colour surface, scaled if need be. */
static void gsurf_upload(GSurf *s, const uint8_t *m, uint32_t pitch)
{
    if (s->mrb) { s->ss_new = 1; s->ms_new = 0; }   /* (the whole surface: what was drawn is replaced) */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 4));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (s->pw == s->w && s->ph == s->h) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        g_sh.tex_ok = 0;
        return;
    }
    if (!gpu_one(s->w, s->h)) { glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); return; }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_one_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gpu_copy_scaled(g_one_tex, s->w, s->h, g_one_w, g_one_h, gpu_fbo(s->tex, 0), s->pw, s->ph);
}

static GSurf *gsurf_find(uint32_t base, int depth)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == base && (depth < 0 || (int)g_surf[i].is_depth == depth))
            return &g_surf[i];
    return NULL;
}

static int gsurf_near(uint32_t off)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && off >= g_surf[i].base && off < g_surf[i].base + g_surf[i].w * g_surf[i].h * 4)
            return 1;
    return 0;
}

/* Depth and stencil of a whole zeta texture to a value: as D3D's
 * ClearDepthStencilView -- no scissor, every bit written. */
#if defined(__vita__)
/* (PS Vita) depth cleared by drawing: a full-screen quad at the depth asked
 * for, depth test ALWAYS, colour writes off (stencil: REPLACE). vitaGL's
 * glClear left the depth so that the level's GEQUAL draws all failed (the
 * v31 captures: with the depth test off, the world is there). */
static GLuint g_zc_prog;
static int g_zc_mode = -1;   /* 1 quad (default), 0 vitaGL's glClear: RECOMP_VGL_ZCLEAR */
static void vgl_zclear_prog(void)
{
    static const char vs_src[] = "void main(float4 vin0, float4 out gl_Position : POSITION) { gl_Position = vin0; }\n";
    static const char fs_src[] = "float4 main() : COLOR { return float4(0.0, 0.0, 0.0, 0.0); }\n";
    GLuint vs, fs;
    const char *a = vs_src, *b = fs_src;
    if (g_zc_prog) return;
    vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &a, NULL); glCompileShader(vs);
    fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &b, NULL); glCompileShader(fs);
    g_zc_prog = glCreateProgram();
    glAttachShader(g_zc_prog, vs); glAttachShader(g_zc_prog, fs);
    glLinkProgram(g_zc_prog);
}
/* (v65) the clear quad over the whole framebuffer: it was drawn through
 * whatever viewport was set last -- a depth clear carried to the other
 * colour buffer just after the 256x256 shadow targets (zrec_bound) cleared
 * only that corner, and the rest of the screen kept the depth of an earlier
 * frame: the world missing in patches, or in whole frames */
static void vgl_zc_viewport(GLuint fbo)
{
    int f = fbo_find(fbo), i;
    if (f < 0) return;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].tex == g_fbo[f].col) {
            glViewport(0, 0, (GLsizei)g_surf[i].pw, (GLsizei)g_surf[i].ph);
            g_sh.vp_ok = 0;
            fr_add(5, fbo, g_surf[i].base, g_surf[i].pw, g_surf[i].ph, 0);
            return;
        }
    for (i = 0; i < GSURF_MAX; i++)   /* (a depth-only framebuffer: its zeta's size) */
        if (g_surf[i].tex && g_surf[i].tex == g_fbo[f].dep) {
            glViewport(0, 0, (GLsizei)g_surf[i].pw, (GLsizei)g_surf[i].ph);
            g_sh.vp_ok = 0;
            return;
        }
}
static void vgl_zclear_quad(int which, float z, int stencil)
{
    glGetIntegerv(GL_VIEWPORT, g_fr_vp);
    fr_add(3, (uint32_t)which, (uint32_t)(z * 1e6f), (uint32_t)stencil, (uint32_t)g_fr_vp[2], (uint32_t)g_fr_vp[3]);
    static float q[6][4];
    static const float xy[6][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1} };
    float zn = 2.0f * z - 1.0f;
    int i;
    if (zn > 0.999999f) zn = 0.999999f;
    if (zn < -0.999999f) zn = -0.999999f;
    for (i = 0; i < 6; i++) { q[i][0] = xy[i][0]; q[i][1] = xy[i][1]; q[i][2] = zn; q[i][3] = 1.0f; }
    vgl_zclear_prog();
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    if (which & 1) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); }
    else { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
    if (which & 2) {
        glEnable(GL_STENCIL_TEST);
        glStencilFuncSeparate(GL_FRONT_AND_BACK, GL_ALWAYS, stencil, 0xFF);
        glStencilOpSeparate(GL_FRONT_AND_BACK, GL_REPLACE, GL_REPLACE, GL_REPLACE);
        glStencilMask(0xFF);
    } else glDisable(GL_STENCIL_TEST);
    /* (vitaGL applies the colour mask only with blending on: without this
     * the quad wrote its 0,0,0,0 over the colour -- the battle's scene wiped
     * by the stencil clear before its shadows, the v55 capture) */
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ZERO, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glUseProgram(g_zc_prog);
    glBindVertexArray(g_vao_x);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (i = 0; i < 16; i++) {
        if (!i) { glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, q); }
        else glDisableVertexAttribArray(i);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
/* (PS Vita) colour cleared with a channel mask: vitaGL's glClear ignores
 * glColorMask, so the battle's alpha-only clear before its shadows (alpha to
 * 0, the shadows MAX into it, then a full-screen quad darkens by it) wiped
 * the whole scene to black -- the v55 capture, draw 640 to 660. A quad of the
 * clear colour, blending ONE/ZERO so that the mask applies. */
static GLuint g_cc_prog;
static GLint g_cc_loc = -1;
static void vgl_cclear_quad(float r, float g, float b, float a, int mr, int mg, int mb, int ma)
{
    static float q[6][4];
    static const float xy[6][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1} };
    int i;
    if (!g_cc_prog) {
        static const char vs_src[] = "void main(float4 vin0, float4 out gl_Position : POSITION) { gl_Position = vin0; }\n";
        static const char fs_src[] = "uniform float4 ccol;\nfloat4 main() : COLOR { return ccol; }\n";
        const char *pa = vs_src, *pb = fs_src;
        GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(vs, 1, &pa, NULL); glCompileShader(vs);
        glShaderSource(fs, 1, &pb, NULL); glCompileShader(fs);
        g_cc_prog = glCreateProgram();
        glAttachShader(g_cc_prog, vs); glAttachShader(g_cc_prog, fs);
        glLinkProgram(g_cc_prog);
        g_cc_loc = glGetUniformLocation(g_cc_prog, "ccol");
    }
    for (i = 0; i < 6; i++) { q[i][0] = xy[i][0]; q[i][1] = xy[i][1]; q[i][2] = 0.0f; q[i][3] = 1.0f; }
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glColorMask(mr, mg, mb, ma);
    glUseProgram(g_cc_prog);
    glUniform4f(g_cc_loc, r, g, b, a);
    glBindVertexArray(g_vao_x);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (i = 0; i < 16; i++) {
        if (!i) { glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, q); }
        else glDisableVertexAttribArray(i);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
#endif
static void gpu_clear_ds(GLuint fbo, int which, float z, int stencil)
{
#if defined(__vita__)
    vgl_trf("gpu_clear_ds fbo %u which %d", fbo, which);
    if (g_zc_mode < 0) { const char *e = getenv("RECOMP_VGL_ZCLEAR"); g_zc_mode = !(e && e[0] == '0'); }
    if (g_zc_mode) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDisable(GL_SCISSOR_TEST);
        vgl_zc_viewport(fbo);
        vgl_zclear_quad(which, z, stencil);
        gpu_sh_reset();
        zrec_cleared(fbo, which, z, stencil);
        return;
    }
    zrec_cleared(fbo, which, z, stencil);
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glClearDepth(z);
    glClearStencil(stencil);
    glClear(((which & 1) ? GL_DEPTH_BUFFER_BIT : 0) | ((which & 2) ? GL_STENCIL_BUFFER_BIT : 0));
    gpu_sh_reset();
}

/* ---- (1.9) MSAA ---- */
static void gsurf_ms_make(GSurf *s)
{
    GLint mx = 0;
    int n = g_msaa;
    glGetIntegerv(GL_MAX_SAMPLES, &mx);
    while (n > 1 && (n > mx || (uint64_t)s->pw * s->ph * (uint64_t)n * 8u > (1536ull << 20))) n >>= 1;
    if (n <= 1) return;
    while (glGetError() != GL_NO_ERROR) {}
    glGenRenderbuffers(1, &s->mrb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->mrb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, s->is_depth ? GL_DEPTH24_STENCIL8 : GL_RGBA8, (GLsizei)s->pw, (GLsizei)s->ph);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (glGetError() != GL_NO_ERROR) { glDeleteRenderbuffers(1, &s->mrb); s->mrb = 0; g_msaa_bad = 1; return; }
    if (s->is_depth) {
        uint32_t zv = s_cb_scratch[0];
        gpu_clear_ds(gpu_fbo_ms(0, s->mrb), 3, (float)(zv >> 8) / 16777215.0f, (int)(zv & 0xFF));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo_ms(s->mrb, 0));
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        gpu_sh_reset();
        s->ss_new = 1;
    }
}
/* the texture made from what was drawn (before anything reads it) */
static void gsurf_resolve(GSurf *s)
{
    GLbitfield b;
    if (!s || !s->mrb || !s->ms_new) return;
    s->ms_new = 0;
    b = s->is_depth ? (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_COLOR_BUFFER_BIT;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->is_depth ? gpu_fbo_ms(0, s->mrb) : gpu_fbo_ms(s->mrb, 0));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->is_depth ? gpu_fbo(0, s->tex) : gpu_fbo(s->tex, 0));
    glDisable(GL_SCISSOR_TEST);
    if (!s->is_depth) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    else { glDepthMask(GL_TRUE); glStencilMask(0xFF); }
    glBlitFramebuffer(0, 0, (GLint)s->pw, (GLint)s->ph, 0, 0, (GLint)s->pw, (GLint)s->ph, b, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gpu_sh_reset();
}
/* a surface drawn without its multisampled copy from now on */
static void gsurf_ms_drop(GSurf *s)
{
    if (!s || !s->mrb) return;
    gsurf_resolve(s);
    gpu_fbo_ms_forget(s->mrb);
    glDeleteRenderbuffers(1, &s->mrb);
    s->mrb = 0; s->ms_new = s->ss_new = 0;
}
/* the framebuffer the title's drawing goes to */
static GLuint gsurf_draw_fbo(GSurf *rt, GSurf *ds)
{
    if (rt->mrb) return gpu_fbo_ms(rt->mrb, ds ? ds->mrb : 0);
    return gpu_fbo(rt->tex, ds ? ds->tex : 0);
}

static GSurf *gsurf_get(uint32_t base, uint32_t w, uint32_t h, int depth)
{
    GSurf *s = NULL, *victim = NULL;
    int i;
    GLuint keep = 0;
    uint32_t keep_pw = 0, keep_ph = 0, keep_w = 0, keep_h = 0, tw, th;
    for (i = 0; i < GSURF_MAX; i++) {
        GSurf *c = &g_surf[i];
        /* zeta surfaces are also keyed by size: a depth buffer must be the
         * size of the colour target it is drawn with, and the title pairs one
         * zeta offset with targets of different sizes (the 128x128 lava
         * passes) */
        if (c->tex && c->base == base && (int)c->is_depth == depth && (!depth || (c->w == w && c->h == h))) { s = c; break; }
        if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
    }
    if (s && s->w >= w && s->h >= h) { s->used = ++g_clock; return s; }
    g_gsurf_gen++;
    if (s) {
        if (w < s->w) w = s->w;
        if (h < s->h) h = s->h;
        if (!depth && s->tex) {
            /* a colour surface that grows keeps what it holds */
            keep = s->tex; s->tex = 0;
            keep_pw = s->pw; keep_ph = s->ph; keep_w = s->w; keep_h = s->h;
        }
        gsurf_free(s);
    }
    else { s = victim; gsurf_free(s); }

    tw = RSX_(w, w, h); th = RSY_(h, w, h);
#if defined(__vita__)
    vgl_make_room((uint64_t)tw * th * 4);   /* (render targets come before cached textures) */
#endif
    s->tex = gpu_tex2d(tw, th, depth);
    if (!s->tex) { memset(s, 0, sizeof *s); return NULL; }
    if (depth) {
        /* A zeta surface starts at the title's clear value, as the software
         * z-buffer does: titles with an inverted depth range never clear
         * depth and rely on it. */
#if defined(__vita__)
        s->ss_new = 2;   /* (cleared when first drawn with a colour target: gpu_bind_targets) */
#else
        uint32_t zv = s_cb_scratch[0];
        gpu_clear_ds(gpu_fbo(0, s->tex), 3, (float)(zv >> 8) / 16777215.0f, (int)(zv & 0xFF));
#endif
    } else {
        GLuint fbo = gpu_fbo(s->tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (keep) {
            /* Grown (a later copy reaching further down, as the heat haze's
             * second patch does): the old contents carry over, when both are
             * at the same scale. */
            if (keep_pw * w == tw * keep_w && keep_ph * h == th * keep_h) {
                GLuint src = gpu_fbo(keep, 0);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
                glBlitFramebuffer(0, 0, (GLint)keep_pw, (GLint)keep_ph, 0, 0, (GLint)keep_pw, (GLint)keep_ph,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            gpu_fbo_forget(keep);
            glDeleteTextures(1, &keep);
        }
        gpu_sh_reset();
    }
#if defined(__vita__)
    if (!depth) s->ss_new = 0;
#endif
    s->base = base; s->w = w; s->h = h; s->is_depth = (uint32_t)depth; s->used = ++g_clock;
#if defined(__vita__)
    {
        static unsigned n;
        if (n < 80 || n % 50 == 0) {
            fprintf(stderr, "  [VGL] surface %08X %ux%u (%ux%u) %s\n", base, w, h, s->pw, s->ph, depth ? "zeta" : "colour");
            vgl_mem_report("new surface");
        }
        n++;
    }
#endif
    s->pw = tw; s->ph = th;
    if (g_msaa > 1) gsurf_ms_make(s);
    if (getenv("RECOMP_SURF_LOG")) {
        static int n;
        if (n++ < 200)
            fprintf(stderr, "  [SURF] new %s %08X %ux%u (fmt %08X pitch %08X, clip %u,%u %ux%u)\n", depth ? "zeta" : "colour",
                    base, w, h, s_gpu.format, s_gpu.pitch, s_gpu.clip_x, s_gpu.clip_y, s_gpu.clip_w, s_gpu.clip_h);
    }
    s->mem_sum = 0;
    if (!depth) {
        /* Start from what the guest has there: a surface the CPU filled (or
         * the software path drew before the switch) keeps its picture. */
        const uint8_t *m = (const uint8_t *)XR_PTR(base);
        uint32_t pitch = s_gpu.pitch && (s_gpu.pitch & 0xFFFF) >= w * 4 ? (s_gpu.pitch & 0xFFFF) : w * 4;
        if (dma_resolve(s_gpu.color_offset) != base) pitch = w * 4;
        gsurf_upload(s, m, pitch);
        s->pitch = pitch;
        s->mem_sum = gpu_mem_sum(base, pitch * h);
    }
    return s;
}

/* The GPU copy of a surface lives on the card; the guest's copy in memory
 * only changes when the CPU writes it -- a movie frame, a loading screen. If
 * memory has changed since the two were last in sync, the CPU drew: take its
 * picture. Checked once per pushbuffer run. */
static void gsurf_sync_from_mem(GSurf *s)
{
    uint32_t sum;
    if (!s || s->is_depth || !s->pitch) return;
    if (s->synced_run == g_pb_runs) return;
    s->synced_run = g_pb_runs;
    sum = gpu_mem_sum(s->base, s->pitch * s->h);
    if (sum == s->mem_sum) return;
    gsurf_upload(s, (const uint8_t *)XR_PTR(s->base), s->pitch);
    s->mem_sum = sum;
}

/* ---------------- textures from guest memory ---------------- */

typedef struct {
    uint32_t off, color, w, h, pitch, ver, hash;
    GLuint tex;
    int levels;
    uint64_t used;
    uint32_t seen_frame, seen_hash;   /* (PS Vita: checked already this frame) */
    int opaque;                       /* (v206) every texel's alpha 255 */
} GTex;
static int g_gtex_opaque;             /* (v206) of the texture gtex_get just gave */
static int g_ab2_ph;                  /* (v206 [AB2]: 1 = the alpha test kept everywhere, as before v206) */
static unsigned g_ab2_drop, g_ab2_keep;
#define FHASH_MAX 512
static uint32_t g_fhash[2][FHASH_MAX];
static int g_fhash_n[2], g_fhash_cur;
/* (v111) the frame's texture set through a small hash (the list was scanned
 * on every bind, up to 512 entries) */
static uint32_t g_fh_set[2048], g_fh_gen[2048], g_fh_cur_gen = 1;
static void fhash_note(uint32_t h)
{
    int c = g_fhash_cur, n = g_fhash_n[c];
    uint32_t k = (h * 2654435761u) >> 21, q;
    for (q = 0; q < 2048 && g_fh_gen[k] == g_fh_cur_gen; q++, k = (k + 1) & 2047u)
        if (g_fh_set[k] == h) return;
    if (q < 2048) { g_fh_gen[k] = g_fh_cur_gen; g_fh_set[k] = h; }
    if (n < FHASH_MAX) { g_fhash[c][n] = h; g_fhash_n[c] = n + 1; }
}
int xbox_FrameTextureList(uint32_t *out, int max)
{
    int p = g_fhash_cur ^ 1, i, n = g_fhash_n[p] < max ? g_fhash_n[p] : max;
    for (i = 0; i < n; i++) out[i] = g_fhash[p][i];
    return n;
}
int xbox_FrameUsedTexture(uint32_t hash)
{
    int p = g_fhash_cur ^ 1, i;
    for (i = 0; i < g_fhash_n[p]; i++) if (g_fhash[p][i] == hash) return 1;
    return 0;
}
#define GTEX_MAX 512
static GTex g_tex[GTEX_MAX];

#if defined(__vita__)
/* (PS Vita) vitaGL's memory: CDRAM (128 MB) and what the game's heap leaves
 * of the RAM. Textures are decoded to 32-bit RGBA -- the title's compressed
 * ones take 4 to 8 times their size -- and a level fills both: an allocation
 * then fails and the next clear or draw on that target crashed in vitaGL
 * (the uniform buffer of a scene that never began). The texture cache keeps
 * to a budget (RECOMP_VGL_TEXMB, default 64 MB) and to a margin of free
 * memory (RECOMP_VGL_FREEMB, default 12 MB), the least recently used
 * textures going first (read again from guest memory when they come back). */
static uint64_t g_vtex_bytes;
static unsigned g_vtex_evicted;
static uint32_t vgl_free_mb(void)
{
    return (uint32_t)((vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM) + vglMemFree(VGL_MEM_PHYCONT)) >> 20);
}
static void vgl_mem_report(const char *why)
{
    int i, n = 0;
    for (i = 0; i < GTEX_MAX; i++) if (g_tex[i].tex) n++;
    {
        SceKernelFreeMemorySizeInfo mi;
        mi.size = sizeof mi;
        if (sceKernelGetFreeMemorySize(&mi) >= 0)
            fprintf(stderr, "  [VGL] system free memory: RAM %d MB (thread stacks come from here), CDRAM %d MB\n",
                    mi.size_user >> 20, mi.size_cdram >> 20);
    }
    fprintf(stderr, "  [VGL] memory (%s): free VRAM %u MB, RAM %u MB, phycont %u MB; textures %d (%u MB), %u let go; deleted (after 3 frames) %u framebuffers %u textures %u renderbuffers\n", why,
            (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 20), (unsigned)(vglMemFree(VGL_MEM_RAM) >> 20),
            (unsigned)(vglMemFree(VGL_MEM_PHYCONT) >> 20), n, (unsigned)(g_vtex_bytes >> 20), g_vtex_evicted,
            g_vdel_total[0], g_vdel_total[1], g_vdel_total[2]);
}
static uint64_t gtex_bytes(const GTex *e)
{
    uint64_t b = (uint64_t)e->w * e->h * 4;
    return e->levels > 1 ? b + b / 3 : b;
}
static void gtex_drop(GTex *e)
{
    if (e->tex) { glDeleteTextures(1, &e->tex); g_vtex_bytes -= gtex_bytes(e); g_vtex_evicted++; }
    memset(e, 0, sizeof *e);
}
/* room for `need` bytes more: the oldest textures go (not `keep`) */
static void gtex_make_room(uint64_t need, const GTex *keep)
{
    static uint64_t budget;
    static uint32_t margin;
    int guard = GTEX_MAX;
    if (!budget) {
        const char *e = getenv("RECOMP_VGL_TEXMB"), *f = getenv("RECOMP_VGL_FREEMB");
        budget = (uint64_t)(e && atoi(e) > 0 ? atoi(e) : 64) << 20;
        margin = f && atoi(f) > 0 ? (uint32_t)atoi(f) : 12;
    }
    while (guard-- > 0 && (g_vtex_bytes + need > budget || vgl_free_mb() < margin + (uint32_t)(need >> 20))) {
        GTex *old = NULL;
        int i;
        for (i = 0; i < GTEX_MAX; i++)
            if (g_tex[i].tex && &g_tex[i] != keep && (!old || g_tex[i].used < old->used)) old = &g_tex[i];
        if (!old) break;
        gtex_drop(old);
    }
}
static void vgl_make_room(uint64_t need) { gtex_make_room(need, NULL); }
#endif

/* Ultra textures (nv2a_ultra.inc): the pack's pictures as BC7 (BPTC)
 * textures, all their levels -- where the driver has BPTC (GL 4.2, or
 * ARB_texture_compression_bptc; not macOS's GL 4.1) */
static GLuint *g_utex;
static int s_ultra_used, g_ultra_bptc = -1;
static GLuint ultra_gpu_get(int idx)
{
    UltraFile *u = &g_uf[idx];
#if defined(__vita__)
    g_ultra_bptc = 0;
#endif
    if (g_ultra_bptc < 0) {
        GLint mj = 0, mn = 0;
        glGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &mj); glGetIntegerv(0x821C /* GL_MINOR_VERSION */, &mn);
        g_ultra_bptc = (mj > 4 || (mj == 4 && mn >= 2)) || gl_has_ext("GL_ARB_texture_compression_bptc") || gl_has_ext("GL_EXT_texture_compression_bptc");
        if (!g_ultra_bptc) fprintf(stderr, "  [ULTRA] no BC7 (BPTC) textures in this OpenGL: Ultra textures off\n");
    }
    if (!g_ultra_bptc) return 0;
    if (!g_utex) { g_utex = (GLuint *)calloc((size_t)g_uicap, sizeof *g_utex); if (!g_utex) return 0; }
    if (u->state == 3 && g_utex[idx]) { u->used = ++g_ultra_clock; return g_utex[idx]; }
    if (u->state == 2) {
        uint32_t l, W = u->w, H = u->h, off = 0, lv = u->levels > 16 ? 16 : u->levels;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &g_utex[idx]);
        glBindTexture(GL_TEXTURE_2D, g_utex[idx]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)lv - 1);
        for (l = 0; l < lv; l++) {
            uint32_t sz = ((W + 3) / 4) * ((H + 3) / 4) * 16;
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)l, 0x8E8C /* COMPRESSED_RGBA_BPTC_UNORM */, (GLsizei)W, (GLsizei)H, 0, (GLsizei)sz, u->data + off);
            off += sz;
            W = W > 1 ? W / 2 : 1; H = H > 1 ? H / 2 : 1;
        }
        free(u->data); u->data = NULL;
        g_sh.tex_ok = 0;
        g_ultra_bytes += u->size;
        u->used = ++g_ultra_clock;
        InterlockedExchange(&u->state, 3);
        while (g_ultra_bytes > ultra_budget()) {
            int i, old = -1;
            for (i = 0; i < g_uin; i++)
                if (i != idx && g_uf[i].state == 3 && g_utex[i] && (old < 0 || g_uf[i].used < g_uf[old].used)) old = i;
            if (old < 0) break;
            glDeleteTextures(1, &g_utex[old]); g_utex[old] = 0;
            g_ultra_bytes -= g_uf[old].size;
            InterlockedExchange(&g_uf[old].state, 0);
        }
        return g_utex[idx];
    }
    if (u->state == 0) ultra_request(idx);
    return 0;
}

#if defined(__vita__)
static uint16_t *g_gtex_fidx;
static void cap_note_text(const char *fmt, ...);
static char g_cap_dir[64] = "ux0:data/twotowers/cap/0";
static int g_cap_idx;
#endif
static GLuint gtex_get(const Texture *t)
{
    uint32_t ver = 0;
    const uint32_t *argb;
    GTex *e = NULL, *victim = NULL;
    int i;
    s_ultra_used = 0;
    g_gtex_opaque = 0;
#if defined(__vita__)
    {   /* (v139) a film picture from the video decoder (hwmovie.c): drawn
         * straight from the decoder's NV12 frame -- a vitaGL YUV texture whose
         * memory is pointed at that frame, the GPU's texture unit turns it to
         * RGB. No decode, no copy, no upload. TT_HWMOVIE_YUV=0: as RGBA. */
        extern volatile uint32_t g_hwm_tex_addr[2]; extern void *volatile g_hwm_frame; extern volatile uint32_t g_hwm_fw, g_hwm_fh;
        extern void *volatile g_hwm_rgba; extern volatile uint32_t g_hwm_rgba_w, g_hwm_rgba_h;
        uint32_t o = t->offset & 0x03FFFFFFu;
        void *fr = g_hwm_frame, *rg = g_hwm_rgba;
        (void)rg;
        {   /* (v141) the film's two RGBA textures: made here (their memory given to
             * hwmovie.c, which converts each picture into the one not shown) */
            extern void *volatile g_hwm_texmem[2]; extern volatile uint32_t g_hwm_texmem_w, g_hwm_texmem_h, g_hwm_texmem_stride;
            extern volatile uint32_t g_hwm_req_w, g_hwm_req_h; extern volatile int g_hwm_show;
            static GLuint ftex[2]; static uint32_t fw_, fh_;
            uint32_t rw = g_hwm_req_w, rh = g_hwm_req_h;
            if (rw && rh && (fw_ != rw || fh_ != rh)) {
                int b2;
                g_hwm_texmem[0] = g_hwm_texmem[1] = NULL;
                if (!ftex[0]) glGenTextures(2, ftex);
                glActiveTexture(GL_TEXTURE0);
                void *zero = calloc((size_t)rw * rh, 4);   /* (real data: a texture made from NULL may not count as filled in vitaGL) */
                for (b2 = 0; b2 < 2; b2++) {
                    glBindTexture(GL_TEXTURE_2D, ftex[b2]);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)rw, (GLsizei)rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                }
                {
                    void *m0, *m1;
                    glBindTexture(GL_TEXTURE_2D, ftex[0]); m0 = vglGetTexDataPointer(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, ftex[1]); m1 = vglGetTexDataPointer(GL_TEXTURE_2D);
                    g_hwm_texmem_stride = (rw + 7u) & ~7u;   /* (vitaGL's linear textures: rows of a multiple of 8 texels) */
                    g_hwm_texmem_w = rw; g_hwm_texmem_h = rh;
                    __atomic_thread_fence(__ATOMIC_SEQ_CST);
                    g_hwm_texmem[0] = m0; g_hwm_texmem[1] = m1;
                    fprintf(stderr, "  [HWMOVIE] film textures %u, %u (%ux%u) at %p, %p\n", ftex[0], ftex[1], rw, rh, m0, m1);
                }
                free(zero);
                fw_ = rw; fh_ = rh;
                g_sh.tex_ok = 0;
            }
            if (g_hwm_req_w && g_hwm_show >= 0 && g_hwm_texmem[0]
                && ((g_hwm_tex_addr[0] && o == g_hwm_tex_addr[0]) || (g_hwm_tex_addr[1] && o == g_hwm_tex_addr[1])))
                return ftex[g_hwm_show & 1];
        }
        if (fr && ((g_hwm_tex_addr[0] && o == g_hwm_tex_addr[0]) || (g_hwm_tex_addr[1] && o == g_hwm_tex_addr[1]))) {
            static GLuint ytex; static uint32_t yw, yh; static void *yptr;
            uint32_t w = g_hwm_fw, h = g_hwm_fh;
            glActiveTexture(GL_TEXTURE0);
            if (!ytex || yw != w || yh != h) {
                if (!ytex) glGenTextures(1, &ytex);
                glBindTexture(GL_TEXTURE_2D, ytex);
                glTexImage2D(GL_TEXTURE_2D, 0, VGL_YUV420P_NV12_BT601, (GLsizei)w, (GLsizei)h, 0, VGL_YUV420P_NV12_BT601, GL_UNSIGNED_BYTE, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                yw = w; yh = h; yptr = NULL;
                fprintf(stderr, "  [HWMOVIE] the film drawn from the decoder's frames (%ux%u NV12 texture %u)\n", w, h, ytex);
            }
            if (yptr != fr) { glBindTexture(GL_TEXTURE_2D, ytex); vglOverloadTexDataPointer(GL_TEXTURE_2D, fr); yptr = fr; }
            g_sh.tex_ok = 0;
            return ytex;
        }
    }
    {   /* (v68) a texture already checked this frame: no second look at its
         * memory -- the decode cache's lookup cost ~10 us a bind, 1400 binds
         * a frame (16-23 ms of the render thread) */
        static uint16_t fidx[4096];
        uint32_t hk = ((t->offset >> 6) ^ (t->offset >> 17) ^ (t->color * 0x9E37u) ^ (t->width << 3) ^ (t->height << 7)) & 4095u;
        int q = (int)fidx[hk] - 1;
        if (q >= 0 && q < GTEX_MAX) {
            GTex *c = &g_tex[q];
            if (c->tex && c->seen_frame == g_present_n + 1 && c->off == t->offset && c->color == t->color
                && c->w == t->width && c->h == t->height && c->pitch == t->pitch && g_ultra_on != 1 && !g_cap_on) {
                fhash_note(c->seen_hash);
                c->used = ++g_clock;
                g_tq_n[0]++;
                g_gtex_opaque = c->opaque;
                return c->tex;
            }
        }
        g_gtex_fidx = &fidx[hk];
        g_tq_n[1]++;
        if (q >= 0 && q < GTEX_MAX && g_tex[q].tex && g_tex[q].seen_frame == g_present_n + 1) g_tq_n[2]++;   /* (its slot held another texture of this frame) */
    }
    VWHERE("tex: decode cache");   /* (v112: finer zones in the texture lookup) */
    { TX_T0(tc); s_texc_drop_ok = !g_cap_on; argb = texc_get_ver(t, &ver); s_texc_drop_ok = 0; TX_ADD(1, tc); }
    VWHERE("tex: GL texture slot");
#else
    argb = texc_get_ver(t, &ver);
#endif
    if (!argb) return 0;
#if defined(__vita__)
    if (g_cap_on && argb != s_texc_dropped_px) {   /* (the capture: what the texture holds) */
        uint32_t i, n = t->width * t->height, st = n > 4096 ? n / 4096 : 1, k = 0;
        uint64_t sr = 0, sa = 0;
        for (i = 0; i < n; i += st, k++) { uint32_t c = argb[i]; sr += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); sa += c >> 24; }
        cap_note_text("TEX %08X %ux%u fmt %08X pitch %u: mean rgb %.0f alpha %.0f (ver %u)", t->offset, t->width, t->height, t->color, t->pitch,
                      k ? (double)sr / (3.0 * k) : 0.0, k ? (double)sa / k : 0.0, ver);
    }
#endif
    fhash_note(s_texc_last_hash);
    if (s_texc_last_uidx >= 0 && g_ultra_on == 1 && ultra_sky_check(s_texc_last_uidx, t, s_3d.depth_test, s_3d.depth_mask)) {   /* Ultra textures: the pack's picture, once it is read */
        GLuint hd = ultra_gpu_get(s_texc_last_uidx);
        if (hd) { s_ultra_used = 1; return hd; }
    }
    {   /* (the slot this texture had last time, from a small index: the full
         * scan of all GTEX_MAX slots on every bind only on a miss) */
        static uint16_t gidx[4096];
        uint32_t hk = ((t->offset >> 6) ^ (t->offset >> 17) ^ (t->color * 0x9E37u) ^ (t->width << 3) ^ (t->height << 7)) & 4095u;
        int q = (int)gidx[hk] - 1;
        if (q >= 0 && q < GTEX_MAX) {
            GTex *c = &g_tex[q];
            if (c->tex && c->off == t->offset && c->color == t->color && c->w == t->width && c->h == t->height
                && c->pitch == t->pitch) e = c;
        }
        if (!e) {
            g_tq_n[4]++;
            for (i = 0; i < GTEX_MAX; i++) {
                GTex *c = &g_tex[i];
                if (c->tex && c->off == t->offset && c->color == t->color && c->w == t->width && c->h == t->height
                    && c->pitch == t->pitch) { e = c; break; }
                if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
            }
            gidx[hk] = (uint16_t)((e ? e : victim) - g_tex + 1);
        }
    }
    if (!e) {
        int lv = 1;
        e = victim;
#if defined(__vita__)
        if (e->tex) { gtex_drop(e); g_vtex_evicted--; }   /* (a slot reused, not memory let go) */
        else memset(e, 0, sizeof *e);
        gtex_make_room((uint64_t)t->width * t->height * 4, NULL);
#else
        if (e->tex) glDeleteTextures(1, &e->tex);
        memset(e, 0, sizeof *e);
#endif
        gpu_filter_opts();
        /* A full mip chain the GPU builds from the top level (the title's
         * own mip levels are not decoded), for stages that filter with
         * mipmaps -- less shimmer at a distance, and what anisotropic
         * filtering works from. */
        if (g_mips && t->width > 1 && t->height > 1 && tex_size_from_format(t->color)) {
            uint32_t m = t->width > t->height ? t->width : t->height;
            while (m > 1) { m >>= 1; lv++; }
        }
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &e->tex);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lv - 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)t->width, (GLsizei)t->height, 0, GL_BGRA,
                     GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        e->levels = lv;
        e->off = t->offset; e->color = t->color; e->w = t->width; e->h = t->height; e->pitch = t->pitch;
#if defined(__vita__)
        g_vtex_bytes += gtex_bytes(e);
#endif
        e->ver = ver - 1;
        g_sh.tex_ok = 0;
    }
#if defined(__vita__)
    if (e->ver != ver && argb == s_texc_dropped_px) {
        /* (its copy was let go: decoded again for the upload) */
        argb = texc_get_ver(t, &ver);
        if (!argb) return e->tex;
    }
#endif
    if (e->ver != ver) {
        VWHERE("tex: upload");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#if defined(__vita__)
        g_vp_up++; g_vp_upkb += (t->width * t->height * 4) >> 10;
#endif
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)t->width, (GLsizei)t->height, GL_BGRA,
                        GL_UNSIGNED_INT_8_8_8_8_REV, argb);
        if (e->levels > 1) glGenerateMipmap(GL_TEXTURE_2D);
        e->ver = ver;
        g_sh.tex_ok = 0;
#if defined(__vita__)
        {   /* (v206) no texel see-through at all? (the alpha test of a draw that only reads such textures cannot fail) */
            const uint32_t n = t->width * t->height;
            uint32_t q, a = 0xFF000000u;
            for (q = 0; q < n; q++) a &= argb[q];
            e->opaque = (a >> 24) == 0xFF;
        }
#endif
    }
    e->used = ++g_clock;
    g_gtex_opaque = e->opaque;
#if defined(__vita__)
    VWHERE("tex: after");
    if (argb != s_texc_dropped_px) texc_drop_last();   /* (on the GPU now: the copy let go) */
    e->seen_frame = g_present_n + 1; e->seen_hash = s_texc_last_hash;
    if (g_gtex_fidx) { *g_gtex_fidx = (uint16_t)(e - g_tex + 1); g_gtex_fidx = NULL; }
#endif
    return e->tex;
}

/* ---------------- samplers ---------------- */

typedef struct { uint64_t key; GLuint obj; } GSmp;
#define GSMP_MAX 128
static GSmp g_smp_c[GSMP_MAX];
#if defined(__vita__)
static int g_vita_mip_sample = 1;

/* (v198) between two levels the nearest one, not both blended: one texture read a pixel (TT_TRILINEAR=1: both) */
static GLenum vita_mipf(void) { static int t = -1; if (t < 0) { const char *e = getenv("TT_TRILINEAR"); t = e && e[0] == '1'; } return t ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST; }
#define VITA_MIPF vita_mipf()
#else
#define VITA_MIPF GL_LINEAR_MIPMAP_LINEAR
#endif
static GLuint gpu_sampler_ex(uint32_t au, uint32_t av, int linear, int mip)
{
    uint64_t k;
    int i;
    GLuint s = 0;
    gpu_filter_opts();
    if (!g_mips) mip = 0;
#if defined(__vita__)
    if (!g_vita_mip_sample) mip = 0;   /* (v198 [AB]: the mip levels there but not read) */
#endif
    k = ((uint64_t)au << 16) | ((uint64_t)av << 8) | (uint64_t)linear | ((uint64_t)mip << 32) | (1ull << 40);
    for (i = 0; i < GSMP_MAX && g_smp_c[i].obj; i++)
        if (g_smp_c[i].key == k) return g_smp_c[i].obj;
    glGenSamplers(1, &s);
    {
        uint32_t a[2] = { au, av };
        GLenum pn[2] = { GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T };
        int j;
        for (j = 0; j < 2; j++)
            glSamplerParameteri(s, pn[j], a[j] == 1 ? GL_REPEAT : a[j] == 2 ? GL_MIRRORED_REPEAT
                                         : a[j] == 4 ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, linear ? (mip ? VITA_MIPF : GL_LINEAR)
                                                              : (mip ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST));
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, 0.0f);
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, mip ? 1000.0f : 0.0f);
        if (linear && mip && g_aniso > 1 && g_max_aniso > 1.0f)
            glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)g_aniso < g_max_aniso ? (float)g_aniso : g_max_aniso);
    }
    if (i < GSMP_MAX) { g_smp_c[i].key = k; g_smp_c[i].obj = s; }
    return s;
}
static GLuint gpu_sampler(uint32_t au, uint32_t av, int linear) { return gpu_sampler_ex(au, av, linear, 0); }
static void gpu_samplers_drop(void)
{
    int i;
    for (i = 0; i < GSMP_MAX; i++) if (g_smp_c[i].obj) glDeleteSamplers(1, &g_smp_c[i].obj);
    memset(g_smp_c, 0, sizeof g_smp_c);
    g_sh.tex_ok = 0;
}

/* ---------------- shaders ---------------- */

/* GLSL versions of the HLSL in nv2a_d3d11_shaders.h; the uniform blocks have
 * the cbuffers' layouts (std140 packs these 16-byte members the same way),
 * so the CPU side fills them with the same bytes. */

static const char s_glsl_vs_main[] =
"#version 330 core\n"
"layout(std140) uniform VSC { vec4 vp; };\n"
"layout(location = 0) in vec4 a_p;\n"
"layout(location = 1) in vec4 a_d0;\n"
"layout(location = 2) in vec4 a_d1;\n"
"layout(location = 3) in vec4 a_fog;\n"
"layout(location = 4) in vec4 a_t0;\n"
"layout(location = 5) in vec4 a_t1;\n"
"layout(location = 6) in vec4 a_t2;\n"
"layout(location = 7) in vec4 a_t3;\n"
"out vec4 v_d0; out vec4 v_d1; out vec4 v_fog; out vec4 v_t0; out vec4 v_t1; out vec4 v_t2; out vec4 v_t3;\n"
"invariant gl_Position;\n"
"void main() {\n"
"  gl_Position = vec4(a_p.x * vp.x - a_p.w, a_p.y * vp.y - a_p.w, 2.0 * a_p.z * vp.z - a_p.w, a_p.w);\n"
"  v_d0 = a_d0; v_d1 = a_d1; v_fog = a_fog; v_t0 = a_t0; v_t1 = a_t1; v_t2 = a_t2; v_t3 = a_t3;\n"
"}\n";

/* Declarations shared by the general fragment shader and the specialized
 * ones (as the HLSL's text before ps_main). */
static const char s_glsl_ps_head[] =
"#version 330 core\n"
"struct CStage { ivec4 cin; ivec4 ain; ivec4 f1; ivec4 f2; ivec4 f3; vec4 c0; vec4 c1; };\n"
"layout(std140) uniform PSC {\n"
"  CStage st[8];\n"
"  ivec4 fin0; ivec4 fin1; ivec4 misc;\n"
"  vec4 ff0; vec4 ff1; vec4 fogc; vec4 fogp;\n"
"  ivec4 smode; ivec4 sflags;\n"
"  vec4 sscale[4];\n"
"  ivec4 atest; ivec4 sdot; ivec4 sinp;\n"
"};\n"
"uniform sampler2D tx0; uniform sampler2D tx1; uniform sampler2D tx2; uniform sampler2D tx3;\n"
"in vec4 v_d0; in vec4 v_d1; in vec4 v_fog; in vec4 v_t0; in vec4 v_t1; in vec4 v_t2; in vec4 v_t3;\n"
"layout(location = 0) out vec4 o_color;\n"
"float sat(float x) { return clamp(x, 0.0, 1.0); }\n"
"vec3 sat3(vec3 x) { return clamp(x, 0.0, 1.0); }\n"
"vec4 sat4(vec4 x) { return clamp(x, 0.0, 1.0); }\n"
"float cmap(float x, int m) {\n"
"  if (m == 0) return max(x, 0.0);\n"
"  if (m == 1) return 1.0 - sat(x);\n"
"  if (m == 2) return 2.0 * max(x, 0.0) - 1.0;\n"
"  if (m == 3) return -2.0 * max(x, 0.0) + 1.0;\n"
"  if (m == 4) return max(x, 0.0) - 0.5;\n"
"  if (m == 5) return -max(x, 0.0) + 0.5;\n"
"  if (m == 6) return x;\n"
"  return -x;\n"
"}\n"
"float cop(float x, int op) {\n"
"  if (op == 1) x -= 0.5; else if (op == 2) x *= 2.0; else if (op == 3) x = (x - 0.5) * 2.0;\n"
"  else if (op == 4) x *= 4.0; else if (op == 6) x *= 0.5;\n"
"  return clamp(x, -1.0, 1.0);\n"
"}\n"
/* A depth-encoded stage reads the Z24S8 surface as A8R8G8B8:
 * A = z[23:16], R = z[15:8], G = z[7:0], B = stencil (here 0). */
"vec4 zread(sampler2D t, vec2 uv) {\n"
"  ivec2 sz = textureSize(t, 0);\n"
"  ivec2 ip = ivec2(clamp(floor(uv * vec2(sz)), vec2(0.0), vec2(sz - ivec2(1))));\n"
"  uint z = uint(texelFetch(t, ip, 0).r * 16777215.0 + 0.5);\n"
"  return vec4(float((z >> 8u) & 255u) / 255.0, float(z & 255u) / 255.0, 0.0, float((z >> 16u) & 255u) / 255.0);\n"
"}\n"
"vec3 dnorm(vec4 c, int m) {\n"
"  if (m == 0) return c.rgb;\n"
"  if (m == 3) { vec3 b = round(c.rgb * 255.0); return mix(b, b - 256.0, greaterThan(b, vec3(127.5))) / 127.0; }\n"
"  return c.rgb * 2.0 - 1.0;\n"
"}\n";

/* The general combiner interpreter (ps_main). */
static const char s_glsl_ps_main[] =
"vec4 stex(sampler2D t, vec4 tc, int k) {\n"
"  int mode = smode[k], fl = sflags[k];\n"
"  if (mode == 0) return vec4(0.0);\n"
"  if (mode == 4) return sat4(tc);\n"
"  if ((fl & 1) == 0) return vec4(0.0, 0.0, 0.0, 1.0);\n"
"  vec2 uv = tc.xy;\n"
"  if (mode == 1 && tc.w != 0.0 && tc.w != 1.0) uv /= tc.w;\n"
"  if ((fl & 4) == 0) uv *= sscale[k].xy;\n"
"  if ((fl & 8) != 0) return zread(t, uv);\n"
"  return texture(t, uv);\n"
"}\n"
"vec3 crgb(int c, vec4 R[16]) {\n"
"  int reg = c & 15, al = (c >> 4) & 1, m = (c >> 8) & 7;\n"
"  vec4 r = R[reg];\n"
"  if (al != 0) { float v = cmap(r.a, m); return vec3(v); }\n"
"  return vec3(cmap(r.r, m), cmap(r.g, m), cmap(r.b, m));\n"
"}\n"
"float calp(int c, vec4 R[16]) {\n"
"  int reg = c & 15, al = (c >> 4) & 1, m = (c >> 8) & 7;\n"
"  return cmap(al != 0 ? R[reg].a : R[reg].b, m);\n"
"}\n"
"void main() {\n"
"  vec4 R[16];\n"
"  int k;\n"
"  for (k = 0; k < 16; k++) R[k] = vec4(0.0);\n"
"  float dp1 = 0.0, dp2 = 0.0;\n"
"  if ((sflags.x & 2) != 0) R[8] = stex(tx0, v_t0, 0);\n"
"  if (smode.y == 17) { dp1 = dot(v_t1.xyz, dnorm(R[8], sdot.y)); R[9] = vec4(0.0); }\n"
"  else if ((sflags.y & 2) != 0) R[9] = stex(tx1, v_t1, 1);\n"
"  if (smode.z == 17) { dp2 = dot(v_t2.xyz, dnorm(sinp.z != 0 ? R[9] : R[8], sdot.z)); R[10] = vec4(0.0); }\n"
"  else if (smode.z == 9) { vec2 uv = vec2(dp1, dot(v_t2.xyz, dnorm(sinp.z != 0 ? R[9] : R[8], sdot.z)));\n"
"    R[10] = (sflags.z & 1) != 0 ? texture(tx2, uv) : vec4(0.0, 0.0, 0.0, 1.0); }\n"
"  else if ((sflags.z & 2) != 0) R[10] = stex(tx2, v_t2, 2);\n"
"  if (smode.w == 9) { vec4 nn = sinp.w == 2 ? R[10] : (sinp.w == 1 ? R[9] : R[8]);\n"
"    vec2 uv = vec2(dp2, dot(v_t3.xyz, dnorm(nn, sdot.w)));\n"
"    R[11] = (sflags.w & 1) != 0 ? texture(tx3, uv) : vec4(0.0, 0.0, 0.0, 1.0); }\n"
"  else if ((sflags.w & 2) != 0) R[11] = stex(tx3, v_t3, 3);\n"
"  R[4] = v_d0; R[5] = v_d1;\n"
"  vec4 fc = vec4(fogc.rgb, 1.0);\n"
"  if (fogp.w != 0.0) {\n"
"    float dd = v_fog.x, f; int fm = int(fogp.z);\n"
"    if (fm == 0x800 || fm == 0x802) { if (fm == 0x802) dd = abs(dd); f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5; }\n"
"    else if (fm == 0x801 || fm == 0x803) { if (fm == 0x803) dd = abs(dd); f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5; }\n"
"    else { if (fm == 0x804) dd = abs(dd); f = fogp.x + dd * fogp.y - 1.0; }\n"
"    fc.a = sat(f);\n"
"  }\n"
"  R[3] = fc;\n"
"  R[12].a = R[8].a;\n"
"  vec4 o;\n"
"  if (misc.w == 0) {\n"
"    o = ((atest.w != 0) ? R[8] : vec4(1.0)) * R[4];\n"
"  } else {\n"
"    for (k = 0; k < misc.x; k++) {\n"
"      CStage S = st[k];\n"
"      R[1] = S.c0; R[2] = S.c1;\n"
"      vec3 A = crgb(S.cin.x, R), B = crgb(S.cin.y, R), C = crgb(S.cin.z, R), D = crgb(S.cin.w, R);\n"
"      vec3 ab = (S.f1.y & 2) != 0 ? vec3(dot(A, B)) : A * B;\n"
"      vec3 cd = (S.f1.y & 1) != 0 ? vec3(dot(C, D)) : C * D;\n"
"      bool sel = misc.y != 0 ? (R[12].a >= 0.5) : ((int(R[12].a * 255.0 + 0.5) & 1) != 0);\n"
"      vec3 sm = (S.f1.y & 4) != 0 ? (sel ? cd : ab) : ab + cd;\n"
"      int op = S.f1.z;\n"
"      vec3 rab = vec3(cop(ab.x, op), cop(ab.y, op), cop(ab.z, op));\n"
"      vec3 rcd = vec3(cop(cd.x, op), cop(cd.y, op), cop(cd.z, op));\n"
"      vec3 rsm = vec3(cop(sm.x, op), cop(sm.y, op), cop(sm.z, op));\n"
"      float a0 = calp(S.ain.x, R), a1 = calp(S.ain.y, R), a2 = calp(S.ain.z, R), a3 = calp(S.ain.w, R);\n"
"      float aab = a0 * a1, acd = a2 * a3;\n"
"      float asm_ = (S.f1.y & 8) != 0 ? (sel ? acd : aab) : aab + acd;\n"
"      int aop = S.f1.w;\n"
"      float ra0 = cop(aab, aop), ra1 = cop(acd, aop), ra2 = cop(asm_, aop);\n"
"      if (S.f2.x != 0) { R[S.f2.x].rgb = rab; if ((S.f3.w & 2) != 0) R[S.f2.x].a = rab.b; }\n"
"      if (S.f2.y != 0) { R[S.f2.y].rgb = rcd; if ((S.f3.w & 1) != 0) R[S.f2.y].a = rcd.b; }\n"
"      if (S.f2.z != 0) R[S.f2.z].rgb = rsm;\n"
"      if (S.f3.x != 0) R[S.f3.x].a = ra0;\n"
"      if (S.f3.y != 0) R[S.f3.y].a = ra1;\n"
"      if (S.f3.z != 0) R[S.f3.z].a = ra2;\n"
"    }\n"
"    if (fin1.w == 0) {\n"
"      o = sat4(R[12]);\n"
"    } else {\n"
"      R[1] = ff0; R[2] = ff1;\n"
"      vec3 E = crgb(fin1.x, R), F = crgb(fin1.y, R);\n"
"      R[15].rgb = E * F;\n"
"      R[14].rgb = R[5].rgb + R[12].rgb;\n"
"      if (misc.z != 0) R[14].rgb = min(R[14].rgb, vec3(1.0));\n"
"      vec3 A = crgb(fin0.x, R), B = crgb(fin0.y, R), C = crgb(fin0.z, R), D = crgb(fin0.w, R);\n"
"      vec3 a = sat3(A);\n"
"      o.rgb = sat3(a * B + (1.0 - a) * C + D);\n"
"      o.a = sat(calp(fin1.z, R));\n"
"    }\n"
"  }\n"
"  if (atest.x != 0) {\n"
"    int a8 = int(sat(o.a) * 255.0 + 0.5), r = atest.z, f = atest.y & 15; bool okp = true;\n"
"    if (f == 0) okp = false; else if (f == 1) okp = a8 < r; else if (f == 2) okp = a8 == r;\n"
"    else if (f == 3) okp = a8 <= r; else if (f == 4) okp = a8 > r; else if (f == 5) okp = a8 != r;\n"
"    else if (f == 6) okp = a8 >= r;\n"
"    if (!okp) discard;\n"
"  }\n"
"  o_color = o;\n"
"}\n";

/* Full-screen triangles: clears, copies, the picture to the window. u_ys is
 * +1 for drawing into a surface (image row 0 at the top, as everything
 * here), -1 for the window, whose rows count from the bottom. */
static const char s_glsl_vs_tri[] =
"#version 330 core\n"
"layout(std140) uniform CLC { vec4 clear_color; };\n"
"uniform float u_ys;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
"  gl_Position = vec4(p.x * 2.0 - 1.0, (p.y * 2.0 - 1.0) * u_ys, 0.0, 1.0);\n"
"#ifdef BLIT\n"
"  v_uv = clear_color.zw + p * clear_color.xy;\n"
"#else\n"
"  v_uv = p * clear_color.xy;\n"
"#endif\n"
"}\n";

static const char s_glsl_ps_tri[] =
"#version 330 core\n"
"layout(std140) uniform CLC { vec4 clear_color; };\n"
"layout(std140) uniform POSTC { vec4 post; };\n"
"uniform sampler2D tx0;\n"
"in vec2 v_uv;\n"
"layout(location = 0) out vec4 o_color;\n"
"vec3 sharp_apply(vec3 c, vec3 n, vec3 s, vec3 w, vec3 e) {\n"
"  vec3 mn = min(c, min(min(n, s), min(w, e))), mx = max(c, max(max(n, s), max(w, e)));\n"
"  vec3 amp = clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0);\n"
"  vec3 k = sqrt(amp) * post.y * 0.25;\n"
"  return clamp(c + k * (4.0 * c - n - s - w - e), mn, mx);\n"
"}\n"
"vec3 post_sharp(vec3 c, vec2 uv) {\n"
"  if (post.y <= 0.0) return c;\n"
"  vec2 rc = clear_color.zw;\n"
"  vec3 n = textureLod(tx0, uv + vec2(0.0, -rc.y), 0.0).rgb, s = textureLod(tx0, uv + vec2(0.0, rc.y), 0.0).rgb;\n"
"  vec3 w = textureLod(tx0, uv + vec2(-rc.x, 0.0), 0.0).rgb, e = textureLod(tx0, uv + vec2(rc.x, 0.0), 0.0).rgb;\n"
"  return sharp_apply(c, n, s, w, e);\n"
"}\n"
/* (1.9) drawn larger than the window: the mean of the texels a pixel
 * covers (nv2a_d3d11_shaders.h) */
"vec2 ds_foot(vec2 uv) { return vec2(abs(dFdx(uv.x)), abs(dFdy(uv.y))) / clear_color.zw; }\n"
"vec3 ds_fetch(vec2 uv, vec2 f) {\n"
"  if (max(f.x, f.y) <= 1.5) return textureLod(tx0, uv, 0.0).rgb;\n"
"  int nx = clamp(int(ceil(f.x * 0.5)), 1, 8), ny = clamp(int(ceil(f.y * 0.5)), 1, 8);\n"
"  vec3 a = vec3(0.0);\n"
"  for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)\n"
"    a += textureLod(tx0, uv + (vec2((float(x) + 0.5) / float(nx), (float(y) + 0.5) / float(ny)) - 0.5) * f * clear_color.zw, 0.0).rgb;\n"
"  return a / float(nx * ny);\n"
"}\n"
"vec3 ds_present(vec2 uv, vec2 f) {\n"
"  vec3 c = ds_fetch(uv, f);\n"
"  if (post.y > 0.0) {\n"
"    vec2 st = f * clear_color.zw;\n"
"    c = sharp_apply(c, ds_fetch(uv - vec2(0.0, st.y), f), ds_fetch(uv + vec2(0.0, st.y), f), ds_fetch(uv - vec2(st.x, 0.0), f), ds_fetch(uv + vec2(st.x, 0.0), f));\n"
"  }\n"
"  return c;\n"
"}\n"
"vec3 post_grade(vec3 c) {\n"
"  c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / post.x));\n"
"  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
"  return clamp(mix(vec3(l), c, post.z), 0.0, 1.0);\n"
"}\n"
"float fx_luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
"void main() {\n"
"#if defined(CLEAR)\n"
"  o_color = clear_color;\n"
"#elif defined(PRESENT)\n"
"  vec2 f = ds_foot(v_uv);\n"
"  if (max(f.x, f.y) > 1.5) o_color = vec4(post_grade(ds_present(v_uv, f)), 1.0);\n"
"  else { vec3 c = texture(tx0, v_uv).rgb; o_color = vec4(post_grade(post_sharp(c, v_uv)), 1.0); }\n"
"#elif defined(FXAA)\n"
"  vec2 rc = clear_color.zw, uv = v_uv;\n"
"  vec2 f = ds_foot(uv);\n"
"  if (max(f.x, f.y) > 1.5) { o_color = vec4(post_grade(ds_present(uv, f)), 1.0); return; }\n"
"  vec3 nw = textureLod(tx0, uv + vec2(-1.0, -1.0) * rc, 0.0).rgb;\n"
"  vec3 ne = textureLod(tx0, uv + vec2( 1.0, -1.0) * rc, 0.0).rgb;\n"
"  vec3 sw = textureLod(tx0, uv + vec2(-1.0,  1.0) * rc, 0.0).rgb;\n"
"  vec3 se = textureLod(tx0, uv + vec2( 1.0,  1.0) * rc, 0.0).rgb;\n"
"  vec3 m  = textureLod(tx0, uv, 0.0).rgb;\n"
"  float lnw = fx_luma(nw), lne = fx_luma(ne), lsw = fx_luma(sw), lse = fx_luma(se), lm = fx_luma(m);\n"
"  float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
"  float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
"  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));\n"
"  float red = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);\n"
"  float rmin = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);\n"
"  dir = clamp(dir * rmin, -8.0, 8.0) * rc;\n"
"  vec3 a = 0.5 * (textureLod(tx0, uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb + textureLod(tx0, uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb);\n"
"  vec3 b = a * 0.5 + 0.25 * (textureLod(tx0, uv - dir * 0.5, 0.0).rgb + textureLod(tx0, uv + dir * 0.5, 0.0).rgb);\n"
"  float lb = fx_luma(b);\n"
"  vec3 r = (lb < lmin || lb > lmax) ? a : b;\n"
"  if (post.y > 0.0) r = clamp(r + post.y * 0.3 * (r - 0.25 * (nw + ne + sw + se)), min(m, min(min(nw, ne), min(sw, se))), max(m, max(max(nw, ne), max(sw, se))));\n"
"  o_color = vec4(post_grade(r), 1.0);\n"
"#else\n"
"  o_color = texture(tx0, v_uv);\n"
"#endif\n"
"}\n";

/* A shader object from sources (concatenated after an optional #define
 * line), compiled; 0 on failure (logged). Waits for the result. */
static unsigned g_mg_n[4];            /* (v116: joinable draws, gpu_batch_hw_direct) */
static unsigned g_jn_joined, g_jn_runs;   /* (v117: draws joined) */
static unsigned g_shc_n; static uint64_t g_shc_us;   /* (v116: shader compiles and their time) */
static GLuint gpu_shader_now(GLenum type, const char *const *src, int n, const char *what)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, n, src, NULL);
#if defined(__vita__)
    { SceUInt64 t0 = sceKernelGetProcessTimeWide(); glCompileShader(s); g_shc_us += sceKernelGetProcessTimeWide() - t0; g_shc_n++; }
#else
    glCompileShader(s);
#endif
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] %s did not compile: %s\n", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* Bind the uniform blocks and samplers of a linked program to the fixed
 * binding points and texture units. */
static void gpu_prog_bindings(GLuint p)
{
    static const char *ub[5] = { "VSC", "PSC", "CLC", "POSTC", "VPC" };
    int i;
    for (i = 0; i < 5; i++) {
        GLuint bi = glGetUniformBlockIndex(p, ub[i]);
        if (bi != GL_INVALID_INDEX) glUniformBlockBinding(p, bi, (GLuint)i);
    }
    glUseProgram(p);
    for (i = 0; i < 4; i++) {
        char nm[8];
        GLint loc;
        snprintf(nm, sizeof nm, "tx%d", i);
        loc = glGetUniformLocation(p, nm);
#if defined(__vita__)
        if (loc != -1 && loc != 0) glUniform1i(loc, i);   /* (vitaGL: see vgl_ub_flush) */
#else
        if (loc >= 0) glUniform1i(loc, i);
#endif
    }
    g_sh.prog = 0;
}

static GLuint gpu_link_now(GLuint vs, GLuint fs, const char *what)
{
    GLuint p = glCreateProgram();
    GLint ok = 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] %s did not link: %s\n", what, log);
        glDeleteProgram(p);
        return 0;
    }
    gpu_prog_bindings(p);
    return p;
}

static GLuint g_vs_main, g_fs_main, g_pr_main;
static int g_parallel = 0;      /* KHR_parallel_shader_compile */

/* A full-screen-triangle program (copy, present, fxaa, blit, clear). */
static GLuint gpu_tri_program(const char *vs_def, const char *fs_def, GLint *ys_loc, const char *what)
{
    const char *vsrc[3], *fsrc[3];
    GLuint vs, fs, p;
#if defined(__vita__)
    vsrc[0] = ""; vsrc[1] = vs_def; vsrc[2] = s_cg_vs_tri;
    fsrc[0] = ""; fsrc[1] = fs_def; fsrc[2] = s_cg_ps_tri;
#else
    vsrc[0] = "#version 330 core\n"; vsrc[1] = vs_def; vsrc[2] = s_glsl_vs_tri + strlen("#version 330 core\n");
    fsrc[0] = "#version 330 core\n"; fsrc[1] = fs_def; fsrc[2] = s_glsl_ps_tri + strlen("#version 330 core\n");
#endif
    vs = gpu_shader_now(GL_VERTEX_SHADER, vsrc, 3, what);
    fs = vs ? gpu_shader_now(GL_FRAGMENT_SHADER, fsrc, 3, what) : 0;
    if (!vs || !fs) { if (vs) glDeleteShader(vs); return 0; }
    p = gpu_link_now(vs, fs, what);
    glDeleteShader(vs); glDeleteShader(fs);
    if (p && ys_loc) *ys_loc = glGetUniformLocation(p, "u_ys");
#if defined(__vita__)
    if (p) vgl_mark_tri(p);
#endif
    return p;
}

/* ---------------- generated programs (vertex and combiner) ------------- */

/* The program for a (vertex shader, fragment shader) pair. D3D binds the two
 * stages separately; GL links them into one program, so every pair a draw
 * uses gets one, linked once and kept -- in the background where the driver
 * offers it (KHR_parallel_shader_compile), the general shaders drawing until
 * it is ready. */
typedef struct {
    GLuint vs, fs, prog;
    int state;                  /* 0 linking, 1 ready, 2 failed */
} GPair;
#define GPAIR_MAX 4096
static GPair *g_pair;
static int g_pair_n;
static uint32_t g_pair_hash_tab[8192];          /* index + 1, open addressing */

#if defined(__vita__)
static void shc_save(GLuint prog, GLuint vs, GLuint fs);
#endif
static int gpu_pair_ready(GPair *e)
{
    GLint ok = 0;
    if (e->state != 0) return e->state == 1;
    if (g_parallel) {
        GLint done = 0;
        glGetProgramiv(e->prog, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return 0;
    }
    glGetProgramiv(e->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        static int said;
        if (said++ < 3) {
            char log[2048];
            glGetProgramInfoLog(e->prog, sizeof log, NULL, log);
            fprintf(stderr, "  [GL] a generated program did not link: %s\n", log);
        }
        glDeleteProgram(e->prog);
        e->prog = 0;
        e->state = 2;
        return 0;
    }
    gpu_prog_bindings(e->prog);
    e->state = 1;
#if defined(__vita__)
    shc_save(e->prog, e->vs, e->fs);
#endif
    return 1;
}

/* The ready program for vs + fs, or 0 (then started, if it was not). */
static GLuint gpu_pair(GLuint vs, GLuint fs)
{
    uint32_t h = (vs * 2654435761u) ^ (fs * 40503u), i;
    GPair *e;
    if (!vs || !fs) return 0;
    if (!g_pair) { g_pair = (GPair *)calloc(GPAIR_MAX, sizeof *g_pair); if (!g_pair) return 0; }
    for (i = 0; i < 8192; i++) {
        uint32_t slot = (h + i) & 8191, ix = g_pair_hash_tab[slot];
        if (!ix) break;
        e = &g_pair[ix - 1];
        if (e->vs == vs && e->fs == fs) return gpu_pair_ready(e) ? e->prog : 0;
    }
    if (i == 8192 || g_pair_n >= GPAIR_MAX) return 0;
    e = &g_pair[g_pair_n++];
    e->vs = vs; e->fs = fs;
    e->prog = glCreateProgram();
    glAttachShader(e->prog, vs);
    glAttachShader(e->prog, fs);
    glLinkProgram(e->prog);
    e->state = 0;
    g_pair_hash_tab[(h + i) & 8191] = (uint32_t)g_pair_n;
    if (!g_parallel) return gpu_pair_ready(e) ? e->prog : 0;
    return 0;
}

#if defined(__vita__)
/* (v117) THE SHADERS KEPT ON THE CARD. vitaGL's shader cache is for its own
 * fixed-function shaders: ours were compiled again on every run, 250-475 ms
 * each (v116 log) -- the game standing still when a level first shows a
 * material. Now the compiled shader (vitaGL's glGetProgramBinary holds the
 * two of a linked program, each as glShaderBinary takes it: count, parameter
 * indices, the GXP) is written to shader_cache/s_<hash of the source>.bin
 * and read from there the next time. RECOMP_VGL_SHC=0: off. */
#define SHC_MAX 2048
static struct { GLuint id; uint64_t h; uint8_t cached, saved; } g_shc[SHC_MAX];
static int g_shc_cnt, g_shc_mode = -1;
static unsigned g_shc_hit, g_shc_written;
static int shc_on(void)
{
    if (g_shc_mode < 0) { const char *e = getenv("RECOMP_VGL_SHC"); g_shc_mode = !(e && e[0] == '0'); }
    return g_shc_mode;
}
static uint64_t shc_hash(GLenum type, const char *src)
{
    uint64_t h = 1469598103934665603ull ^ (uint64_t)type ^ 0x7631ull;
    for (; *src; src++) { h ^= (uint8_t)*src; h *= 1099511628211ull; }
    return h;
}
static void shc_path(char *o, size_t n, uint64_t h, const char *ext)
{
    const char *d = getenv("XBOXRECOMP_VITA_DIR");
    if (!d || !*d) d = "ux0:data/twotowers";
    snprintf(o, n, "%s/shader_cache/t_%08x%08x.%s", d, (unsigned)(h >> 32), (unsigned)h, ext);   /* (v118: t_, the format with the uniform blocks) */
}
static int shc_find(GLuint id)
{
    int i;
    for (i = g_shc_cnt - 1; i >= 0; i--) if (g_shc[i].id == id) return i;
    return -1;
}
static void shc_note(GLuint id, uint64_t h, int cached)
{
    int i = shc_find(id);
    if (i < 0) { if (g_shc_cnt >= SHC_MAX) return; i = g_shc_cnt++; }
    g_shc[i].id = id; g_shc[i].h = h; g_shc[i].cached = (uint8_t)cached; g_shc[i].saved = (uint8_t)cached;
}
/* (v118) THE UNIFORM BLOCKS. vitaGL's serialized shader leaves out the list
 * glCompileShader makes of a shader's uniform blocks (shader + 816: nodes of
 * name[128], buffer index, next) -- ours all have blocks (the vertex
 * constants), and a shader read back had whatever was in that field: the
 * v117 crash in malloc at the first draw of the second run. The list is
 * kept in the file too (after the header) and put back after
 * glShaderBinary. vitaGL's shader table is not exported: it is found from
 * is_shark_online, 0x67004 before it in the same object's data (checked at
 * each use: the entry must be a shader of the type asked for). */
#define SHC_MAGIC 0x32535454u   /* "TTS2" */
typedef struct { char name[128]; uint8_t idx; uint8_t pad[3]; void *next; } VglUBlock;
static uint8_t *shc_struct(GLuint id, GLenum type)
{
    extern uint8_t is_shark_online;
    extern void *vglMalloc(uint32_t size);
    uint8_t **tab = (uint8_t **)((uintptr_t)&is_shark_online + 0x67004u);
    uint8_t *sp;
    uint32_t ty;
    (void)vglMalloc;
    if (!id || id > 2048) return NULL;
    sp = tab[id - 1];
    if ((uintptr_t)sp < 0x81000000u) return NULL;
    memcpy(&ty, sp, 4);
    return ty == (uint32_t)type ? sp : NULL;
}
/* a serialized shader that looks right: count, indices, "GXP" */
static int shc_valid(const uint8_t *b, size_t len)
{
    uint32_t cnt; size_t off;
    if (len < 16) return 0;
    memcpy(&cnt, b, 4);
    if (cnt > 1024) return 0;
    off = 4 + (size_t)cnt * 4;
    if (off + 8 > len) return 0;
    return b[off] == 'G' && b[off + 1] == 'X' && b[off + 2] == 'P' && b[off + 3] == 0;
}
static GLuint shc_load(GLenum type, uint64_t h)
{
    char path[320];
    FILE *f;
    long len;
    uint8_t *b;
    GLuint s = 0;
    shc_path(path, sizeof path, h, "bin");
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len < 16 || len > 512 * 1024) { fclose(f); return 0; }
    b = (uint8_t *)malloc((size_t)len);
    if (b && fread(b, 1, (size_t)len, f) == (size_t)len) {
        uint32_t magic, nblk;
        memcpy(&magic, b, 4); memcpy(&nblk, b + 4, 4);
        if (magic == SHC_MAGIC && nblk <= 16 && (size_t)8 + (size_t)nblk * 132 + 16 <= (size_t)len
            && shc_valid(b + 8 + nblk * 132, (size_t)len - 8 - nblk * 132)) {
            s = glCreateShader(type);
            if (s) {
                uint8_t *sp = shc_struct(s, type);
                if (!sp) { glDeleteShader(s); s = 0; }
                else {
                    extern void *vglMalloc(uint32_t size);
                    void *head = NULL;
                    int q;
                    glShaderBinary(1, &s, 0, b + 8 + nblk * 132, (GLsizei)((size_t)len - 8 - nblk * 132));
                    for (q = (int)nblk - 1; q >= 0; q--) {   /* (the file has them head first) */
                        VglUBlock *nb = (VglUBlock *)vglMalloc(sizeof *nb);
                        if (!nb) break;
                        memset(nb, 0, sizeof *nb);
                        memcpy(nb->name, b + 8 + q * 132, 127);
                        nb->idx = b[8 + q * 132 + 128];
                        nb->next = head; head = nb;
                    }
                    memcpy(sp + 816, &head, 4);
                    shc_note(s, h, 1); g_shc_hit++;
                }
            }
        }
    }
    free(b);
    fclose(f);
    return s;
}
static void shc_write(GLuint id, GLenum type, uint64_t h, const uint8_t *b, size_t len)
{
    char path[320], tmp[320];
    FILE *f;
    uint8_t hdr[8 + 16 * 132];
    uint32_t nblk = 0, magic = SHC_MAGIC;
    uint8_t *sp = shc_struct(id, type);
    VglUBlock *ub;
    if (!sp || !shc_valid(b, len)) return;
    memcpy(&ub, sp + 816, 4);
    memset(hdr, 0, sizeof hdr);
    for (; ub; ub = (VglUBlock *)ub->next) {
        if (nblk >= 16 || (uintptr_t)ub < 0x81000000u) return;   /* (not a list we know: not kept) */
        memcpy(hdr + 8 + nblk * 132, ub->name, 127);
        hdr[8 + nblk * 132 + 128] = ub->idx;
        nblk++;
    }
    memcpy(hdr, &magic, 4); memcpy(hdr + 4, &nblk, 4);
    shc_path(path, sizeof path, h, "bin");
    shc_path(tmp, sizeof tmp, h, "tmp");
    f = fopen(tmp, "wb");
    if (!f) return;
    if (fwrite(hdr, 1, 8 + nblk * 132, f) != 8 + nblk * 132 || fwrite(b, 1, len, f) != len) { fclose(f); remove(tmp); return; }
    fclose(f);
    remove(path);
    if (rename(tmp, path) == 0) g_shc_written++;
}
/* a program linked: its two shaders to the card, if they are not there */
static void shc_save(GLuint prog, GLuint vs, GLuint fs)
{
    static uint8_t *buf;
    enum { CAP = 512 * 1024 };
    int iv, ifs;
    GLsizei len = 0;
    GLenum fmt = 0;
    uint32_t vsz, fsz;
    if (!shc_on()) return;
    iv = shc_find(vs); ifs = shc_find(fs);
    if ((iv < 0 || g_shc[iv].saved) && (ifs < 0 || g_shc[ifs].saved)) return;
    if (!buf) buf = (uint8_t *)malloc(CAP);
    if (!buf) return;
    glGetProgramBinary(prog, CAP, &len, &fmt, buf);
    if (len < 148 || len > CAP) return;
    memcpy(&vsz, buf + 132, 4);
    if ((size_t)136 + vsz + 4 > (size_t)len) return;
    memcpy(&fsz, buf + 136 + vsz, 4);
    if ((size_t)136 + vsz + 4 + fsz > (size_t)len) return;
    if (iv >= 0 && !g_shc[iv].saved) { shc_write(vs, GL_VERTEX_SHADER, g_shc[iv].h, buf + 136, vsz); g_shc[iv].saved = 1; }
    if (ifs >= 0 && !g_shc[ifs].saved) { shc_write(fs, GL_FRAGMENT_SHADER, g_shc[ifs].h, buf + 136 + vsz + 4, fsz); g_shc[ifs].saved = 1; }
}
#endif

/* A shader object compiled (asynchronously where the driver can); state
 * read later with gpu_shader_done. */
static GLuint gpu_shader_start(GLenum type, const char *src)
{
#if defined(__vita__)
    uint64_t shh = 0;
    if (shc_on()) {
        GLuint c;
        shh = shc_hash(type, src);
        c = shc_load(type, shh);
        if (c) { vgl_shader_note_src(c, src); return c; }
    }
#endif
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
#if defined(__vita__)
    { SceUInt64 t0 = sceKernelGetProcessTimeWide(); glCompileShader(s); g_shc_us += sceKernelGetProcessTimeWide() - t0; g_shc_n++; }
    if (shc_on()) shc_note(s, shh, 0);
#else
    glCompileShader(s);
#endif
    return s;
}
/* 1 compiled, 0 still compiling, -1 failed (the object is deleted) */
static int gpu_shader_done(GLuint *s, const char *what, uint32_t id)
{
    GLint ok = 0;
#if defined(__vita__)
    { int ci = shc_find(*s); if (ci >= 0 && g_shc[ci].cached) return 1; }   /* (v117: read from the card) */
#endif
    if (g_parallel) {
        GLint done = 0;
        glGetShaderiv(*s, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return 0;
    }
    glGetShaderiv(*s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static int said;
        if (said++ < 3) {
            char log[2048];
            glGetShaderInfoLog(*s, sizeof log, NULL, log);
            fprintf(stderr, "  [GL] %s %08X did not compile: %s\n", what, id, log);
        }
        glDeleteShader(*s);
        *s = 0;
        return -1;
    }
    return 1;
}

/* ---------------- init ---------------- */

static void gpu_apply_settings(void);
static int gpu_init_fail(void)
{
#if defined(__vita__)
    fprintf(stderr, "  [VGL] the GPU renderer could not start: back to the CPU picture\n");
    fflush(stderr);
    xbox_VitaGLRelease();
#else
    SDL_GL_MakeCurrent(g_win, NULL);
#endif
    return 0;
}
#if defined(__vita__)
static void vgl_surface_test(void);
static void vgl_uniform_test(void);
#endif
static int gpu_init(void)
{
    const char *e = getenv("RECOMP_RENDERER");
    GLint maj = 0, mnr = 0;
    if (e && !strcasecmp(e, "software")) return 0;
#if defined(__vita__)
    (void)maj; (void)mnr;
    vgl_tr_init();
    vgl_trf("gpu_init (build v13): asking for vitaGL");
    if (!xbox_VitaGLAcquire()) {
        fprintf(stderr, "  [VGL] vitaGL belongs to the picture thread: software renderer\n");
        return 0;
    }
    g_win = (SDL_Window *)(uintptr_t)1;   /* (no SDL window: vitaGL draws to the display) */
    fprintf(stderr, "  [VGL] GPU renderer (v231) on %s / %s; shaders compiled when first used (cached)\n",
            (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER));
    if (0)
#endif
    {
    g_glc = (SDL_GLContext)xbox_FramebufferGLContext(10000);
    g_win = (SDL_Window *)xbox_FramebufferHwnd();
    if (!g_win || !g_glc) {
        fprintf(stderr, "  [GL] no OpenGL 3.3 context; using the software renderer\n");
        return 0;
    }
    if (SDL_GL_MakeCurrent(g_win, g_glc) != 0) {
        fprintf(stderr, "  [GL] cannot use the OpenGL context (%s); using the software renderer\n", SDL_GetError());
        return 0;
    }
    if (!gl_load()) { SDL_GL_MakeCurrent(g_win, NULL); return 0; }
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &mnr);
    fprintf(stderr, "  [GL] %s / %s / OpenGL %s\n", (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    if (maj < 3 || (maj == 3 && mnr < 3)) {
        fprintf(stderr, "  [GL] OpenGL 3.3 is needed; using the software renderer\n");
        SDL_GL_MakeCurrent(g_win, NULL);
        return 0;
    }
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &g_ubo_align);
    if (g_ubo_align < 16) g_ubo_align = 16;
    if (gl_has_ext("GL_EXT_texture_filter_anisotropic") || gl_has_ext("GL_ARB_texture_filter_anisotropic"))
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &g_max_aniso);
    if (!getenv("RECOMP_GL_SYNC_COMPILE")
        && (gl_has_ext("GL_KHR_parallel_shader_compile") || gl_has_ext("GL_ARB_parallel_shader_compile"))) {
        *(void **)&p_glMaxShaderCompilerThreadsKHR = SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
        if (!p_glMaxShaderCompilerThreadsKHR)
            *(void **)&p_glMaxShaderCompilerThreadsKHR = SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsARB");
        if (p_glMaxShaderCompilerThreadsKHR) p_glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
        g_parallel = 1;
    }
    fprintf(stderr, "  [GL] shaders compiled %s; anisotropic filtering up to x%.0f\n",
            g_parallel ? "in the background" : "when first used", g_max_aniso);
    }

    {
#if defined(__vita__)
        const char *vs[1] = { s_cg_vs_main };
        const char *fs[2] = { "", s_cg_ps_main };
#else
        const char *vs[1] = { s_glsl_vs_main };
        const char *fs[2] = { s_glsl_ps_head, s_glsl_ps_main };
#endif
        g_vs_main = gpu_shader_now(GL_VERTEX_SHADER, vs, 1, "the main vertex shader");
        g_fs_main = gpu_shader_now(GL_FRAGMENT_SHADER, fs, 2, "the combiner shader");
        if (!g_vs_main || !g_fs_main) return gpu_init_fail();
        g_pr_main = gpu_link_now(g_vs_main, g_fs_main, "the main program");
        if (!g_pr_main) return gpu_init_fail();
    }
    g_pr_copy = gpu_tri_program("", "", &g_loc_ys_copy, "the copy shader");
    g_pr_present = gpu_tri_program("", "#define PRESENT\n", &g_loc_ys_present, "the present shader");
#if defined(__vita__)
    g_pr_fxaa = 0;   /* (no derivatives in the Vita's fragment shaders) */
#else
    g_pr_fxaa = gpu_tri_program("", "#define FXAA\n", &g_loc_ys_fxaa, "the FXAA shader");
#endif
    g_pr_blit = gpu_tri_program("#define BLIT\n", "", &g_loc_ys_blit, "the blit shader");
    g_pr_clear = gpu_tri_program("", "#define CLEAR\n", NULL, "the clear shader");
    if (!g_pr_copy || !g_pr_present || !g_pr_blit || !g_pr_clear) return gpu_init_fail();
    if (g_pr_clear) { glUseProgram(g_pr_clear); glUniform1f(glGetUniformLocation(g_pr_clear, "u_ys"), 1.0f); }

    glGenVertexArrays(1, &g_vao);
    glGenVertexArrays(1, &g_vao_x);
    glGenVertexArrays(1, &g_vao_empty);
    {
        /* what a stage reads when its texture is the target being drawn
         * (D3D unbinds such a view, and reads give 0) */
        uint32_t z = 0;
        g_tex_black = gpu_tex2d(1, 1, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &z);
    }
    {
        const char *rs = getenv("RECOMP_RENDER_SCALE");
#if defined(__vita__)
        g_msaa = 1;
#else
        g_msaa = xbox_GfxMsaa();
#endif
        if (g_msaa > 1) fprintf(stderr, "  [GL] MSAA %dx\n", g_msaa);
        g_rsf = rs ? (float)atof(rs) : 1.0f;
        if (!(g_rsf >= 1.0f)) g_rsf = 1.0f;
#if defined(__vita__)
        g_rsf = vita_res_scale();
#endif
        if (g_rsf > 12.0f) g_rsf = 12.0f;
        g_rs = (int)g_rsf;
        {
            const char *as = getenv("RECOMP_ASPECT");
            float a = as ? (float)atof(as) : 0.0f;
            if (a > 1.34f && a < 6.0f) g_aspect = a;
#if defined(__vita__)
            {   /* (v199) TT_WIDE=1: the picture's shape from the start -- the settings pass that reads it never runs on the Vita (v198: the game's cameras widened, the picture not) */
                float w = xbox_GfxAspect();
                if (w > 1.34f && w < 6.0f) g_aspect = w;   /* (v201: whatever RECOMP_ASPECT says -- the title always sets it from the ini, "4:3", and v200 stayed 4:3) */
                fprintf(stderr, "  [GL] picture %.3f:1\n", g_aspect);
                { extern char s_envlog[] __attribute__((weak)); fprintf(stderr, "  [ENV] env.txt:%s\n", s_envlog && s_envlog[0] ? s_envlog : " none read"); }
            }
#endif
        }
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
        if (g_rsf > 1.0f || g_aspect > 1.34f)
            fprintf(stderr, "  [GL] rendering at %.2f x %.2f the original resolution, picture %.3f:1\n",
                    g_rsx, g_rsy, g_aspect);
    }
    {
        const char *v = getenv("RECOMP_HW_VP");
        g_hwvp_user = !(v && v[0] == '0');
    }
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
#if defined(__vita__)
    {   /* (v167) the present sharpens by default on the Vita (the 85% picture stretched to the screen looked soft):
         * TT_SHARP=0..2 (0 off; default 0.8) */
        const char *e = getenv("TT_SHARP");
        if (e) g_post[1] = (float)atof(e); else if (g_post[1] <= 0.0f) g_post[1] = 0.8f;
        if (g_post[1] < 0.0f) g_post[1] = 0.0f;
        if (g_post[1] > 2.0f) g_post[1] = 2.0f;
    }
#endif
    g_present_smp = gpu_sampler(3, 3, 1);
    g_copy_smp = g_present_smp;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gpu_sh_reset();
#if defined(__vita__)
    /* (v75) the start-up tests only with RECOMP_VGL_TESTS=1: they always
     * gave the same answers (surfaces as OpenGL, the uniforms right), and
     * v74 once stuck in the first one's clear, waiting on the GPU */
    {
        { extern int g_vgl_timers; const char *t = getenv("RECOMP_VGL_TIMERS"); g_vgl_timers = t && t[0] == '1'; }
        const char *e = getenv("RECOMP_VGL_TESTS");
        if (e && e[0] == '1') {
            vgl_surface_test();
            gpu_sh_reset();
            vgl_uniform_test();
            gpu_sh_reset();
        } else g_vgl_flip = 0;
    }
#endif
    fprintf(stderr, "  [GL] renderer up\n");
    fflush(stderr);
    return 1;
}

#if defined(__vita__)
/* Which way up vitaGL fills a framebuffer object (see nv2a_gl_vita.inc):
 * a 64x64 texture cleared green, the lower half in OpenGL's terms (clip
 * y < 0) drawn red, then its memory's first pixel read. OpenGL puts clip
 * y = -1 at row 0: red there. RECOMP_VGL_FLIP=0/1 skips the test. */
static void vgl_surface_test(void)
{
    const char *e = getenv("RECOMP_VGL_FLIP");
    GLuint t, f;
    float v[6][32];
    static const float q[6][2] = { {0, 0}, {64, 0}, {64, 32}, {0, 0}, {64, 32}, {0, 32} };
    float c[4] = { 2.0f / 64.0f, 2.0f / 64.0f, 0.5f, 1.0f };
    const uint8_t *px;
    int i, k;
    if (e && (e[0] == '0' || e[0] == '1')) {
        g_vgl_flip = e[0] == '1';
        fprintf(stderr, "  [VGL] surfaces %s (RECOMP_VGL_FLIP)\n", g_vgl_flip ? "flipped" : "as OpenGL");
        return;
    }
    g_vgl_flip = 0;
    t = gpu_tex2d(64, 64, 0);
    f = gpu_fbo(t, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glViewport(0, 0, 64, 64);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    memset(v, 0, sizeof v);
    for (i = 0; i < 6; i++) {
        v[i][0] = q[i][0]; v[i][1] = q[i][1]; v[i][2] = 1.0f; v[i][3] = 1.0f;   /* window x, y, z (clip z 0); w = 1 */
        v[i][4] = 1.0f; v[i][5] = 0.0f; v[i][6] = 0.0f; v[i][7] = 1.0f;         /* diffuse: red */
    }
    gpu_ub(UB_VSC, c, 16);
    glUseProgram(g_pr_main);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (k = 0; k < 8; k++) {
        glEnableVertexAttribArray(k);
        glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, sizeof v[0], &v[0][4 * k]);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFinish();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t);
    px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
    if (!px) fprintf(stderr, "  [VGL] surface test: no texture memory; surfaces taken as OpenGL's\n");
    else {
        int red = (px[0] > 0x80 || px[2] > 0x80) && px[1] < 0x80, green = px[1] > 0x80 && px[0] < 0x80 && px[2] < 0x80;
        g_vgl_flip = green && !red;
        fprintf(stderr, "  [VGL] surface test: row 0 = %02X %02X %02X %02X -> %s\n", px[0], px[1], px[2], px[3],
                red ? "as OpenGL" : green ? "flipped (compensated)" : "unclear (taken as OpenGL)");
    }
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) fprintf(stderr, "  [VGL] GL error 0x%04X during start-up\n", err);
    }
    gpu_fbo_forget(t);
    glDeleteTextures(1, &t);
    fflush(stderr);
}
#endif


#if defined(__vita__)
#include "nv2a_gl_vtest.inc"
/* (diagnostics) a real translated program (269174D4, from a v23 dump) with
 * its own constants and first vertex: red if the GPU computes the clip
 * position the CPU path does, yellow if only w matches, blue if not */
static void vgl_real_vp_test(GLuint f, GLuint t, GLuint fs)
{
    static float rows[192][4], v[6][16];
    static const float q[6][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1} };
    float vpv[4] = { 0.003125f, 0.00416667f, 5.96047e-08f, 1.0f };
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), p;
    const char *src = s_t_real_vs;
    const uint8_t *px;
    GLint ok = 0;
    int i, k;
    memset(rows, 0, sizeof rows);
    for (i = 0; i < (int)(sizeof s_t_real_c / sizeof s_t_real_c[0]); i++) memcpy(rows[s_t_real_c[i].i], s_t_real_c[i].v, 16);
    for (i = 0; i < 6; i++) {
        memcpy(v[i], s_t_real_in, 48);
        v[i][12] = q[i][0]; v[i][13] = q[i][1]; v[i][14] = 0; v[i][15] = 1;
    }
    glShaderSource(vs, 1, &src, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { fprintf(stderr, "  [VGL] real program test: did not compile\n"); glDeleteShader(vs); return; }
    p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    gpu_ub(UB_VSC, vpv, 16);
    gpu_ub(UB_VPC, rows, sizeof rows);
    glUseProgram(p);
    glBindVertexArray(g_vao_x);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (k = 0; k < 16; k++) {
        if (k < 4) { glEnableVertexAttribArray(k); glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, sizeof v[0], &v[0][4 * k]); }
        else glDisableVertexAttribArray(k);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFinish();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t);
    px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
    if (px) {
        const uint8_t *c = px + (32 * 64 + 32) * 4;
        fprintf(stderr, "  [VGL] real program test (269174D4): centre %02X %02X %02X %02X -> %s\n", c[0], c[1], c[2], c[3],
                c[0] > 0x80 && c[1] < 0x80 && c[2] < 0x80 ? "RIGHT (red)" :
                c[0] > 0x80 && c[1] > 0x80 && c[2] < 0x80 ? "only w right (yellow)" :
                c[2] > 0x80 && c[0] < 0x80 ? "wrong (blue)" :
                c[1] > 0x80 && c[0] < 0x80 ? "not drawn (green)" : !c[0] && !c[1] && !c[2] ? "zero (black)" : "other");
    }
    glUseProgram(0);
    glDeleteProgram(p);
    glDeleteShader(vs);
    g_sh.prog = 0;
}

/* (diagnostics) how a vertex shader's uniform array reaches the GPU: small
 * programs that take a full-screen quad's colour from constants, drawn into a
 * 64x64 texture, the centre pixel read. Red = right. [VGL] uniform test */
static void vgl_uniform_test(void)
{
    static const struct { const char *name, *vs; int how; } T[] = {
        { "c[192], c[100] static, real upload path", "uniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = c[100]; }\n", 0 },
        { "c[192], c[0] static", "uniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = c[0]; }\n", 0 },
        { "c[192], c[191] static", "uniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = c[191]; }\n", 0 },
        { "c[192], c[(int)vin1.x] dynamic (100)", "uniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = c[(int)vin1.x]; }\n", 0 },
        { "c[192] + vp, c[100] static", "uniform float4 vp;\nuniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0 * vp.w; v_d0 = c[100]; }\n", 0 },
        { "c[192] + vp, position from c[96]", "uniform float4 vp;\nuniform float4 c[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = float4(dot(vin0, c[96]), dot(vin0, c[97]), 0.0, 1.0); v_d0 = c[100]; }\n", 0 },
        { "q[192] glUniform4fv(192) direct", "uniform float4 q[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = q[100]; }\n", 1 },
        { "q[192] glUniform4fv(\"q[100]\", 1)", "uniform float4 q[192];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = q[100]; }\n", 2 },
        { "q[8] glUniform4fv(8), q[5]", "uniform float4 q[8];\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = q[5]; }\n", 3 },
        { "k100 single uniform", "uniform float4 k100;\n"
          "void main(float4 vin0, float4 vin1, float4 out gl_Position : POSITION, float4 out v_d0 : TEXCOORD4) {\n"
          "  gl_Position = vin0; v_d0 = k100; }\n", 4 },
    };
    static float rows[192][4];
    static const float q[6][8] = {
        { -1, -1, 0, 1, 100, 0, 0, 0 }, { 1, -1, 0, 1, 100, 0, 0, 0 }, { 1, 1, 0, 1, 100, 0, 0, 0 },
        { -1, -1, 0, 1, 100, 0, 0, 0 }, { 1, 1, 0, 1, 100, 0, 0, 0 }, { -1, 1, 0, 1, 100, 0, 0, 0 } };
    float vpv[4] = { 1, 1, 1, 1 };
    GLuint t, f, fs;
    int i, k;
    for (i = 0; i < 192; i++) { rows[i][0] = 0; rows[i][1] = 0; rows[i][2] = 1; rows[i][3] = 1; }   /* blue */
    rows[0][0] = rows[100][0] = rows[191][0] = 1; rows[0][2] = rows[100][2] = rows[191][2] = 0;     /* red */
    rows[5][0] = 1; rows[5][2] = 0;
    rows[96][0] = 1; rows[96][1] = 0; rows[96][2] = 0; rows[96][3] = 0;
    rows[97][0] = 0; rows[97][1] = 1; rows[97][2] = 0; rows[97][3] = 0;
    t = gpu_tex2d(64, 64, 0);
    f = gpu_fbo(t, 0);
    fs = glCreateShader(GL_FRAGMENT_SHADER);
    { const char *src = s_cg_ps_main; glShaderSource(fs, 1, &src, NULL); glCompileShader(fs); }
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER), p;
        GLint ok = 0, loc;
        const uint8_t *px;
        const char *src = T[i].vs;
        glShaderSource(vs, 1, &src, NULL);
        glCompileShader(vs);
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok) { fprintf(stderr, "  [VGL] uniform test %d (%s): did not compile\n", i, T[i].name); glDeleteShader(vs); continue; }
        p = glCreateProgram();
        glAttachShader(p, vs); glAttachShader(p, fs);
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { fprintf(stderr, "  [VGL] uniform test %d (%s): did not link\n", i, T[i].name); glDeleteProgram(p); glDeleteShader(vs); continue; }
        glBindFramebuffer(GL_FRAMEBUFFER, f);
        glViewport(0, 0, 64, 64);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        gpu_ub(UB_VSC, vpv, 16);
        gpu_ub(UB_VPC, rows, sizeof rows);
        glUseProgram(p);
        loc = -1;
        switch (T[i].how) {
        case 1: loc = glGetUniformLocation(p, "q"); glUniform4fv(loc, 192, rows[0]); break;
        case 2: loc = glGetUniformLocation(p, "q[100]"); glUniform4fv(loc, 1, rows[100]); break;
        case 3: loc = glGetUniformLocation(p, "q"); glUniform4fv(loc, 8, rows[0]); break;
        case 4: loc = glGetUniformLocation(p, "k100"); glUniform4fv(loc, 1, rows[100]); break;
        default: loc = glGetUniformLocation(p, "c"); break;
        }
        glBindVertexArray(g_vao_x);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (k = 0; k < 16; k++) {
            if (k < 2) { glEnableVertexAttribArray(k); glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, sizeof q[0], &q[0][4 * k]); }
            else glDisableVertexAttribArray(k);
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFinish();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t);
        px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
        if (px) {
            const uint8_t *c = px + (32 * 64 + 32) * 4;
            fprintf(stderr, "  [VGL] uniform test %d (%s): loc %d, centre %02X %02X %02X %02X -> %s\n", i, T[i].name, (int)loc,
                    c[0], c[1], c[2], c[3],
                    c[0] > 0x80 && c[1] < 0x80 && c[2] < 0x80 ? "RIGHT (red)" :
                    c[2] > 0x80 && c[0] < 0x80 ? "wrong row (blue)" :
                    c[1] > 0x80 && c[0] < 0x80 ? "not drawn (green)" :
                    !c[0] && !c[1] && !c[2] ? "zero (black)" : "other");
        }
        glUseProgram(0);
        glDeleteProgram(p);
        glDeleteShader(vs);
        g_sh.prog = 0;
    }
    vgl_real_vp_test(f, t, fs);
    glDeleteShader(fs);
    gpu_fbo_forget(t);
    glDeleteTextures(1, &t);
    { GLenum err = glGetError(); if (err != GL_NO_ERROR) fprintf(stderr, "  [VGL] GL error 0x%04X in the uniform test\n", err); }
    fflush(stderr);
}
#endif

static int gpu_on(void)
{
    if (s_gpu_mode < 0) {
        s_gpu_mode = gpu_init();
        if (s_gpu_mode) xbox_FramebufferSetGpuOwned(1);
    }
    return s_gpu_mode;
}

static void gpu_flush(void) { if (s_gpu_mode > 0) glFlush(); }

/* ---------------- drawing state ---------------- */

static void gpu_set_blend(int write_mask_only_clear)
{
    uint64_t k;
    uint32_t m = s_3d.color_mask;
    int wr = (m & 0x00010000u) != 0, wg = (m & 0x00000100u) != 0, wb = (m & 0x00000001u) != 0, wa = (m & 0x01000000u) != 0;
    int en = s_gpu.blend_enable && !write_mask_only_clear;
    float bf[4];
    uint32_t c = s_3d.blend_color;
    k = ((uint64_t)en << 63) | ((uint64_t)(wr | (wg << 1) | (wb << 2) | (wa << 3)) << 48) | ((uint64_t)s_3d.blend_eq << 32)
      | ((uint64_t)(s_gpu.blend_sfactor & 0xFFFF) << 16) | (s_gpu.blend_dfactor & 0xFFFF);
    bf[0] = ((c >> 16) & 0xFF) / 255.0f; bf[1] = ((c >> 8) & 0xFF) / 255.0f;
    bf[2] = (c & 0xFF) / 255.0f; bf[3] = (c >> 24) / 255.0f;
    if (g_sh.bs_ok && g_sh.blend == k && !memcmp(g_sh.bf, bf, sizeof bf)) return;
    glColorMask(wr, wg, wb, wa);
#if defined(__vita__)
    /* (PS Vita) a draw that writes alpha only: the level's shadows go into
     * the alpha channel (ONE/ONE, MAX) and a full-screen pass then darkens by
     * it. vitaGL's colour mask did not hold them back: the colour went too
     * and the scene under them was wiped (the v53 capture). The colour is
     * kept by the blend itself as well: ZERO/ONE, ADD for RGB. */
    if (!wr && !wg && !wb && wa) {
        uint32_t sf = s_gpu.blend_sfactor & 0xFFFF, df = s_gpu.blend_dfactor & 0xFFFF, eq = s_3d.blend_eq & 0xFFFF;
        if (!en) { sf = GL_ONE; df = GL_ZERO; eq = GL_FUNC_ADD; }
        if (sf >= 0x8001 && sf <= 0x8004) sf = GL_ONE;
        if (df >= 0x8001 && df <= 0x8004) df = GL_ZERO;
        if (eq != 0x800A && eq != 0x800B && eq != 0x8007 && eq != 0x8008) eq = GL_FUNC_ADD;
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ZERO, GL_ONE, sf, df);
        glBlendEquationSeparate(GL_FUNC_ADD, eq);
        { GLenum s2 = GL_ONE, d2 = GL_ZERO; vgl_blend_const(&s2, &d2, bf); }
        g_sh.blend = k; memcpy(g_sh.bf, bf, sizeof bf); g_sh.bs_ok = 1;
        return;
    }
#endif
    if (en) {
        /* the NV2A's factors and equations are GL's own enums */
        static const uint32_t ok_f[] = { 0x0000, 0x0001, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305, 0x0306,
                                         0x0307, 0x0308, 0x8001, 0x8002, 0x8003, 0x8004 };
        uint32_t sf = s_gpu.blend_sfactor & 0xFFFF, df = s_gpu.blend_dfactor & 0xFFFF, eq = s_3d.blend_eq & 0xFFFF;
        size_t i;
        int sok = 0, dok = 0;
        for (i = 0; i < sizeof ok_f / sizeof ok_f[0]; i++) { if (sf == ok_f[i]) sok = 1; if (df == ok_f[i]) dok = 1; }
        if (!sok) sf = GL_ONE;
        if (!dok) df = GL_ONE;
        if (eq != 0x800A && eq != 0x800B && eq != 0x8007 && eq != 0x8008) eq = GL_FUNC_ADD;
#if defined(__vita__)
        { GLenum s2 = sf, d2 = df; vgl_blend_const(&s2, &d2, bf); sf = s2; df = d2; }
#endif
        glEnable(GL_BLEND);
        glBlendFuncSeparate(sf, df, sf, df);
        glBlendEquationSeparate(eq, eq);
        glBlendColor(bf[0], bf[1], bf[2], bf[3]);
    } else {
#if defined(__vita__)
        /* (vitaGL applies the colour mask only with blending on: a masked
         * draw without blending gets ONE/ZERO blending, which is the same) */
        if (!(wr && wg && wb && wa)) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        } else glDisable(GL_BLEND);
        { GLenum s2 = GL_ONE, d2 = GL_ZERO; vgl_blend_const(&s2, &d2, bf); }
#else
        glDisable(GL_BLEND);
#endif
    }
    g_sh.blend = k; memcpy(g_sh.bf, bf, sizeof bf); g_sh.bs_ok = 1;
}

static GLenum gl_sop(uint32_t op)
{
    switch (op) {
    case 0x0000: return GL_ZERO;
    case 0x1E01: return GL_REPLACE;
    case 0x1E02: return GL_INCR;
    case 0x1E03: return GL_DECR;
    case 0x150A: return GL_INVERT;
    case 0x8507: return GL_INCR_WRAP;
    case 0x8508: return GL_DECR_WRAP;
    default:     return GL_KEEP;
    }
}

static void gpu_set_depth(int have_dsv)
{
    uint64_t k[3];
    int dt = have_dsv && s_3d.depth_test, st = have_dsv && s_3d.stencil_test;
    k[0] = ((uint64_t)dt << 63) | ((uint64_t)(dt && s_3d.depth_mask) << 62) | ((uint64_t)st << 61)
         | ((uint64_t)(s_3d.depth_func & 0xF) << 48) | ((uint64_t)(s_3d.st_func & 0xF) << 40)
         | ((uint64_t)(s_3d.st_mask & 0xFF) << 32) | ((uint64_t)(s_3d.st_wmask & 0xFF) << 24)
         | (uint64_t)(s_3d.st_ref & 0xFF);
    k[1] = ((uint64_t)s_3d.st_fail << 32) | s_3d.st_zfail;
    k[2] = s_3d.st_zpass;
    if (g_sh.ds_ok && !memcmp(g_sh.ds, k, sizeof k)) return;
    if (dt) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(0x0200 | (s_3d.depth_func & 0xF));
        glDepthMask(s_3d.depth_mask ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    if (st) {
        glEnable(GL_STENCIL_TEST);
        glStencilFuncSeparate(GL_FRONT_AND_BACK, 0x0200 | (s_3d.st_func & 0xF), (GLint)(s_3d.st_ref & 0xFF), s_3d.st_mask & 0xFF);
        glStencilOpSeparate(GL_FRONT_AND_BACK, gl_sop(s_3d.st_fail), gl_sop(s_3d.st_zfail), gl_sop(s_3d.st_zpass));
        glStencilMask(s_3d.st_wmask & 0xFF);
    } else {
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
    }
    memcpy(g_sh.ds, k, sizeof k); g_sh.ds_ok = 1;
}

static int gpu_set_raster(void)
{
    uint64_t k;
    int cull = 0;
    GLenum face = GL_BACK;
    static int nocull = -1, cullflip;   /* (v96: two getenv a draw) */
    if (nocull < 0) { nocull = getenv("RECOMP_GPU_NOCULL") != NULL; cullflip = getenv("RECOMP_GPU_CULLFLIP") != NULL; }
    if (s_3d.cull_enable && !nocull) {
        if (s_3d.cull_face == 0x408) return 0;        /* front and back: nothing drawn */
        cull = 1;
        face = s_3d.cull_face == 0x404 ? GL_FRONT : GL_BACK;
    }
    /* D3D: front = counter-clockwise unless the title's front face is CW
     * (0x900); see the top of this file for why D3D's clockwise is GL's
     * counter-clockwise here. */
    {
        int fccw = (s_3d.front_face != 0x900) ^ cullflip;
#if defined(__vita__)
        fccw ^= g_vgl_flip;   /* (y flipped back: the winding too) */
#endif
        k = ((uint64_t)cull << 40) | ((uint64_t)face << 8) | (uint64_t)fccw | (1ull << 41);
        if (g_sh.rs_ok && g_sh.rs == k) return 1;
#if defined(__vita__)
        /* (v81) vitaGL (misc.o change_cull_mode) sums front face + cull face
         * (+1 on the display) and only knows 0xD05 / 0xD06: GL_CW with
         * GL_FRONT in a framebuffer object (0xD04) falls to "no polygons" and
         * every draw is dropped until the cull state changes. The same cull
         * said the other way round: front face always CCW, the culled face
         * swapped when the front is CW. */
        if (fccw) { fccw = 0; face = face == GL_FRONT ? GL_BACK : GL_FRONT; }
#endif
        if (cull) { glEnable(GL_CULL_FACE); glCullFace(face); } else glDisable(GL_CULL_FACE);
        glFrontFace(fccw ? GL_CW : GL_CCW);
#if !defined(__vita__)
        if (getenv("RECOMP_GPU_NOZCLIP")) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
#endif
        glEnable(GL_SCISSOR_TEST);
    }
    g_sh.rs = k; g_sh.rs_ok = 1;
    return 1;
}

/* The colour and zeta targets the title has bound, made current. */
static GSurf *g_cur_rt, *g_cur_ds;
static int g_cur_sc[4];                 /* the clip rectangle on g_cur_rt */
#if defined(__vita__)
/* (v74) the targets of the draw before, when nothing they are made from
 * has changed: 4-12% of the render thread went into finding the same two
 * surfaces and framebuffer for every draw (v73) */
static struct {
    int ok, nd;
    uint32_t co, pitch, cx, cy, cw, ch, fmt, zo, runs, gen;
    float cmax;
    GSurf *rt, *ds;
    GLuint fbo;
    int sc[4];
} g_bt;
#endif
static unsigned g_vbs_draws, g_vbs_full, g_vbs_bytes;   /* (v102: vbs_place) */
static unsigned g_vsm_quads, g_vsm_runs;   /* (v104) */
static unsigned g_lite_skipped[3];   /* (v105) */
static uint32_t g_lite_dst[8];
static int gpu_bind_targets_(int need_depth);
static unsigned g_bt_hit, g_bt_miss, g_bt_miss_run, g_bt_runs0;   /* (v94: [VGLPERF] targets) */
static int gpu_bind_targets(int need_depth)
{
#if defined(__vita__)
    extern volatile uint32_t g_pb_runs;
    if (g_bt.ok && g_bt.nd == need_depth && g_bt.co == s_gpu.color_offset && g_bt.pitch == s_gpu.pitch
        && g_bt.cx == s_gpu.clip_x && g_bt.cy == s_gpu.clip_y && g_bt.cw == s_gpu.clip_w && g_bt.ch == s_gpu.clip_h
        && g_bt.fmt == s_gpu.format && g_bt.zo == s_zeta_offset && g_bt.runs == g_pb_runs && g_bt.gen == g_gsurf_gen
        && g_bt.cmax == s_clip_max && g_sh.tg && g_sh.fbo == g_bt.fbo && g_sh.vp_ok && g_sh.sc_ok && g_sh.cbvs_ok
        && !g_cap_on) {
        g_bt_hit++;
        g_cur_rt = g_bt.rt; g_cur_ds = g_bt.ds;
        g_cur_rt->wr = ++g_wr_clock;
        if (g_cur_ds) g_cur_ds->wr = ++g_wr_clock;
        memcpy(g_cur_sc, g_bt.sc, sizeof g_bt.sc);
        return 1;
    }
    {
        int r;
        g_bt_miss++;
        if (g_bt.ok && g_bt.runs != g_pb_runs) g_bt_miss_run++;
        r = gpu_bind_targets_(need_depth);
        g_bt.ok = r && !g_cur_rt->mrb;
        if (g_bt.ok) {
            g_bt.nd = need_depth; g_bt.co = s_gpu.color_offset; g_bt.pitch = s_gpu.pitch;
            g_bt.cx = s_gpu.clip_x; g_bt.cy = s_gpu.clip_y; g_bt.cw = s_gpu.clip_w; g_bt.ch = s_gpu.clip_h;
            g_bt.fmt = s_gpu.format; g_bt.zo = s_zeta_offset; g_bt.runs = g_pb_runs; g_bt.gen = g_gsurf_gen;
            g_bt.cmax = s_clip_max; g_bt.rt = g_cur_rt; g_bt.ds = g_cur_ds; g_bt.fbo = g_sh.fbo;
            memcpy(g_bt.sc, g_cur_sc, sizeof g_bt.sc);
        }
        return r;
    }
#else
    return gpu_bind_targets_(need_depth);
#endif
}
static int gpu_bind_targets_(int need_depth)
{
    uint32_t base = dma_resolve(s_gpu.color_offset);
    uint32_t bpp = surface_bpp();
    uint32_t w, h;
    int sc[4];
    if (!s_gpu.color_offset || !s_gpu.clip_w || !s_gpu.clip_h) return 0;
    w = (bpp && (s_gpu.pitch & 0xFFFF) / (bpp ? bpp : 4) >= s_gpu.clip_x + s_gpu.clip_w)
        ? (s_gpu.pitch & 0xFFFF) / bpp : s_gpu.clip_x + s_gpu.clip_w;
    if (((s_gpu.format >> 8) & 0xF) == 2) w = s_gpu.clip_x + s_gpu.clip_w;   /* swizzled: no pitch */
    h = s_gpu.clip_y + s_gpu.clip_h;
    g_cur_rt = gsurf_get(base, w, h, 0);
    if (!g_cur_rt) return 0;
    g_cur_rt->wr = ++g_wr_clock;
    gsurf_sync_from_mem(g_cur_rt);
    g_cur_ds = NULL;
#if defined(__vita__)
    /* (PS Vita) a depth buffer only on targets of the screen's size: on a
     * smaller one (the title's 256x256 render-to-texture passes) vitaGL's
     * scene for colour + depth renderbuffer did not begin (sceGxmBeginScene
     * unchecked in its scene_reset) and the next clear crashed; the same
     * target without depth works. Those passes draw without depth for now.
     * RECOMP_VGL_SMALLZETA=1 ([Display] vita_smallzeta=1): depth on them too. */
    if (need_depth && (g_cur_rt->w < 640 || g_cur_rt->h < 480)) {
        static int allow = -1;
        if (allow < 0) { const char *e = getenv("RECOMP_VGL_SMALLZETA"); allow = e && e[0] == '1'; }
        if (!allow) {
            static unsigned said;
            if (said < 8) { said++; fprintf(stderr, "  [VGL] target %08X %ux%u: drawn without depth (smaller than the screen)\n", base, g_cur_rt->w, g_cur_rt->h); }
            need_depth = 0;
        }
    }
#endif
    if (need_depth) {
        uint32_t zb = s_zeta_offset ? dma_resolve(s_zeta_offset) : (base | 1u);
        g_cur_ds = gsurf_get(zb, g_cur_rt->w, g_cur_rt->h, 1);
        if (g_cur_ds) g_cur_ds->wr = ++g_wr_clock;
    }
    if (g_cur_ds && !g_cur_rt->mrb != !g_cur_ds->mrb) gsurf_ms_drop(g_cur_rt->mrb ? g_cur_rt : g_cur_ds);   /* (MSAA: both or neither) */
    if (g_cur_rt->mrb) {
        if (g_cur_rt->ss_new) {
            g_cur_rt->ss_new = 0;
            gpu_copy_scaled(g_cur_rt->tex, g_cur_rt->pw, g_cur_rt->ph, g_cur_rt->pw, g_cur_rt->ph, gpu_fbo_ms(g_cur_rt->mrb, 0), g_cur_rt->pw, g_cur_rt->ph);
        }
        g_cur_rt->ms_new = 1;
        if (g_cur_ds) g_cur_ds->ms_new = 1;
    }
#if defined(__vita__)
    if (g_cur_ds && g_cur_ds->ss_new == 2) {
        uint32_t zv = s_cb_scratch[0];
        g_cur_ds->ss_new = 0;
        gpu_clear_ds(gsurf_draw_fbo(g_cur_rt, g_cur_ds), 3, (float)(zv >> 8) / 16777215.0f, (int)(zv & 0xFF));
    }
#endif
    gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
    if (!g_sh.vp_ok || g_sh.vw != (int)g_cur_rt->pw || g_sh.vh != (int)g_cur_rt->ph) {
        glViewport(0, 0, (GLsizei)g_cur_rt->pw, (GLsizei)g_cur_rt->ph);
        g_sh.vw = (int)g_cur_rt->pw; g_sh.vh = (int)g_cur_rt->ph; g_sh.vp_ok = 1;
    }
    sc[0] = (int)SX(g_cur_rt, s_gpu.clip_x); sc[1] = (int)SY(g_cur_rt, s_gpu.clip_y);
    sc[2] = (int)SX(g_cur_rt, s_gpu.clip_x + s_gpu.clip_w) - sc[0];
    sc[3] = (int)SY(g_cur_rt, s_gpu.clip_y + s_gpu.clip_h) - sc[1];
    memcpy(g_cur_sc, sc, sizeof sc);
    if (!g_sh.sc_ok || memcmp(g_sh.sc, sc, sizeof sc)) {
        glScissor(sc[0], sc[1], sc[2], sc[3]);
        memcpy(g_sh.sc, sc, sizeof sc); g_sh.sc_ok = 1;
    }
    {
        float c[4];
        /* window z goes to depth as z / zmax (see nv2a_d3d11.inc) */
        float zmax = (((s_gpu.format >> 4) & 0xF) == 1) ? 65535.0f : 16777215.0f;
        { static int zf = -1; if (zf < 0) zf = getenv("RECOMP_GPU_ZFORMAT") != NULL; if (s_clip_max > 0.0f && !zf) zmax = s_clip_max; }
        c[0] = 2.0f / (float)g_cur_rt->w; c[1] = 2.0f / (float)g_cur_rt->h; c[2] = 1.0f / zmax; c[3] = 0;
#if defined(__vita__)
        c[3] = g_vgl_flip ? -1.0f : 1.0f;   /* (the Cg vertex shader's y sign) */
#endif
        if (!g_sh.cbvs_ok || memcmp(g_sh.cbvs, c, sizeof c)) {
            gpu_ub(UB_VSC, c, 16);
            memcpy(g_sh.cbvs, c, sizeof c); g_sh.cbvs_ok = 1;
        }
    }
    return 1;
}

static GSurf *gpu_surface_for_texture(uint32_t off, uint32_t tw, uint32_t th, int *is_depth)
{
    /* One piece of memory can be both: the one drawn into last is what
     * memory holds (see nv2a_d3d11.inc). */
    GSurf *s = NULL;
    int i;
    /* The main depth buffer and the shadow pass's 256x256 zeta share one
     * address: a texture of the one's size reads that one (choosing by the
     * last binding alone broke the character shadows). */
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == off) {
            GSurf *c = &g_surf[i];
#if defined(__vita__)
            if (c->is_depth) {   /* (renderbuffers: not readable as textures) */
                static int said;
                if (!said++) fprintf(stderr, "  [VGL] a zeta surface is sampled: not available, read as memory\n");
                continue;
            }
#endif
            int cm = tw && c->w == tw && c->h == th, sm = s && tw && s->w == tw && s->h == th;
            if (!s || (cm != sm ? cm : c->wr > s->wr)) s = c;
        }
    if (!s) return NULL;
    *is_depth = (int)s->is_depth;
    return s;
}

typedef struct {
    int st[8][20];
    int fin0[4], fin1[4], misc[4];
    int fogmode, fogen;
    int smode[4], sflags[4];
    int at_en, at_func, at_s0;
    int sdot[4], sinp[4];
} PsKey;
static PsKey g_pskey;

static int g_vstg_dyn, g_vstg_lin;
static unsigned g_bs_gen;   /* (gpu_bind_stages calls: another caller's binding ends the keeping) */
static void gpu_bind_stages(void);
/* (v74, as nv2a_d3d11.inc gpu_stages) the textures and combiners kept as the
 * last draw bound them when nothing they depend on has changed since: no
 * texture, combiner or other register written with a new value, no new
 * pushbuffer run when a linear texture is bound, no drawn surface sampled.
 * They were built again for every draw: 11-15% of the render thread (v73).
 * RECOMP_STAGES_ALWAYS=1: every draw. */
static unsigned g_vstg_kept, g_vstg_made;
static void gpu_stages_vgl(void)
{
    static int always = -1, valid; static long trev; static int zt, zw, md; static unsigned mygen, vpg;
    if (always < 0) always = getenv("RECOMP_STAGES_ALWAYS") != NULL;
    if (valid && !always && !(s_grp & (GRP_TEX | GRP_COMB | GRP_OTHER)) && !((s_grp & GRP_RUN) && g_vstg_lin)
        && g_sh.tex_ok && g_sh.cbps_ok && trev == xbox_tex_rev && zt == s_3d.depth_test && zw == s_3d.depth_mask
        && !g_cap_on && mygen == g_bs_gen && !s_gpu.inline_active && vpg == s_vp_gen && md == s_3d.mode) { g_vstg_kept++; return; }
    s_grp &= ~(GRP_TEX | GRP_COMB | GRP_OTHER | GRP_RUN);
    g_vstg_dyn = 0; g_vstg_lin = 0;
    gpu_bind_stages();
    mygen = g_bs_gen; vpg = s_vp_gen; md = s_3d.mode;
    valid = !g_vstg_dyn && !s_gpu.inline_active; trev = xbox_tex_rev;
    zt = s_3d.depth_test; zw = s_3d.depth_mask;
    g_vstg_made++;
}
static void gpu_bind_stages(void)
{
    struct {
        int cin[4], ain[4], f1[4], f2[4], f3[4]; float c0[4], c1[4];
    } st[8];
    struct {
        int fin0[4], fin1[4], misc[4];
        float ff0[4], ff1[4], fogc[4], fogp[4];
        int smode[4], sflags[4];
        float sscale[4][4];
        int atest[4];
        int sdot[4];
        int sinp[4];
    } tail;
    char cb[sizeof st + sizeof tail];
    GLuint tex[4] = { 0 }, smp[4] = { 0 };
    int k, i;
#if defined(__vita__)
    int at_ntex = 0, at_seethru = 0;
#endif

    VWHERE("stages: combiner words");   /* (v176: finer zones) */
    memset(st, 0, sizeof st);
    memset(&tail, 0, sizeof tail);
    for (i = 0; i < s_cp.n; i++) {
        const CStage *S = &s_cp.st[i];
        for (k = 0; k < 4; k++) {
            st[i].cin[k] = S->c[k].reg | (S->c[k].alpha << 4) | (S->c[k].map << 8);
            st[i].ain[k] = S->a[k].reg | (S->a[k].alpha << 4) | (S->a[k].map << 8);
        }
        st[i].f1[1] = (S->abdot << 1) | S->cddot | (S->mux << 2) | (S->amux << 3);
        st[i].f1[2] = S->op; st[i].f1[3] = S->aop;
        st[i].f2[0] = S->dab; st[i].f2[1] = S->dcd; st[i].f2[2] = S->dsum;
        st[i].f3[0] = S->aab; st[i].f3[1] = S->acd; st[i].f3[2] = S->asum;
        st[i].f3[3] = (S->ab_b2a << 1) | S->cd_b2a;
        memcpy(st[i].c0, s_cf0[i], 16); memcpy(st[i].c1, s_cf1[i], 16);
    }
#define PACK(c) ((c).reg | ((c).alpha << 4) | ((c).map << 8))
    tail.fin0[0] = PACK(s_cp.fA); tail.fin0[1] = PACK(s_cp.fB); tail.fin0[2] = PACK(s_cp.fC); tail.fin0[3] = PACK(s_cp.fD);
    tail.fin1[0] = PACK(s_cp.fE); tail.fin1[1] = PACK(s_cp.fF); tail.fin1[2] = PACK(s_cp.fG); tail.fin1[3] = s_cp.has_final;
#undef PACK
    tail.misc[0] = s_cp.n; tail.misc[1] = s_cp.muxmsb; tail.misc[2] = s_cp.clamp_sum; tail.misc[3] = s_3d.comb_seen;
    memcpy(tail.ff0, s_ff0, 16); memcpy(tail.ff1, s_ff1, 16);
    memcpy(tail.fogc, s_fogc, 16);
    tail.fogp[0] = s_3d.fog_params[0]; tail.fogp[1] = s_3d.fog_params[1];
    tail.fogp[2] = (float)s_3d.fog_mode; tail.fogp[3] = s_3d.fog_enable ? 1.0f : 0.0f;
    tail.atest[0] = s_3d.alpha_test; tail.atest[1] = (int)s_3d.alpha_func; tail.atest[2] = (int)s_3d.alpha_ref;
    tail.atest[3] = s_stage[0].enabled;

    for (k = 0; k < 4; k++) {
        const SwStage *S = &s_stage[k];
        int fl = 0, isd = 0;
        GSurf *gs;
        tail.smode[k] = (int)S->mode;
        tail.sdot[k] = k ? (int)((s_dot_map >> (4 * (k - 1))) & 0xF) : 0;
        tail.sinp[k] = k == 2 ? (int)((s_other_in >> 20) & 1) : k == 3 ? (int)((s_other_in >> 21) & 3) : 0;
        if (S->enabled) fl |= 1;
        if (s_tex_used[k] || (S->mode && S->mode != 1 && S->mode != 2)) fl |= 2;
        if (S->normalized) fl |= 4;
        tail.sscale[k][0] = S->t.width ? 1.0f / (float)S->t.width : 1.0f;
        tail.sscale[k][1] = S->t.height ? 1.0f / (float)S->t.height : 1.0f;
        if ((fl & 1) && (fl & 2)) {
            static int surf_log = -1;
            if (surf_log < 0) surf_log = getenv("RECOMP_SURF_LOG") != NULL;   /* (once: getenv on every stage of every draw cost) */
#if defined(__vita__)
            TX_T0(tsf);
            g_tx_binds++;
#endif
            VWHERE("stages: surface lookup");
            gs = gpu_surface_for_texture(S->t.offset, S->t.width, S->t.height, &isd);
            VWHERE("stages: per stage");
            if (surf_log && (gs || (S->t.width <= 256 && S->t.height <= 256 && S->t.offset < 0x04000000u
                                                     && gsurf_near(S->t.offset)))) {
                static uint32_t seen[64]; static int ns; int q;
                uint32_t key = S->t.offset ^ (s_tex_reg[k * 0x10 + 1] << 4);
                for (q = 0; q < ns && seen[q] != key; q++) {}
                if (q == ns && ns < 64) {
                    seen[ns++] = key;
                    fprintf(stderr, "  [SURF] stage %d samples %08X %ux%u fmt %08X mode %u -> %s\n", k, S->t.offset,
                            S->t.width, S->t.height, s_tex_reg[k * 0x10 + 1], S->mode,
                            gs ? (isd ? "zeta surface" : "colour surface") : "memory (near a surface!)");
                }
            }
            if (gs && !isd) gsurf_sync_from_mem(gs);
            if (gs) gsurf_resolve(gs);   /* (MSAA: the texture from its samples) */
            if (gs) g_vstg_dyn = 1;
            if (!S->normalized) g_vstg_lin = 1;
#if defined(__vita__)
            TX_ADD(0, tsf);
#endif
            if (gs) {
                /* a surface bound as a target reads as zero, as in D3D */
                tex[k] = (gs == g_cur_rt || gs == g_cur_ds) ? g_tex_black : gs->tex;
#if defined(__vita__)
                at_seethru = 1;
#endif
                if (isd && tex[k] != g_tex_black) fl |= 8;
                if (!S->normalized) { tail.sscale[k][0] = 1.0f / (float)gs->w; tail.sscale[k][1] = 1.0f / (float)gs->h; }
            } else {
#if defined(__vita__)
                TX_T0(tg);
                VWHERE("texture lookup/decode");
                tex[k] = gtex_get(&S->t);
                if (tex[k]) { at_ntex++; if (!g_gtex_opaque || s_ultra_used) at_seethru = 1; }
                VWHERE("stages: per stage");
                TX_ADD(4, tg);
#else
                tex[k] = gtex_get(&S->t);
#endif
            }
            if (!tex[k]) fl &= ~1;
            /* (a pack picture: always through its levels) */
            VWHERE("stages: sampler");
            smp[k] = gpu_sampler_ex(S->t.addr_u, S->t.addr_v, S->bilinear || (!gs && s_ultra_used),
                                    (S->mip || (!gs && s_ultra_used)) && !gs && S->normalized);
        }
        tail.sflags[k] = fl;
        VWHERE("stages: per stage");
    }
    VWHERE("stages: shader key");
#if defined(__vita__)
    /* (v206) The game leaves the alpha test on for nearly all the world (v205 census: 420-485 of a frame's draws,
     * 52-62 thousand indices, against 12 plain ones). A shader that can discard makes the Vita's GPU give up its
     * hidden-surface removal for that draw: every covered pixel shaded. Where the test cannot fail -- no blending,
     * and every texture the draw reads without a single see-through texel -- the shader is made without it.
     * TT_ATEST_KEEP=1: as before. g_ab2_ph 1: as before, for the [AB2] turns. */
    if (tail.atest[0]) {
        static int keep = -1;
        if (keep < 0) { const char *e = getenv("TT_ATEST_KEEP"); keep = e && e[0] == '1'; }
        if (!keep && !g_ab2_ph && !s_gpu.blend_enable && at_ntex > 0 && !at_seethru) { tail.atest[0] = 0; g_ab2_drop++; }
        else g_ab2_keep++;
    }
#endif
    {
        PsKey *K = &g_pskey;
        memset(K, 0, sizeof *K);
        K->misc[0] = tail.misc[0]; K->misc[1] = tail.misc[1]; K->misc[2] = tail.misc[2]; K->misc[3] = tail.misc[3];
        if (K->misc[3]) {
            for (i = 0; i < s_cp.n && i < 8; i++) {
                memcpy(&K->st[i][0], st[i].cin, 16); memcpy(&K->st[i][4], st[i].ain, 16);
                memcpy(&K->st[i][8], st[i].f1, 16); memcpy(&K->st[i][12], st[i].f2, 16);
                memcpy(&K->st[i][16], st[i].f3, 16);
            }
            memcpy(K->fin0, tail.fin0, 16); memcpy(K->fin1, tail.fin1, 16);
        } else {
            K->misc[0] = K->misc[1] = K->misc[2] = 0;
            K->at_s0 = tail.atest[3] != 0;
        }
        K->fogen = s_3d.fog_enable ? 1 : 0;
        K->fogmode = K->fogen ? (int)s_3d.fog_mode : 0;
        memcpy(K->smode, tail.smode, 16); memcpy(K->sflags, tail.sflags, 16);
        K->at_en = tail.atest[0] ? 1 : 0;
        K->at_func = K->at_en ? tail.atest[1] : 0;
        memcpy(K->sdot, tail.sdot, 16); memcpy(K->sinp, tail.sinp, 16);
    }
#if defined(__vita__)
    {   /* the fallback shader's stage 0 (s_cg_ps_main): scale, on */
        float g[4];
        int fl0 = tail.sflags[0], m0 = tail.smode[0];
        g[0] = (fl0 & 4) ? 1.0f : tail.sscale[0][0];
        g[1] = (fl0 & 4) ? 1.0f : tail.sscale[0][1];
        g[2] = ((fl0 & 1) && (fl0 & 2) && !(fl0 & 8) && m0 != 0 && m0 != 4) ? 1.0f : 0.0f;
        g[3] = 0.0f;
        if (memcmp(g_vub[VB_PSC][74], g, 16)) { memcpy(g_vub[VB_PSC][74], g, 16); g_vub_gen[VB_PSC]++; }
    }
#endif
    VWHERE("stages: combiner constants upload");
    memcpy(cb, st, sizeof st);
    memcpy(cb + sizeof st, &tail, sizeof tail);
    if (!g_sh.cbps_ok || memcmp(g_sh.cbps, cb, sizeof cb)) {
        gpu_ub(UB_PSC, cb, sizeof cb);
        memcpy(g_sh.cbps, cb, sizeof cb); g_sh.cbps_ok = 1;
    }
    if (!g_sh.tex_ok || memcmp(g_sh.tex, tex, sizeof tex) || memcmp(g_sh.smp, smp, sizeof smp)) {
        VWHERE("stages: GL texture binds");
        for (k = 0; k < 4; k++) {
            glActiveTexture(GL_TEXTURE0 + k);
            glBindTexture(GL_TEXTURE_2D, tex[k]);
            glBindSampler((GLuint)k, smp[k]);
        }
        memcpy(g_sh.tex, tex, sizeof tex); memcpy(g_sh.smp, smp, sizeof smp); g_sh.tex_ok = 1;
    }
    g_bs_gen++;
    VWHERE("batch: textures/stages");
}

/* ---------------- vertices on the CPU ---------------- */

typedef struct { float p[4], d0[4], d1[4], fog[4], t[4][4]; } GVert;
static GVert *g_verts;
static uint32_t *g_idx, g_verts_cap, g_idx_cap;
static uint32_t *g_slot, *g_slot_gen;
static uint32_t g_gen = 1;
static uint32_t *g_vidx, g_vidx_cap;
static uint8_t  *g_vok;

static uint32_t gpu_vertex_slot(uint32_t index, uint32_t *nverts)
{
    uint32_t slot = index & (SW3D_CACHE - 1);
    if (g_slot_gen[slot] == g_gen && g_slot[slot * 2] == index) return g_slot[slot * 2 + 1];
    if (*nverts == g_vidx_cap) {
        uint32_t nc = g_vidx_cap ? g_vidx_cap * 2 : 4096;
        uint32_t *ni = (uint32_t *)realloc(g_vidx, nc * 4);
        uint8_t *no = (uint8_t *)realloc(g_vok, nc);
        if (ni) g_vidx = ni;
        if (no) g_vok = no;
        if (!ni || !no) return 0xFFFFFFFFu;
        g_vidx_cap = nc;
    }
    g_vidx[*nverts] = index;
    g_slot_gen[slot] = g_gen;
    g_slot[slot * 2] = index;
    g_slot[slot * 2 + 1] = *nverts;
    return (*nverts)++;
}

static void gpu_xform_range(uint32_t lo, uint32_t hi)
{
    uint32_t k;
    for (k = lo; k < hi; k++) {
        SwVert v;
        GVert *g = &g_verts[k];
        if (!sw3d_transform(g_vidx[k], &v)) { g_vok[k] = 0; memset(g, 0, sizeof *g); continue; }
        if (v.p[3] == 0.0f) v.p[3] = 1e-30f;
        g_vok[k] = 1;
        memcpy(g->p, v.p, 16);
        memcpy(g->d0, &v.a[SA_D0], 16);
        memcpy(g->d1, &v.a[SA_D1], 16);
        g->fog[0] = v.a[SA_FOG]; g->fog[1] = g->fog[2] = g->fog[3] = 0;
        memcpy(g->t, &v.a[SA_T0], 64);
    }
}

/* Transform workers (as nv2a_d3d11.inc): 64-vertex chunks, on the cores
 * other than CPU 0. RECOMP_VTX_THREADS=n (0 = off) overrides the count. */
#define VX_MAX 8
#define VX_CHUNK 64
static int g_vx_n = -1;
static volatile LONG g_vx_gen, g_vx_next, g_vx_busy;
static volatile uint32_t g_vx_total;
static HANDLE g_vx_wake[VX_MAX];
static volatile LONG g_vx_sleeping[VX_MAX];
static void gpu_vx_work(void)
{
    for (;;) {
        LONG c = InterlockedIncrement(&g_vx_next) - 1;
        uint32_t lo = (uint32_t)c * VX_CHUNK, hi;
        if (lo >= g_vx_total) break;
        hi = lo + VX_CHUNK; if (hi > g_vx_total) hi = g_vx_total;
        gpu_xform_range(lo, hi);
    }
}
static DWORD WINAPI gpu_vx_thread(LPVOID arg)
{
    int idx = (int)(intptr_t)arg;
    LONG seen = 0;
    DWORD_PTR pm = 0, sm = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) && (pm & ~(DWORD_PTR)1))
        SetThreadAffinityMask(GetCurrentThread(), pm & ~(DWORD_PTR)1);
    for (;;) {
        int spins = 0;
        while (g_vx_gen == seen) {
            if (++spins < 20000) { YieldProcessor(); continue; }
            InterlockedExchange(&g_vx_sleeping[idx], 1);
            if (g_vx_gen == seen) WaitForSingleObject(g_vx_wake[idx], 20);
            InterlockedExchange(&g_vx_sleeping[idx], 0);
            spins = 0;
        }
        seen = g_vx_gen;
        gpu_vx_work();
        InterlockedDecrement(&g_vx_busy);
    }
    return 0;
}
static void gpu_vx_init(void)
{
    DWORD_PTR pm = 0, sm = 0;
    int i, cpus = 0;
    const char *e = getenv("RECOMP_VTX_THREADS");
    g_vx_n = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm))
        for (i = 1; i < (int)(8 * sizeof(DWORD_PTR)); i++) if (pm & ((DWORD_PTR)1 << i)) cpus++;
    g_vx_n = cpus > 4 ? 4 : cpus;
    if (cpus >= 2 && g_vx_n > cpus - 1) g_vx_n = cpus - 1;
#if defined(__vita__)
    g_vx_n = 1;   /* (three cores for a title: the game's thread, this one, a helper) */
#endif
    if (e) { g_vx_n = atoi(e); if (g_vx_n < 0) g_vx_n = 0; if (g_vx_n > VX_MAX) g_vx_n = VX_MAX; }
    for (i = 0; i < g_vx_n; i++) {
        HANDLE h;
        g_vx_wake[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        h = CreateThread(NULL, 0, gpu_vx_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (!h) { g_vx_n = i; break; }
        SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(h);
    }
    fprintf(stderr, "  [GPU3D] %d vertex transform thread(s) besides the caller\n", g_vx_n);
}

static void gpu_transform_all(uint32_t nverts)
{
    int i;
    if (nverts > g_verts_cap) {
        uint32_t nc = g_verts_cap ? g_verts_cap : 4096;
        GVert *nv;
        while (nc < nverts) nc *= 2;
        nv = (GVert *)realloc(g_verts, nc * sizeof(GVert));
        if (!nv) { nverts = g_verts_cap; } else { g_verts = nv; g_verts_cap = nc; }
    }
    s_3d.verts += nverts;
    if (g_vx_n < 0) gpu_vx_init();
    if (g_vx_n <= 0 || nverts < 2 * VX_CHUNK || (s_3d.mode == 2 && (s_vp_slow || s_vpd_cwrite))) {
        gpu_xform_range(0, nverts);
        return;
    }
    g_vx_total = nverts;
    g_vx_next = 0;
    g_vx_busy = g_vx_n;
    MemoryBarrier();
    InterlockedIncrement(&g_vx_gen);
    for (i = 0; i < g_vx_n; i++)
        if (g_vx_sleeping[i]) SetEvent(g_vx_wake[i]);
    gpu_vx_work();
    while (g_vx_busy > 0) YieldProcessor();
}

static uint32_t g_nidx;
static void gpu_tri(uint32_t a, uint32_t b, uint32_t c, uint32_t *nverts)
{
    uint32_t sa = gpu_vertex_slot(a, nverts), sb, sc;
    if (sa == 0xFFFFFFFFu) return;
    sb = gpu_vertex_slot(b, nverts); if (sb == 0xFFFFFFFFu) return;
    sc = gpu_vertex_slot(c, nverts); if (sc == 0xFFFFFFFFu) return;
    if (g_nidx + 3 > g_idx_cap) {
        uint32_t nc = g_idx_cap ? g_idx_cap * 2 : 16384;
        uint32_t *ni = (uint32_t *)realloc(g_idx, nc * 4);
        if (!ni) return;
        g_idx = ni; g_idx_cap = nc;
    }
    g_idx[g_nidx++] = sa; g_idx[g_nidx++] = sb; g_idx[g_nidx++] = sc;
}

static void gpu_drop_bad_tris(uint32_t nverts)
{
    uint32_t k, o = 0, bad = 0;
    for (k = 0; k < nverts; k++) if (!g_vok[k]) { bad = 1; break; }
    if (!bad) return;
    for (k = 0; k + 2 < g_nidx; k += 3) {
        if (!g_vok[g_idx[k]] || !g_vok[g_idx[k + 1]] || !g_vok[g_idx[k + 2]]) continue;
        g_idx[o++] = g_idx[k]; g_idx[o++] = g_idx[k + 1]; g_idx[o++] = g_idx[k + 2];
    }
    g_nidx = o;
}

/* ---------------- vertex programs as GLSL ---------------- */

/* As nv2a_d3d11_vp.inc: each program the title uploads is written out as a
 * vertex shader, compiled in the background, and once ready the batch's
 * vertices go to the GPU as raw inputs. Until then (or when the program
 * writes constants) the CPU path draws it. [Display] vertex_programs=cpu /
 * RECOMP_HW_VP=0 turns it off. */
typedef struct {
    uint32_t hash, len, state;        /* state: 0 compiling, 1 ready, 2 failed */
    uint32_t words[VP_SLOTS][4];
    uint32_t nin;
    uint8_t  inreg[16];
    GLuint vs;
#if defined(__vita__)
    char *src;                        /* (the Cg, kept for hwvp_dump) */
    uint16_t c_lo, c_n;               /* (the rows of c[] it declares: only those are sent each draw) */
    int dumped, probed;
#endif
} HwVp;

#define HWVP_MAX 512
static HwVp *g_hwvp[HWVP_MAX];
static int g_hwvp_n;
static int g_hwvp_on = -1;
static float g_vpc_shadow[VP_CONSTS][4];
static int g_vpc_ok;
/* (v109) the vertex-program constants to the uniform rows: only the rows the
 * pushbuffer changed since the last draw (s_vpc_lo..hi, nv2a_sw3d.inc) */
static unsigned g_vpc_rows, g_vpc_syncs;
static void gpu_vpc_sync(void)
{
#if defined(__vita__)
    static int mode = -1;
    if (mode < 0) { const char *e = getenv("RECOMP_VGL_VPC_ROWS"); mode = !(e && e[0] == '0'); }
    if (mode) {
        if (!g_vpc_ok) { s_vpc_lo = 0; s_vpc_hi = VP_CONSTS - 1; s_vpc_dirty = 1; }
        if (!s_vpc_dirty) return;
        if (s_vpc_lo < 0) s_vpc_lo = 0;
        if (s_vpc_hi > VP_CONSTS - 1) s_vpc_hi = VP_CONSTS - 1;
        if (s_vpc_lo <= s_vpc_hi) {
            memcpy(g_vub[VB_VPC][s_vpc_lo], s_3d.c[s_vpc_lo], (size_t)(s_vpc_hi - s_vpc_lo + 1) * 16);
            g_vpc_rows += (unsigned)(s_vpc_hi - s_vpc_lo + 1); g_vpc_syncs++;
            g_vub_gen[VB_VPC]++;
            if (!g_vub_gen[VB_VPC]) g_vub_gen[VB_VPC] = 1;
        }
        g_vpc_ok = 1; s_vpc_dirty = 0; s_vpc_lo = VP_CONSTS; s_vpc_hi = -1;
        return;
    }
#endif
    if (!g_vpc_ok || memcmp(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow)) {
        memcpy(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow);
        g_vpc_ok = 1;
        gpu_ub(UB_VPC, g_vpc_shadow, sizeof g_vpc_shadow);
    }
}
static float *g_xin; static uint32_t g_xin_cap;
static uint32_t g_hwvp_stat_hw, g_hwvp_stat_cpu, g_hwvp_compiled, g_hwvp_failed;
#if defined(__vita__)
static uint8_t s_hwvp_cg_in[16];   /* (hwvp_translate_cg: guest input register at each location) */
static int s_hwvp_cg_lo, s_hwvp_cg_n;   /* (and the constants' rows it declares) */
#endif

static uint32_t hwvp_hash(const uint32_t (*w)[4], uint32_t len)
{
    uint32_t h = 2166136261u, i, k;
    for (i = 0; i < len; i++) for (k = 0; k < 4; k++) h = (h ^ w[i][k]) * 16777619u;
    return h ^ len;
}

#include <stdarg.h>
static void hwvp_cat(char **p, size_t *cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!*p) return;
    for (;;) {
        char *np;
        va_start(ap, fmt);
        n = vsnprintf(*p + *len, *cap - *len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < *cap - *len) { *len += n; return; }
        *cap *= 2;
        np = (char *)realloc(*p, *cap);
        if (!np) { free(*p); *p = NULL; return; }
        *p = np;
    }
}

static const char *hwvp_src_expr(const VpSrc *d, char *buf, size_t bs)
{
    static const char sw[] = "xyzw";
    char base[64];
    switch (d->mux) {
    case 1: if (d->reg == 12) snprintf(base, sizeof base, "o[0]"); else snprintf(base, sizeof base, "R[%u]", d->reg); break;
    case 2: snprintf(base, sizeof base, "v%u", d->reg); break;
    case 3:
        if (d->rel) snprintf(base, sizeof base, "kc(%d + a0)", d->ci);
        else if (d->ci >= 0 && d->ci < VP_CONSTS) snprintf(base, sizeof base, "c[%d]", d->ci);
        else snprintf(base, sizeof base, "vec4(0.0, 0.0, 0.0, 1.0)");
        break;
    default: snprintf(base, sizeof base, "vec4(0.0, 0.0, 0.0, 1.0)"); break;
    }
    snprintf(buf, bs, "%s(%s).%c%c%c%c", d->neg ? "-" : "", base,
             sw[d->sw[0]], sw[d->sw[1]], sw[d->sw[2]], sw[d->sw[3]]);
    return buf;
}

static const char *hwvp_mask(uint32_t m, char *b)
{
    int n = 0;
    b[n++] = '.';
    if (m & 8) b[n++] = 'x';
    if (m & 4) b[n++] = 'y';
    if (m & 2) b[n++] = 'z';
    if (m & 1) b[n++] = 'w';
    b[n] = 0;
    return b;
}

/* The decoded program (s_vpd) as GLSL; NULL if it cannot run on the GPU (it
 * writes constants). The arithmetic is nv2a_d3d11_vp.inc's, line for line. */
static char *hwvp_translate(HwVp *e)
{
    size_t cap = 16384, len = 0;
    char *p = (char *)malloc(cap);
    uint32_t k, used = 0, i;
    char a[96], b[96], c[96], mk[8];
    if (!p) return NULL;
    p[0] = 0;
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        int s;
        if (I->o_mask && !I->orb) { free(p); return NULL; }
        for (s = 0; s < 3; s++) if (I->s[s].mux == 2) used |= 1u << I->s[s].reg;
    }
    e->nin = 0;
    for (i = 0; i < 16; i++) if (used & (1u << i)) e->inreg[e->nin++] = (uint8_t)i;

    hwvp_cat(&p, &cap, &len,
        "#version 330 core\n"
        "layout(std140) uniform VSC { vec4 vp; };\n"
        "layout(std140) uniform VPC { vec4 c[192]; };\n"
        "vec4 kc(int i) { return (i >= 0 && i < 192) ? c[i] : vec4(0.0, 0.0, 0.0, 1.0); }\n"
        "vec4 rcc(float x) { float r = 1.0 / x; float ar = abs(r);\n"
        "  ar = (ar < 5.42101e-20) ? 5.42101e-20 : ((ar > 1.884467e+19) ? 1.884467e+19 : ar);\n"
        "  r = (r < 0.0 || (r == 0.0 && floatBitsToUint(r) == 0x80000000u)) ? -ar : ar; return vec4(r); }\n"
        "vec4 vexp(float x) { float fl = floor(x); return vec4(exp2(fl), x - fl, exp2(x), 1.0); }\n"
        "vec4 vlog(float x) { x = abs(x); if (x == 0.0) return vec4(-uintBitsToFloat(0x7f800000u), 1.0, -uintBitsToFloat(0x7f800000u), 1.0);\n"
        "  float e = floor(log2(x)); return vec4(e, x / exp2(e), log2(x), 1.0); }\n"
        "vec4 vlit(vec4 s) { float nl = max(s.x, 0.0), nh = max(s.y, 0.0); float pw = clamp(s.w, -127.9961, 127.9961);\n"
        "  return vec4(1.0, nl, (s.x > 0.0) ? pow(nh, pw) : 0.0, 1.0); }\n");
    for (i = 0; i < e->nin; i++) hwvp_cat(&p, &cap, &len, "layout(location = %u) in vec4 v%u;\n", i, e->inreg[i]);
    hwvp_cat(&p, &cap, &len,
        "out vec4 v_d0; out vec4 v_d1; out vec4 v_fog; out vec4 v_t0; out vec4 v_t1; out vec4 v_t2; out vec4 v_t3;\n"
        "invariant gl_Position;\n"
        "void main() {\n"
        "  vec4 R[16]; vec4 o[13]; int a0 = 0; vec4 m, l;\n"
        "  for (int q = 0; q < 16; q++) R[q] = vec4(0.0);\n"
        "  for (int q2 = 0; q2 < 13; q2++) o[q2] = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "  o[3] = vec4(1.0);\n");
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        hwvp_src_expr(&I->s[0], a, sizeof a);
        hwvp_src_expr(&I->s[1], b, sizeof b);
        hwvp_src_expr(&I->s[2], c, sizeof c);
        hwvp_cat(&p, &cap, &len, "  m = vec4(0.0); l = vec4(0.0);\n");
        switch (I->mac) {
        case MAC_MOV: hwvp_cat(&p, &cap, &len, "  m = %s;\n", a); break;
        case MAC_MUL: hwvp_cat(&p, &cap, &len, "  m = %s * %s;\n", a, b); break;
        case MAC_ADD: hwvp_cat(&p, &cap, &len, "  m = %s + %s;\n", a, c); break;
        case MAC_MAD: hwvp_cat(&p, &cap, &len, "  m = %s * %s + %s;\n", a, b, c); break;
        case MAC_DP3: hwvp_cat(&p, &cap, &len, "  m = vec4(dot((%s).xyz, (%s).xyz));\n", a, b); break;
        case MAC_DPH: hwvp_cat(&p, &cap, &len, "  { vec4 A_ = %s, B_ = %s; m = vec4(dot(A_.xyz, B_.xyz) + B_.w); }\n", a, b); break;
        case MAC_DP4: hwvp_cat(&p, &cap, &len, "  m = vec4(dot(%s, %s));\n", a, b); break;
        case MAC_DST: hwvp_cat(&p, &cap, &len, "  { vec4 A_ = %s, B_ = %s; m = vec4(1.0, A_.y * B_.y, A_.z, B_.w); }\n", a, b); break;
        case MAC_MIN: hwvp_cat(&p, &cap, &len, "  m = min(%s, %s);\n", a, b); break;
        case MAC_MAX: hwvp_cat(&p, &cap, &len, "  m = max(%s, %s);\n", a, b); break;
        case MAC_SLT: hwvp_cat(&p, &cap, &len, "  m = vec4(lessThan(%s, %s));\n", a, b); break;
        case MAC_SGE: hwvp_cat(&p, &cap, &len, "  m = vec4(greaterThanEqual(%s, %s));\n", a, b); break;
        case MAC_ARL: hwvp_cat(&p, &cap, &len, "  a0 = int(floor((%s).x + 0.001));\n", a); break;
        default: break;
        }
        switch (I->ilu) {
        case ILU_MOV: hwvp_cat(&p, &cap, &len, "  l = %s;\n", c); break;
        case ILU_RCP: hwvp_cat(&p, &cap, &len, "  l = vec4(1.0 / (%s).x);\n", c); break;
        case ILU_RCC: hwvp_cat(&p, &cap, &len, "  l = rcc((%s).x);\n", c); break;
        case ILU_RSQ: hwvp_cat(&p, &cap, &len, "  l = vec4(1.0 / sqrt(abs((%s).x)));\n", c); break;
        case ILU_EXP: hwvp_cat(&p, &cap, &len, "  l = vexp((%s).x);\n", c); break;
        case ILU_LOG: hwvp_cat(&p, &cap, &len, "  l = vlog((%s).x);\n", c); break;
        case ILU_LIT: hwvp_cat(&p, &cap, &len, "  l = vlit(%s);\n", c); break;
        default: break;
        }
        if (I->mac && I->mac != MAC_ARL && I->mac_mask && I->out_r < 16 && I->out_r != 12)
            hwvp_cat(&p, &cap, &len, "  R[%u]%s = m%s;\n", I->out_r, hwvp_mask(I->mac_mask, mk), mk);
        if (I->ilu && I->ilu_mask) {
            uint32_t d = I->mac ? 1 : I->out_r;
            if (d < 16 && d != 12) hwvp_cat(&p, &cap, &len, "  R[%u]%s = l%s;\n", d, hwvp_mask(I->ilu_mask, mk), mk);
        }
        if (I->o_mask && I->orb && I->oaddr < VP_OUTS)
            hwvp_cat(&p, &cap, &len, "  o[%u]%s = %s%s;\n", I->oaddr, hwvp_mask(I->o_mask, mk), I->omux ? "l" : "m", mk);
        if (I->final) break;
    }
    hwvp_cat(&p, &cap, &len,
        "  float w = o[0].w; if (w == 0.0) w = 1e-30;\n"
        "  vec4 P = vec4(o[0].xyz * w, w);\n"
        "  gl_Position = vec4(P.x * vp.x - P.w, P.y * vp.y - P.w, 2.0 * P.z * vp.z - P.w, P.w);\n"
        "  v_d0 = clamp(o[3], 0.0, 1.0); v_d1 = clamp(o[4], 0.0, 1.0); v_fog = vec4(o[5].x, 0.0, 0.0, 0.0);\n"
        "  v_t0 = o[9]; v_t1 = o[10]; v_t2 = o[11]; v_t3 = o[12];\n"
        "}\n");
    return p;
}

#if defined(__vita__)
/* hwvp_translate in Cg (PS Vita): the same program, line for line, for sce
 * Cg -- no integer bit casts (rcc keeps the sign of r; log of 0 gives the
 * largest negative float), comparisons through step(), the address register
 * a float. Inputs vin<i> at location i (nv2a_gl_vita.inc binds them), the
 * constants a uniform array c[192] (uploaded from gpu_ub(UB_VPC)). */
static const char *hwvp_src_cg(const VpSrc *d, char *buf, size_t bs)
{
    static const char sw[] = "xyzw";
    char base[64];
    switch (d->mux) {
    case 1: if (d->reg == 12) snprintf(base, sizeof base, "o0"); else snprintf(base, sizeof base, "R%u", d->reg); break;
    case 2: { uint32_t i; int at = -1; for (i = 0; i < 16; i++) if (s_hwvp_cg_in[i] == d->reg) { at = (int)i; break; }
              if (at >= 0) snprintf(base, sizeof base, "vin%d", at); else snprintf(base, sizeof base, "float4(0.0, 0.0, 0.0, 1.0)"); } break;
    case 3:
        if (d->rel) snprintf(base, sizeof base, "kc(%d.0 + a0)", d->ci);
        else if (d->ci >= 0 && d->ci < VP_CONSTS) snprintf(base, sizeof base, "c[%d]", d->ci - s_hwvp_cg_lo);
        else snprintf(base, sizeof base, "float4(0.0, 0.0, 0.0, 1.0)");
        break;
    default: snprintf(base, sizeof base, "float4(0.0, 0.0, 0.0, 1.0)"); break;
    }
    snprintf(buf, bs, "%s(%s).%c%c%c%c", d->neg ? "-" : "", base,
             sw[d->sw[0]], sw[d->sw[1]], sw[d->sw[2]], sw[d->sw[3]]);
    return buf;
}
static char *hwvp_translate_cg(HwVp *e)
{
    size_t cap = 16384, len = 0;
    char *p = (char *)malloc(cap);
    uint32_t k, used = 0, i;
    char a[128], b[128], c[128], mk[8];
    if (!p) return NULL;
    p[0] = 0;
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        int s;
        if (I->o_mask && !I->orb) { free(p); return NULL; }
        for (s = 0; s < 3; s++) if (I->s[s].mux == 2) used |= 1u << I->s[s].reg;
    }
    e->nin = 0;
    for (i = 0; i < 16; i++) if (used & (1u << i)) e->inreg[e->nin++] = (uint8_t)i;
    for (i = 0; i < 16; i++) s_hwvp_cg_in[i] = i < e->nin ? e->inreg[i] : 0xFF;
    {   /* the constants' rows: every draw sends them all again (sceGxm's
         * vertex ring buffer: 192 rows x 1500 draws a frame did not fit and
         * the world drew nothing), so only the range the program reads; with
         * relative addressing, all of them */
        int lo = VP_CONSTS, hi = -1, rel = 0, s2;
        for (k = 0; k < s_vpd_len; k++) {
            for (s2 = 0; s2 < 3; s2++) {
                const VpSrc *d = &s_vpd[k].s[s2];
                if (d->mux != 3) continue;
                if (d->rel) rel = 1;
                else if (d->ci >= 0 && d->ci < VP_CONSTS) { if (d->ci < lo) lo = d->ci; if (d->ci > hi) hi = d->ci; }
            }
            if (s_vpd[k].final) break;
        }
        if (rel) { lo = 0; hi = VP_CONSTS - 1; }
        if (hi < 0) { lo = 0; hi = 0; }
        s_hwvp_cg_lo = lo; s_hwvp_cg_n = hi - lo + 1;
        e->c_lo = (uint16_t)lo; e->c_n = (uint16_t)(hi - lo + 1);
    }

    hwvp_cat(&p, &cap, &len,
        "uniform float4 vp;\n"
        "uniform float4 vsw[4];\n");
    hwvp_cat(&p, &cap, &len, "uniform float4 c[%d]; // c_lo %d\n", s_hwvp_cg_n, s_hwvp_cg_lo);
    hwvp_cat(&p, &cap, &len, "float4 kc(float i) { return (i >= 0.0 && i < %d.0) ? c[(int)i] : float4(0.0, 0.0, 0.0, 1.0); }\n", s_hwvp_cg_n);
    hwvp_cat(&p, &cap, &len,
        "float4 f4(float x) { return float4(x, x, x, x); }\n"
        "float4 rcc(float x) { float r = 1.0 / x; float ar = abs(r);\n"
        "  ar = (ar < 5.42101e-20) ? 5.42101e-20 : ((ar > 1.884467e+19) ? 1.884467e+19 : ar);\n"
        "  return f4(r < 0.0 ? -ar : ar); }\n"
        "float4 vexp(float x) { float fl = floor(x); return float4(exp2(fl), x - fl, exp2(x), 1.0); }\n"
        "float4 vlog(float x) { x = abs(x); if (x == 0.0) return float4(-3.402823e38, 1.0, -3.402823e38, 1.0);\n"
        "  float e = floor(log2(x)); return float4(e, x / exp2(e), log2(x), 1.0); }\n"
        "float4 vlit(float4 s) { float nl = max(s.x, 0.0), nh = max(s.y, 0.0); float pw = clamp(s.w, -127.9961, 127.9961);\n"
        "  return float4(1.0, nl, (s.x > 0.0) ? pow(nh, pw) : 0.0, 1.0); }\n"
        "void main(");
    for (i = 0; i < e->nin; i++) hwvp_cat(&p, &cap, &len, "float4 vin%u, ", i);
    hwvp_cat(&p, &cap, &len,
        "float4 out gl_Position : POSITION, float4 out v_t0 : TEXCOORD0, float4 out v_t1 : TEXCOORD1,\n"
        "  float4 out v_t2 : TEXCOORD2, float4 out v_t3 : TEXCOORD3, float4 out v_d0 : TEXCOORD4,\n"
        "  float4 out v_d1 : TEXCOORD5, float4 out v_fog : TEXCOORD6) {\n"
        "  float a0 = 0.0; float4 m, l;\n");
    /* (inputs straight from the guest's vertex buffers, gpu_batch_hw_direct:
     * a D3DCOLOR's bytes are B,G,R,A -- vsw says which inputs to turn round) */
    for (i = 0; i < e->nin; i++)
        hwvp_cat(&p, &cap, &len, "  if (vsw[%u].%c > 0.5) vin%u = vin%u.zyxw;\n", i / 4, "xyzw"[i % 4], i, i);
    for (i = 0; i < 16; i++) hwvp_cat(&p, &cap, &len, "  float4 R%u = f4(0.0);\n", i);
    for (i = 0; i < 13; i++) hwvp_cat(&p, &cap, &len, "  float4 o%u = %s;\n", i, i == 3 ? "f4(1.0)" : "float4(0.0, 0.0, 0.0, 1.0)");
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        hwvp_src_cg(&I->s[0], a, sizeof a);
        hwvp_src_cg(&I->s[1], b, sizeof b);
        hwvp_src_cg(&I->s[2], c, sizeof c);
        hwvp_cat(&p, &cap, &len, "  m = f4(0.0); l = f4(0.0);\n");
        switch (I->mac) {
        case MAC_MOV: hwvp_cat(&p, &cap, &len, "  m = %s;\n", a); break;
        case MAC_MUL: hwvp_cat(&p, &cap, &len, "  m = %s * %s;\n", a, b); break;
        case MAC_ADD: hwvp_cat(&p, &cap, &len, "  m = %s + %s;\n", a, c); break;
        case MAC_MAD: hwvp_cat(&p, &cap, &len, "  m = %s * %s + %s;\n", a, b, c); break;
        case MAC_DP3: hwvp_cat(&p, &cap, &len, "  m = f4(dot((%s).xyz, (%s).xyz));\n", a, b); break;
        case MAC_DPH: hwvp_cat(&p, &cap, &len, "  { float4 A_ = %s, B_ = %s; m = f4(dot(A_.xyz, B_.xyz) + B_.w); }\n", a, b); break;
        case MAC_DP4: hwvp_cat(&p, &cap, &len, "  m = f4(dot(%s, %s));\n", a, b); break;
        case MAC_DST: hwvp_cat(&p, &cap, &len, "  { float4 A_ = %s, B_ = %s; m = float4(1.0, A_.y * B_.y, A_.z, B_.w); }\n", a, b); break;
        case MAC_MIN: hwvp_cat(&p, &cap, &len, "  m = min(%s, %s);\n", a, b); break;
        case MAC_MAX: hwvp_cat(&p, &cap, &len, "  m = max(%s, %s);\n", a, b); break;
        case MAC_SLT: hwvp_cat(&p, &cap, &len, "  m = f4(1.0) - step(%s, %s);\n", b, a); break;
        case MAC_SGE: hwvp_cat(&p, &cap, &len, "  m = step(%s, %s);\n", b, a); break;
        case MAC_ARL: hwvp_cat(&p, &cap, &len, "  a0 = floor((%s).x + 0.001);\n", a); break;
        default: break;
        }
        switch (I->ilu) {
        case ILU_MOV: hwvp_cat(&p, &cap, &len, "  l = %s;\n", c); break;
        case ILU_RCP: hwvp_cat(&p, &cap, &len, "  l = f4(1.0 / (%s).x);\n", c); break;
        case ILU_RCC: hwvp_cat(&p, &cap, &len, "  l = rcc((%s).x);\n", c); break;
        case ILU_RSQ: hwvp_cat(&p, &cap, &len, "  l = f4(1.0 / sqrt(abs((%s).x)));\n", c); break;
        case ILU_EXP: hwvp_cat(&p, &cap, &len, "  l = vexp((%s).x);\n", c); break;
        case ILU_LOG: hwvp_cat(&p, &cap, &len, "  l = vlog((%s).x);\n", c); break;
        case ILU_LIT: hwvp_cat(&p, &cap, &len, "  l = vlit(%s);\n", c); break;
        default: break;
        }
        if (I->mac && I->mac != MAC_ARL && I->mac_mask && I->out_r < 16 && I->out_r != 12)
            hwvp_cat(&p, &cap, &len, "  R%u%s = m%s;\n", I->out_r, hwvp_mask(I->mac_mask, mk), mk);
        if (I->ilu && I->ilu_mask) {
            uint32_t d = I->mac ? 1 : I->out_r;
            if (d < 16 && d != 12) hwvp_cat(&p, &cap, &len, "  R%u%s = l%s;\n", d, hwvp_mask(I->ilu_mask, mk), mk);
        }
        if (I->o_mask && I->orb && I->oaddr < VP_OUTS)
            hwvp_cat(&p, &cap, &len, "  o%u%s = %s%s;\n", I->oaddr, hwvp_mask(I->o_mask, mk), I->omux ? "l" : "m", mk);
        if (I->final) break;
    }
    hwvp_cat(&p, &cap, &len,
        "  float w = o0.w; if (w == 0.0) w = 1e-30;\n"
        "  float4 P = float4(o0.xyz * w, w);\n"
        "  gl_Position = float4(P.x * vp.x - P.w, (P.y * vp.y - P.w) * vp.w, 2.0 * P.z * vp.z - P.w, P.w);\n"
        "  v_d0 = saturate(o3); v_d1 = saturate(o4); v_fog = float4(o5.x, 0.0, 0.0, 0.0);\n"
        "  v_t0 = o9; v_t1 = o10; v_t2 = o11; v_t3 = o12;\n"
        "}\n");
    if (getenv("RECOMP_VGL_VPLOG")) {
        static int n;
        if (n++ < 3) fprintf(stderr, "  [VGL] vertex program shader:\n%s\n", p);
    }
    return p;
}
#endif

static int hwvp_enabled(void)
{
    if (g_hwvp_on < 0) {
        /* set up even when the ini says cpu: the overlay can switch later */
        g_hwvp_on = 1;
        fprintf(stderr, "  [HWVP] vertex programs on the GPU: %s\n", g_hwvp_user ? "on" : "off");
    }
    return g_hwvp_on;
}

#if defined(__vita__)
static char *s_hwvp_fail_src;   /* (a copy of the source until it compiles: only while none has failed) */
static int s_hwvp_fail_keep = 1;
#endif
static void hwvp_poll(HwVp *e)
{
    if (e->state == 0 && e->vs) {
        int r = gpu_shader_done(&e->vs, "vertex program", e->hash);
        if (r > 0) { e->state = 1; g_hwvp_compiled++; }
        else if (r < 0) { e->state = 2; g_hwvp_failed++; }
#if defined(__vita__)
        if (r < 0 && s_hwvp_fail_src) {
            /* (the first one that fails, written out whole) */
            FILE *f = fopen("ux0:data/twotowers/hwvp_fail.txt", "w");
            if (f) { fputs(s_hwvp_fail_src, f); fclose(f); }
            fprintf(stderr, "  [HWVP] the failed shader is in ux0:data/twotowers/hwvp_fail.txt\n");
        }
        if (r && s_hwvp_fail_src) { free(s_hwvp_fail_src); s_hwvp_fail_src = NULL; s_hwvp_fail_keep = r > 0; }
#endif
    }
}

/* The shader for the current program, or NULL to use the CPU path. */
static HwVp *hwvp_lookup(void)
{
    uint32_t h, i;
    HwVp *e;
    if (!g_hwvp_user || !hwvp_enabled() || s_3d.mode != 2 || s_vp_slow || !s_vpd_len) return NULL;
    {   /* (v96) the same decoded program as the last draw's: no hash, no compare */
        static uint32_t last_rev; static HwVp *last_e;
        if (last_e && last_rev == s_vpd_rev && last_e->state == 1) return last_e;
        last_e = NULL;
        h = hwvp_hash((const uint32_t (*)[4])s_vpd_src, s_vpd_len);
        for (i = 0; i < (uint32_t)g_hwvp_n; i++) {
            e = g_hwvp[i];
            if (e->hash == h && e->len == s_vpd_len && !memcmp(e->words, s_vpd_src, s_vpd_len * 16)) {
                hwvp_poll(e);
                if (e->state == 1) { last_e = e; last_rev = s_vpd_rev; }
                return e->state == 1 ? e : NULL;
            }
        }
    }
    h = hwvp_hash((const uint32_t (*)[4])s_vpd_src, s_vpd_len);
    for (i = 0; i < (uint32_t)g_hwvp_n; i++) {
        e = g_hwvp[i];
        if (e->hash == h && e->len == s_vpd_len && !memcmp(e->words, s_vpd_src, s_vpd_len * 16)) {
            hwvp_poll(e);
            return e->state == 1 ? e : NULL;
        }
    }
    if (g_hwvp_n >= HWVP_MAX) return NULL;
    e = (HwVp *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->hash = h; e->len = s_vpd_len;
    memcpy(e->words, s_vpd_src, s_vpd_len * 16);
    {
#if defined(__vita__)
        char *src = (VWHERE("compiling a vertex program"), hwvp_translate_cg(e));
#else
        char *src = hwvp_translate(e);
#endif
        if (!src) e->state = 2;
        else {
#if defined(__vita__)
            if (s_hwvp_fail_keep && !s_hwvp_fail_src) s_hwvp_fail_src = strdup(src);
            e->src = strdup(src);
#endif
            e->vs = gpu_shader_start(GL_VERTEX_SHADER, src); free(src);
        }
    }
    g_hwvp[g_hwvp_n++] = e;
    hwvp_poll(e);
    return e->state == 1 ? e : NULL;
}

static void hwvp_pack_range(const HwVp *e, uint32_t lo, uint32_t hi)
{
    uint32_t k, i;
    for (k = lo; k < hi; k++) {
        float *d = g_xin + (size_t)k * 4 * e->nin;
        for (i = 0; i < e->nin; i++) fetch_attr_any(e->inreg[i], g_vidx[k], d + 4 * i);
    }
}

/* ---------------- combiner programs as GLSL ---------------- */

/* As nv2a_d3d11_ps.inc: each distinct combiner program written out as
 * straight-line code with its registers, mappings, operations, texture modes,
 * fog mode and alpha test fixed; its constants still come from PSC.
 * RECOMP_HW_PS=0 turns it off. */
typedef struct {
    PsKey key;
    uint32_t hash, state;             /* 0 compiling, 1 ready, 2 failed */
    GLuint fs;
    uint32_t tries, retry_at;         /* (PS Vita: a failed one compiled again later) */
} HwPs;

#define HWPS_MAX 1024
static HwPs *g_hwps[HWPS_MAX];
static volatile LONG g_hwps_n;
static int g_hwps_on = -1;
static uint32_t g_hwps_stat_hw, g_hwps_stat_sw, g_hwps_compiled, g_hwps_failed;

static const char *ps_cmap(int m, const char *x, char *b, size_t n)
{
    switch (m & 7) {
    case 0: snprintf(b, n, "max(%s, 0.0)", x); break;
    case 1: snprintf(b, n, "(1.0 - clamp(%s, 0.0, 1.0))", x); break;
    case 2: snprintf(b, n, "(2.0 * max(%s, 0.0) - 1.0)", x); break;
    case 3: snprintf(b, n, "(-2.0 * max(%s, 0.0) + 1.0)", x); break;
    case 4: snprintf(b, n, "(max(%s, 0.0) - 0.5)", x); break;
    case 5: snprintf(b, n, "(-max(%s, 0.0) + 0.5)", x); break;
    case 6: snprintf(b, n, "(%s)", x); break;
    default: snprintf(b, n, "(-(%s))", x); break;
    }
    return b;
}
static const char *ps_crgb(int c, char *b, size_t n)
{
    char x[32], t[96];
    if ((c >> 4) & 1) { snprintf(x, sizeof x, "R%d.a", c & 15); snprintf(b, n, "vec3(%s)", ps_cmap(c >> 8, x, t, sizeof t)); }
    else { snprintf(x, sizeof x, "R%d.rgb", c & 15); ps_cmap(c >> 8, x, b, n); }
    return b;
}
static const char *ps_calp(int c, char *b, size_t n)
{
    char x[32];
    snprintf(x, sizeof x, "R%d.%c", c & 15, ((c >> 4) & 1) ? 'a' : 'b');
    return ps_cmap(c >> 8, x, b, n);
}
static const char *ps_cop(int op, const char *x, char *b, size_t n)
{
    switch (op) {
    case 1: snprintf(b, n, "clamp((%s) - 0.5, -1.0, 1.0)", x); break;
    case 2: snprintf(b, n, "clamp((%s) * 2.0, -1.0, 1.0)", x); break;
    case 3: snprintf(b, n, "clamp(((%s) - 0.5) * 2.0, -1.0, 1.0)", x); break;
    case 4: snprintf(b, n, "clamp((%s) * 4.0, -1.0, 1.0)", x); break;
    case 6: snprintf(b, n, "clamp((%s) * 0.5, -1.0, 1.0)", x); break;
    default: snprintf(b, n, "clamp(%s, -1.0, 1.0)", x); break;
    }
    return b;
}

static void ps_gen_stex(char **p, size_t *cap, size_t *len, const PsKey *K, int k)
{
    int mode = K->smode[k], fl = K->sflags[k];
    if (mode == 0) { hwvp_cat(p, cap, len, "  R%d = vec4(0.0);\n", 8 + k); return; }
    if (mode == 4) { hwvp_cat(p, cap, len, "  R%d = clamp(v_t%d, 0.0, 1.0);\n", 8 + k, k); return; }
    if (!(fl & 1)) { hwvp_cat(p, cap, len, "  R%d = vec4(0.0, 0.0, 0.0, 1.0);\n", 8 + k); return; }
    hwvp_cat(p, cap, len, "  { vec2 uv = v_t%d.xy;\n", k);
    if (mode == 1) hwvp_cat(p, cap, len, "    if (v_t%d.w != 0.0 && v_t%d.w != 1.0) uv /= v_t%d.w;\n", k, k, k);
    if (!(fl & 4)) hwvp_cat(p, cap, len, "    uv *= sscale[%d].xy;\n", k);
    if (fl & 8) hwvp_cat(p, cap, len, "    R%d = zread(tx%d, uv); }\n", 8 + k, k);
    else hwvp_cat(p, cap, len, "    R%d = texture(tx%d, uv); }\n", 8 + k, k);
}

static const char *ps_dnorm(int m, const char *c, char *b, size_t n)
{
    if (m == 0) snprintf(b, n, "%s.rgb", c);
    else if (m == 3) snprintf(b, n, "dnorm(%s, 3)", c);
    else snprintf(b, n, "(%s.rgb * 2.0 - 1.0)", c);
    return b;
}

static char *hwps_translate(const PsKey *K)
{
    size_t cap = 8192, len = 0;
    char *p = (char *)malloc(cap), a[160], b2[160], c[160], d[160], t[256];
    int k;
    if (!p) return NULL;
    p[0] = 0;
    hwvp_cat(&p, &cap, &len, "%s", s_glsl_ps_head);
    hwvp_cat(&p, &cap, &len, "void main() {\n"
                             "  vec4 R0 = vec4(0.0), R1 = vec4(0.0), R2 = vec4(0.0), R3 = vec4(0.0), R4 = vec4(0.0), R5 = vec4(0.0), R6 = vec4(0.0), R7 = vec4(0.0);\n"
                             "  vec4 R8 = vec4(0.0), R9 = vec4(0.0), R10 = vec4(0.0), R11 = vec4(0.0), R12 = vec4(0.0), R13 = vec4(0.0), R14 = vec4(0.0), R15 = vec4(0.0);\n"
                             "  float dp1 = 0.0, dp2 = 0.0;\n");
    if (K->sflags[0] & 2) ps_gen_stex(&p, &cap, &len, K, 0);
    if (K->smode[1] == 17) hwvp_cat(&p, &cap, &len, "  dp1 = dot(v_t1.xyz, %s); R9 = vec4(0.0);\n", ps_dnorm(K->sdot[1], "R8", a, sizeof a));
    else if (K->sflags[1] & 2) ps_gen_stex(&p, &cap, &len, K, 1);
    if (K->smode[2] == 17)
        hwvp_cat(&p, &cap, &len, "  dp2 = dot(v_t2.xyz, %s); R10 = vec4(0.0);\n", ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
    else if (K->smode[2] == 9) {
        if (K->sflags[2] & 1)
            hwvp_cat(&p, &cap, &len, "  R10 = texture(tx2, vec2(dp1, dot(v_t2.xyz, %s)));\n",
                     ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R10 = vec4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[2] & 2) ps_gen_stex(&p, &cap, &len, K, 2);
    if (K->smode[3] == 9) {
        const char *nn = K->sinp[3] == 2 ? "R10" : K->sinp[3] == 1 ? "R9" : "R8";
        if (K->sflags[3] & 1)
            hwvp_cat(&p, &cap, &len, "  R11 = texture(tx3, vec2(dp2, dot(v_t3.xyz, %s)));\n", ps_dnorm(K->sdot[3], nn, a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R11 = vec4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[3] & 2) ps_gen_stex(&p, &cap, &len, K, 3);
    hwvp_cat(&p, &cap, &len, "  R4 = v_d0; R5 = v_d1;\n  R3 = vec4(fogc.rgb, 1.0);\n");
    if (K->fogen) {
        int fm = K->fogmode;
        hwvp_cat(&p, &cap, &len, "  { float dd = v_fog.x, f;\n");
        if (fm == 0x800 || fm == 0x802)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5;\n", fm == 0x802 ? "    dd = abs(dd);\n" : "");
        else if (fm == 0x801 || fm == 0x803)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5;\n", fm == 0x803 ? "    dd = abs(dd);\n" : "");
        else hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + dd * fogp.y - 1.0;\n", fm == 0x804 ? "    dd = abs(dd);\n" : "");
        hwvp_cat(&p, &cap, &len, "    R3.a = clamp(f, 0.0, 1.0); }\n");
    }
    hwvp_cat(&p, &cap, &len, "  R12.a = R8.a;\n  vec4 o;\n");
    if (K->misc[3] == 0) {
        hwvp_cat(&p, &cap, &len, "  o = %s * R4;\n", K->at_s0 ? "R8" : "vec4(1.0)");
    } else {
        for (k = 0; k < K->misc[0] && k < 8; k++) {
            const int *S = K->st[k];
            int f1y = S[9], op = S[10], aop = S[11];
            hwvp_cat(&p, &cap, &len, "  { R1 = st[%d].c0; R2 = st[%d].c1;\n", k, k);
            hwvp_cat(&p, &cap, &len, "    vec3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(S[0], a, sizeof a), ps_crgb(S[1], b2, sizeof b2), ps_crgb(S[2], c, sizeof c), ps_crgb(S[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    vec3 ab = %s, cd = %s;\n", (f1y & 2) ? "vec3(dot(A, B))" : "A * B", (f1y & 1) ? "vec3(dot(C, D))" : "C * D");
            if ((f1y & 4) || (f1y & 8))
                hwvp_cat(&p, &cap, &len, "    bool sel = %s;\n", K->misc[1] ? "R12.a >= 0.5" : "((int(R12.a * 255.0 + 0.5)) & 1) != 0");
            hwvp_cat(&p, &cap, &len, "    vec3 sm = %s;\n", (f1y & 4) ? "sel ? cd : ab" : "ab + cd");
            hwvp_cat(&p, &cap, &len, "    vec3 rab = %s;\n", ps_cop(op, "ab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    vec3 rcd = %s;\n", ps_cop(op, "cd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    vec3 rsm = %s;\n", ps_cop(op, "sm", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float a0 = %s, a1 = %s, a2 = %s, a3 = %s;\n",
                     ps_calp(S[4], a, sizeof a), ps_calp(S[5], b2, sizeof b2), ps_calp(S[6], c, sizeof c), ps_calp(S[7], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float aab = a0 * a1, acd = a2 * a3, asm_ = %s;\n", (f1y & 8) ? "sel ? acd : aab" : "aab + acd");
            hwvp_cat(&p, &cap, &len, "    float ra0 = %s;\n", ps_cop(aop, "aab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra1 = %s;\n", ps_cop(aop, "acd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra2 = %s;\n", ps_cop(aop, "asm_", t, sizeof t));
            if (S[12]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rab;\n", S[12]); if (S[19] & 2) hwvp_cat(&p, &cap, &len, "    R%d.a = rab.b;\n", S[12]); }
            if (S[13]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rcd;\n", S[13]); if (S[19] & 1) hwvp_cat(&p, &cap, &len, "    R%d.a = rcd.b;\n", S[13]); }
            if (S[14]) hwvp_cat(&p, &cap, &len, "    R%d.rgb = rsm;\n", S[14]);
            if (S[16]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra0;\n", S[16]);
            if (S[17]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra1;\n", S[17]);
            if (S[18]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra2;\n", S[18]);
            hwvp_cat(&p, &cap, &len, "  }\n");
        }
        if (K->fin1[3] == 0) {
            hwvp_cat(&p, &cap, &len, "  o = clamp(R12, 0.0, 1.0);\n");
        } else {
            hwvp_cat(&p, &cap, &len, "  R1 = ff0; R2 = ff1;\n");
            hwvp_cat(&p, &cap, &len, "  { vec3 E = %s, F = %s;\n    R15.rgb = E * F; }\n",
                     ps_crgb(K->fin1[0], a, sizeof a), ps_crgb(K->fin1[1], b2, sizeof b2));
            hwvp_cat(&p, &cap, &len, "  R14.rgb = R5.rgb + R12.rgb;\n");
            if (K->misc[2]) hwvp_cat(&p, &cap, &len, "  R14.rgb = min(R14.rgb, vec3(1.0));\n");
            hwvp_cat(&p, &cap, &len, "  { vec3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(K->fin0[0], a, sizeof a), ps_crgb(K->fin0[1], b2, sizeof b2), ps_crgb(K->fin0[2], c, sizeof c), ps_crgb(K->fin0[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    vec3 sa = clamp(A, 0.0, 1.0);\n    o.rgb = clamp(sa * B + (1.0 - sa) * C + D, 0.0, 1.0);\n");
            hwvp_cat(&p, &cap, &len, "    o.a = clamp(%s, 0.0, 1.0); }\n", ps_calp(K->fin1[2], a, sizeof a));
        }
    }
    if (K->at_en) {
        static const char *cmp[7] = { "false", "a8 < r", "a8 == r", "a8 <= r", "a8 > r", "a8 != r", "a8 >= r" };
        int f = K->at_func & 15;
        if (f < 7)
            hwvp_cat(&p, &cap, &len, "  { int a8 = int(clamp(o.a, 0.0, 1.0) * 255.0 + 0.5), r = atest.z;\n"
                                     "    if (!(%s)) discard; }\n", cmp[f]);
    }
    hwvp_cat(&p, &cap, &len, "  o_color = o;\n}\n");
    return p;
}

#if defined(__vita__)
/* hwps_translate in Cg (nv2a_gl_vita.inc has the helpers and the head) */
#define CG_Z4 "float4(0.0, 0.0, 0.0, 0.0)"
static void cg_gen_stex(char **p, size_t *cap, size_t *len, const PsKey *K, int k)
{
    int mode = K->smode[k], fl = K->sflags[k];
    if (mode == 0) { hwvp_cat(p, cap, len, "  R%d = " CG_Z4 ";\n", 8 + k); return; }
    if (mode == 4) { hwvp_cat(p, cap, len, "  R%d = saturate(v_t%d);\n", 8 + k, k); return; }
    if (!(fl & 1)) { hwvp_cat(p, cap, len, "  R%d = float4(0.0, 0.0, 0.0, 1.0);\n", 8 + k); return; }
    hwvp_cat(p, cap, len, "  { float2 uv = v_t%d.xy;\n", k);
    if (mode == 1) hwvp_cat(p, cap, len, "    if (v_t%d.w != 0.0 && v_t%d.w != 1.0) uv /= v_t%d.w;\n", k, k, k);
    if (!(fl & 4)) hwvp_cat(p, cap, len, "    uv *= ss%d.xy;\n", k);
    hwvp_cat(p, cap, len, "    R%d = tex2D(tx%d, uv); }\n", 8 + k, k);
}

static char *hwps_translate_cg(const PsKey *K)
{
    size_t cap = 8192, len = 0;
    char *p = (char *)malloc(cap), a[160], b2[160], c[160], d[160], t[256];
    int k;
    if (!p) return NULL;
    p[0] = 0;
    hwvp_cat(&p, &cap, &len, "%s", s_cg_ps_head);
    hwvp_cat(&p, &cap, &len, "float4 main(float4 v_t0 : TEXCOORD0, float4 v_t1 : TEXCOORD1, float4 v_t2 : TEXCOORD2,\n"
                             "  float4 v_t3 : TEXCOORD3, float4 v_d0 : TEXCOORD4, float4 v_d1 : TEXCOORD5,\n"
                             "  float4 v_fog : TEXCOORD6) : COLOR {\n");
    for (k = 0; k < 16; k++) hwvp_cat(&p, &cap, &len, "  float4 R%d = " CG_Z4 ";\n", k);
    hwvp_cat(&p, &cap, &len, "  float dp1 = 0.0, dp2 = 0.0;\n");
    if (K->sflags[0] & 2) cg_gen_stex(&p, &cap, &len, K, 0);
    if (K->smode[1] == 17) hwvp_cat(&p, &cap, &len, "  dp1 = dot(v_t1.xyz, %s); R9 = " CG_Z4 ";\n", cg_dnorm(K->sdot[1], "R8", a, sizeof a));
    else if (K->sflags[1] & 2) cg_gen_stex(&p, &cap, &len, K, 1);
    if (K->smode[2] == 17)
        hwvp_cat(&p, &cap, &len, "  dp2 = dot(v_t2.xyz, %s); R10 = " CG_Z4 ";\n", cg_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
    else if (K->smode[2] == 9) {
        if (K->sflags[2] & 1)
            hwvp_cat(&p, &cap, &len, "  R10 = tex2D(tx2, float2(dp1, dot(v_t2.xyz, %s)));\n",
                     cg_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R10 = float4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[2] & 2) cg_gen_stex(&p, &cap, &len, K, 2);
    if (K->smode[3] == 9) {
        const char *nn = K->sinp[3] == 2 ? "R10" : K->sinp[3] == 1 ? "R9" : "R8";
        if (K->sflags[3] & 1)
            hwvp_cat(&p, &cap, &len, "  R11 = tex2D(tx3, float2(dp2, dot(v_t3.xyz, %s)));\n", cg_dnorm(K->sdot[3], nn, a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R11 = float4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[3] & 2) cg_gen_stex(&p, &cap, &len, K, 3);
    hwvp_cat(&p, &cap, &len, "  R4 = v_d0; R5 = v_d1;\n  R3 = float4(fogc.rgb, 1.0);\n");
    if (K->fogen) {
        int fm = K->fogmode;
        hwvp_cat(&p, &cap, &len, "  { float dd = v_fog.x, f;\n");
        if (fm == 0x800 || fm == 0x802)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5;\n", fm == 0x802 ? "    dd = abs(dd);\n" : "");
        else if (fm == 0x801 || fm == 0x803)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5;\n", fm == 0x803 ? "    dd = abs(dd);\n" : "");
        else hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + dd * fogp.y - 1.0;\n", fm == 0x804 ? "    dd = abs(dd);\n" : "");
        hwvp_cat(&p, &cap, &len, "    R3.a = saturate(f); }\n");
    }
    hwvp_cat(&p, &cap, &len, "  R12.a = R8.a;\n  float4 o = " CG_Z4 ";\n");
    if (K->misc[3] == 0) {
        hwvp_cat(&p, &cap, &len, "  o = %s;\n", K->at_s0 ? "R8 * R4" : "R4");
    } else {
        for (k = 0; k < K->misc[0] && k < 8; k++) {
            const int *S = K->st[k];
            int f1y = S[9], op = S[10], aop = S[11];
            hwvp_cat(&p, &cap, &len, "  { R1 = c0_%d; R2 = c1_%d;\n", k, k);
            hwvp_cat(&p, &cap, &len, "    float3 A = %s, B = %s, C = %s, D = %s;\n",
                     cg_crgb(S[0], a, sizeof a), cg_crgb(S[1], b2, sizeof b2), cg_crgb(S[2], c, sizeof c), cg_crgb(S[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float3 ab = %s, cd = %s;\n", (f1y & 2) ? "f3(dot(A, B))" : "A * B", (f1y & 1) ? "f3(dot(C, D))" : "C * D");
            if ((f1y & 4) || (f1y & 8))
                hwvp_cat(&p, &cap, &len, "    bool sel = %s;\n", K->misc[1] ? "R12.a >= 0.5" : "fmod(floor(R12.a * 255.0 + 0.5), 2.0) >= 1.0");
            hwvp_cat(&p, &cap, &len, "    float3 sm = %s;\n", (f1y & 4) ? "sel ? cd : ab" : "ab + cd");
            hwvp_cat(&p, &cap, &len, "    float3 rab = %s;\n", cg_cop_v(op, "ab", 1, t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float3 rcd = %s;\n", cg_cop_v(op, "cd", 1, t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float3 rsm = %s;\n", cg_cop_v(op, "sm", 1, t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float a0 = %s, a1 = %s, a2 = %s, a3 = %s;\n",
                     cg_calp(S[4], a, sizeof a), cg_calp(S[5], b2, sizeof b2), cg_calp(S[6], c, sizeof c), cg_calp(S[7], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float aab = a0 * a1, acd = a2 * a3, asum = %s;\n", (f1y & 8) ? "sel ? acd : aab" : "aab + acd");
            hwvp_cat(&p, &cap, &len, "    float ra0 = %s;\n", cg_cop(aop, "aab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra1 = %s;\n", cg_cop(aop, "acd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra2 = %s;\n", cg_cop(aop, "asum", t, sizeof t));
            if (S[12]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rab;\n", S[12]); if (S[19] & 2) hwvp_cat(&p, &cap, &len, "    R%d.a = rab.b;\n", S[12]); }
            if (S[13]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rcd;\n", S[13]); if (S[19] & 1) hwvp_cat(&p, &cap, &len, "    R%d.a = rcd.b;\n", S[13]); }
            if (S[14]) hwvp_cat(&p, &cap, &len, "    R%d.rgb = rsm;\n", S[14]);
            if (S[16]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra0;\n", S[16]);
            if (S[17]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra1;\n", S[17]);
            if (S[18]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra2;\n", S[18]);
            hwvp_cat(&p, &cap, &len, "  }\n");
        }
        if (K->fin1[3] == 0) {
            hwvp_cat(&p, &cap, &len, "  o = saturate(R12);\n");
        } else {
            hwvp_cat(&p, &cap, &len, "  R1 = ff0; R2 = ff1;\n");
            hwvp_cat(&p, &cap, &len, "  { float3 E = %s, F = %s;\n    R15.rgb = E * F; }\n",
                     cg_crgb(K->fin1[0], a, sizeof a), cg_crgb(K->fin1[1], b2, sizeof b2));
            hwvp_cat(&p, &cap, &len, "  R14.rgb = R5.rgb + R12.rgb;\n");
            if (K->misc[2]) hwvp_cat(&p, &cap, &len, "  R14.rgb = min(R14.rgb, f3(1.0));\n");
            hwvp_cat(&p, &cap, &len, "  { float3 A = %s, B = %s, C = %s, D = %s;\n",
                     cg_crgb(K->fin0[0], a, sizeof a), cg_crgb(K->fin0[1], b2, sizeof b2), cg_crgb(K->fin0[2], c, sizeof c), cg_crgb(K->fin0[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float3 sa = saturate(A);\n    o.rgb = saturate(sa * B + (1.0 - sa) * C + D);\n");
            hwvp_cat(&p, &cap, &len, "    o.a = saturate(%s); }\n", cg_calp(K->fin1[2], a, sizeof a));
        }
    }
    if (K->at_en) {
        static const char *cmp[7] = { "false", "a8 < r", "a8 == r", "a8 <= r", "a8 > r", "a8 != r", "a8 >= r" };
        int f = K->at_func & 15;
        if (f < 7)
            hwvp_cat(&p, &cap, &len, "  { float a8 = floor(saturate(o.a) * 255.0 + 0.5), r = atest.z;\n"
                                     "    if (!(%s)) discard; }\n", cmp[f]);
    }
#if defined(__vita__)
    hwvp_cat(&p, &cap, &len, "  o.rgb *= (bmisc.z > 1.5) ? f3(1.0 - o.a) : ((bmisc.z > 0.5) ? f3(o.a) : f3(1.0));\n"
                             "  o.rgb *= bsrc.rgb;\n"
                             "  if (bmisc.x > 0.5) o.a = bmisc.y;\n");
#endif
    hwvp_cat(&p, &cap, &len, "  return o;\n}\n");
    if (getenv("RECOMP_VGL_PSLOG")) {
        static int n;
        if (n++ < 4) fprintf(stderr, "  [VGL] combiner shader:\n%s\n", p);
    }
    return p;
}
#endif

static uint32_t hwps_hash(const PsKey *k)
{
    const uint32_t *w = (const uint32_t *)k;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *k / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

static void hwps_poll(HwPs *e)
{
    if (e->state == 0 && e->fs) {
        int r = gpu_shader_done(&e->fs, "combiner program", e->hash);
        if (r > 0) { e->state = 1; g_hwps_compiled++; }
        else if (r < 0) { e->state = 2; g_hwps_failed++; }
    }
}

/* The fragment shader for the program in this key: the specialized one when
 * it is compiled, else the general one. */
#if defined(__vita__)
/* (PS Vita) sce's run-time Cg compiler fails now and then with an empty log
 * when memory is short (AC8BD4C6 in the battle, v62): the shader is tried
 * again every 200 presented frames, up to 8 times -- once it compiles,
 * vitaGL's shader cache keeps it for the next runs */
static void hwps_vita_retry(HwPs *e, uint32_t n)
{
    char *src;
    GLint ok = 0;
    if (e->state != 2 || e->tries >= 8) return;
    if (!e->retry_at) { e->retry_at = g_present_n + 200; return; }
    if (g_present_n < e->retry_at) return;
    e->tries++;
    e->retry_at = g_present_n + 200;
    src = hwps_translate_cg(&e->key);
    if (!src) return;
    if (e->fs) { glDeleteShader(e->fs); e->fs = 0; }
    e->fs = gpu_shader_start(GL_FRAGMENT_SHADER, src);
    free(src);
    if (e->fs) glGetShaderiv(e->fs, GL_COMPILE_STATUS, &ok);
    fprintf(stderr, "  [VGL] combiner shader %u (%08X) tried again (%u): %s\n", n, e->hash, e->tries, ok ? "compiled" : "FAILED");
    if (!ok) vgl_mem_report("combiner shader failed again");
    /* (hwps_poll takes it from here, as for a new one) */
    e->state = 0;
    if (g_hwps_failed > 0) g_hwps_failed--;
}
#endif
static GLuint hwps_lookup(const PsKey *k)
{
    uint32_t h, i, n;
    HwPs *e;
    static HwPs *last;
    if (g_hwps_on < 0) {
        const char *v = getenv("RECOMP_HW_PS");
        g_hwps_on = !(v && v[0] == '0');
        fprintf(stderr, "  [HWPS] specialized fragment shaders: %s\n", g_hwps_on ? "on" : "off");
    }
    if (!g_hwps_on) return g_fs_main;
    if (last && !memcmp(&last->key, k, sizeof *k)) {
#if defined(__vita__)
        hwps_vita_retry(last, (uint32_t)g_hwps_n);
#endif
        hwps_poll(last);
        if (last->state == 1) { g_hwps_stat_hw++; return last->fs; }
        g_hwps_stat_sw++;
        return g_fs_main;
    }
    h = hwps_hash(k);
    n = (uint32_t)g_hwps_n;
    for (i = 0; i < n; i++) {
        e = g_hwps[i];
        if (e->hash == h && !memcmp(&e->key, k, sizeof *k)) {
            last = e;
#if defined(__vita__)
            hwps_vita_retry(e, n);
#endif
            hwps_poll(e);
            if (e->state == 1) { g_hwps_stat_hw++; return e->fs; }
            g_hwps_stat_sw++;
            return g_fs_main;
        }
    }
    g_hwps_stat_sw++;
    if (n >= HWPS_MAX) return g_fs_main;
    e = (HwPs *)calloc(1, sizeof *e);
    if (!e) return g_fs_main;
    e->key = *k; e->hash = h;
    {
#if defined(__vita__)
        char *src = (VWHERE("compiling a combiner shader"), hwps_translate_cg(k));
#else
        char *src = hwps_translate(k);
#endif
        if (!src) e->state = 2;
        else {
            e->fs = gpu_shader_start(GL_FRAGMENT_SHADER, src);
#if defined(__vita__)
            {
                GLint ok = 0;
                if (e->fs) glGetShaderiv(e->fs, GL_COMPILE_STATUS, &ok);
                if (n < 8 || !ok) { fprintf(stderr, "  [VGL] combiner shader %u (%08X): %s\n", n, h, ok ? "compiled" : "FAILED"); fflush(stderr); }
                if (!ok) {
                    /* (the failed ones written out, with vitaGL's log and the free memory) */
                    static int nf;
                    char path[96], log[1024];
                    GLsizei ll = 0;
                    FILE *f;
                    log[0] = 0;
                    if (e->fs) glGetShaderInfoLog(e->fs, sizeof log - 1, &ll, log);
                    if (ll > 0 && ll < (GLsizei)sizeof log) log[ll] = 0;
                    fprintf(stderr, "  [VGL]   log: %s\n", log[0] ? log : "(empty)");
                    vgl_mem_report("combiner shader failed");
                    if (nf++ < 12) {
                        if (nf == 1) sceIoMkdir("ux0:data/twotowers/hwps_fail", 0777);
                        snprintf(path, sizeof path, "ux0:data/twotowers/hwps_fail/%08X.txt", h);
                        if ((f = fopen(path, "w")) != NULL) { fprintf(f, "log: %s\n\n%s", log, src); fclose(f); }
                    }
                }
            }
#endif
            free(src);
        }
    }
    g_hwps[n] = e;
    g_hwps_n = (LONG)(n + 1);
    last = e;
    hwps_poll(e);
    VWHERE("batch: textures/stages");
    return e->state == 1 ? e->fs : g_fs_main;
}

/* The program for vs with the batch's fragment shader: the specialized one
 * when its pairing is linked, else the general one's, else 0. */
static GLuint gpu_program_for(GLuint vs)
{
    GLuint fs = hwps_lookup(&g_pskey), pr = 0;
    if (fs != g_fs_main) pr = gpu_pair(vs, fs);
    if (!pr) {
        if (fs != g_fs_main) { g_hwps_stat_hw--; g_hwps_stat_sw++; }
        pr = vs == g_vs_main ? g_pr_main : gpu_pair(vs, g_fs_main);
    }
    return pr;
}

static void gpu_use(GLuint prog)
{
    if (g_sh.prog != prog) { glUseProgram(prog); g_sh.prog = prog; }
}

/* ---------------- batches ---------------- */

/* Draw the assembled batch with the program's GPU vertex shader; 0 when its
 * program is not linked yet (the caller draws it on the CPU path). */

#if defined(__vita__)
/* (diagnostics) the first draw of each vertex program written out: its Cg,
 * the state, the inputs of the first vertices and what the CPU path makes of
 * them, and the constants -- ux0:data/twotowers/hwvp/<hash>.txt */
static void hwvp_dump(HwVp *hv, uint32_t nverts)
{
    static int n, made;
    char path[96];
    FILE *f;
    uint32_t k, i;
    const float *vp = g_vub[VB_VSC][0];
    if (hv->dumped || n >= 64) return;
    hv->dumped = 1; n++;
    if (!made) { sceIoMkdir("ux0:data/twotowers/hwvp", 0777); made = 1; }
    snprintf(path, sizeof path, "ux0:data/twotowers/hwvp/%08X.txt", hv->hash);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "program %08X: %u instructions, %u inputs:", hv->hash, hv->len, hv->nin);
    for (i = 0; i < hv->nin; i++) fprintf(f, " v%u", hv->inreg[i]);
    fprintf(f, "\nvp %g %g %g %g; depth test %d mask %d func %u; cull %d; target %ux%u, zeta %d; %u vertices %u indices; fog %d\n",
            vp[0], vp[1], vp[2], vp[3], (int)s_3d.depth_test, (int)s_3d.depth_mask, (unsigned)s_3d.depth_func,
            (int)s_3d.cull_enable, g_cur_rt ? g_cur_rt->w : 0, g_cur_rt ? g_cur_rt->h : 0, g_cur_ds != NULL,
            nverts, g_nidx, (int)s_3d.fog_enable);
    for (k = 0; k < nverts && k < 4; k++) {
        SwVert v;
        int ok;
        fprintf(f, "vertex %u (index %u):\n", k, g_vidx[k]);
        for (i = 0; i < hv->nin; i++) {
            const float *x = g_xin + (size_t)k * 4 * hv->nin + 4 * i;
            fprintf(f, "  in v%u = %.9g %.9g %.9g %.9g\n", hv->inreg[i], x[0], x[1], x[2], x[3]);
        }
        ok = sw3d_transform(g_vidx[k], &v);
        fprintf(f, "  cpu: ok %d p %.9g %.9g %.9g %.9g; clip %.9g %.9g %.9g %.9g\n", ok, v.p[0], v.p[1], v.p[2], v.p[3],
                v.p[0] * vp[0] - v.p[3], (v.p[1] * vp[1] - v.p[3]) * vp[3], 2.0f * v.p[2] * vp[2] - v.p[3], v.p[3]);
        fprintf(f, "  cpu: d0 %.4g %.4g %.4g %.4g fog %.6g t0 %.6g %.6g %.6g %.6g\n", v.a[SA_D0], v.a[SA_D0 + 1], v.a[SA_D0 + 2],
                v.a[SA_D0 + 3], v.a[SA_FOG], v.a[SA_T0], v.a[SA_T0 + 1], v.a[SA_T0 + 2], v.a[SA_T0 + 3]);
    }
    fprintf(f, "constants (non-zero):\n");
    for (i = 0; i < VP_CONSTS; i++) {
        const float *c = s_3d.c[i];
        if (c[0] || c[1] || c[2] || c[3]) fprintf(f, "  c[%u] = %.9g %.9g %.9g %.9g\n", i, c[0], c[1], c[2], c[3]);
    }
    fprintf(f, "uniform c[] as uploaded (rows 96, 99): %g %g %g %g | %g %g %g %g\n",
            g_vub[VB_VPC][96][0], g_vub[VB_VPC][96][1], g_vub[VB_VPC][96][2], g_vub[VB_VPC][96][3],
            g_vub[VB_VPC][99][0], g_vub[VB_VPC][99][1], g_vub[VB_VPC][99][2], g_vub[VB_VPC][99][3]);
    if (g_vgl_cur) {
        int u;
        fprintf(f, "uniform locations:");
        for (u = 0; u < VUNI_N; u++) if (g_vgl_cur->loc[u] != -1) fprintf(f, " %s=%d", g_vuni[u].name, (int)g_vgl_cur->loc[u]);
        fprintf(f, "\n");
    }
    fprintf(f, "Cg:\n%s\n", hv->src ? hv->src : "(none)");
    fclose(f);
    fprintf(stderr, "  [HWVP] program %08X written to %s\n", hv->hash, path);
}
#endif

#if defined(__vita__)
/* (diagnostics) the first world draws drawn again into a 64x64 texture,
 * depth, blending and culling off: with a plain red fragment shader through
 * glDrawElements and through glDrawArrays, then with the draw's own fragment
 * program -- how many pixels each covers ([HWVP] probe). */
static GLuint g_pb_tex, g_pb_fbo, g_pb_fs;
static int g_pb_n;
static void hwvp_red_fs(void)
{
    static const char red_fs[] = "float4 main(float4 v_d0 : TEXCOORD4) : COLOR { return float4(1.0, 0.0, v_d0.x * 0.0, 1.0); }\n";
    if (!g_pb_fs) {
        const char *src = red_fs;
        g_pb_fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(g_pb_fs, 1, &src, NULL);
        glCompileShader(g_pb_fs);
    }
}
static void hwvp_probe_count(const char *what, HwVp *hv)
{
    const uint8_t *px;
    int i, red = 0, other = 0;
    unsigned sr = 0, sg = 0, sb = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFinish();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_pb_tex);
    px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
    if (!px) return;
    for (i = 0; i < 64 * 64; i++) {
        const uint8_t *c = px + 4 * i;
        if (c[0] == 0 && c[1] == 0xFF && c[2] == 0xFF) continue;   /* (cleared: cyan) */
        if (c[0] > 0xF0 && c[1] < 0x10 && c[2] < 0x10) red++;
        else { other++; sr += c[0]; sg += c[1]; sb += c[2]; }
    }
    fprintf(stderr, "  [HWVP] probe %08X %s: %d red, %d other pixels (mean %u %u %u) of 4096\n", hv->hash, what, red, other,
            other ? sr / other : 0, other ? sg / other : 0, other ? sb / other : 0);
    if (g_cap_on) cap_note_text("PROBE %08X %s: %d red, %d other pixels (mean %u %u %u) of 4096", hv->hash, what, red, other,
                                other ? sr / other : 0, other ? sg / other : 0, other ? sb / other : 0);
}
static void hwvp_probe_begin(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, g_pb_fbo);
    glViewport(0, 0, 64, 64);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
static void hwvp_probe(HwVp *hv, GLuint game_prog, uint32_t nverts)
{
    static const char red_fs[] = "float4 main(float4 v_d0 : TEXCOORD4) : COLOR { return float4(1.0, 0.0, v_d0.x * 0.0, 1.0); }\n";
    GLuint pr;
    uint32_t k, i;
    float *ex;
    (void)red_fs;
    if (!g_pb_tex) {
        g_pb_tex = gpu_tex2d(64, 64, 0);
        g_pb_fbo = gpu_fbo(g_pb_tex, 0);
    }
    hwvp_red_fs();
    fprintf(stderr, "  [HWVP] probe %08X: %u instructions, %u vertices, %u indices, nin %u, fog %d, depth test %d func %u, vp %g %g %g %g, c[96] %g %g %g %g\n",
            hv->hash, hv->len, nverts, g_nidx, hv->nin, (int)s_3d.fog_enable, (int)s_3d.depth_test, (unsigned)s_3d.depth_func,
            g_vub[VB_VSC][0][0], g_vub[VB_VSC][0][1], g_vub[VB_VSC][0][2], g_vub[VB_VSC][0][3],
            g_vub[VB_VPC][96][0], g_vub[VB_VPC][96][1], g_vub[VB_VPC][96][2], g_vub[VB_VPC][96][3]);
    pr = gpu_pair(hv->vs, g_pb_fs);
    if (!pr) { fprintf(stderr, "  [HWVP] probe: no program\n"); return; }
    /* 1: as drawn, red */
    hwvp_probe_begin();
    glUseProgram(pr);
    vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx);
    hwvp_probe_count("elements, red", hv);
    /* 2: the vertices in index order, glDrawArrays */
    ex = (float *)malloc((size_t)g_nidx * 16 * hv->nin + 16);
    if (ex && hv->nin) {
        for (k = 0; k < g_nidx; k++) memcpy(ex + (size_t)k * 4 * hv->nin, g_xin + (size_t)g_idx[k] * 4 * hv->nin, 16 * hv->nin);
        for (i = 0; i < hv->nin; i++) glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, (GLsizei)(16 * hv->nin), ex + 4 * i);
        hwvp_probe_begin();
        glUseProgram(pr);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_nidx);
        hwvp_probe_count("arrays, red", hv);
        for (i = 0; i < hv->nin; i++) glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, (GLsizei)(16 * hv->nin), g_xin + 4 * i);
    }
    free(ex);
    /* 3: with its own fragment program */
    hwvp_probe_begin();
    glUseProgram(game_prog);
    vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx);
    hwvp_probe_count("elements, its own fragment program", hv);
    {   /* what the CPU path makes of the same vertices: how many land inside the view */
        uint32_t in = 0, bad = 0;
        for (k = 0; k < nverts; k++) {
            SwVert v;
            if (!sw3d_transform(g_vidx[k], &v)) { bad++; continue; }
            if (v.p[3] > 0 && v.p[0] / v.p[3] >= 0 && v.p[0] / v.p[3] <= 640 && v.p[1] / v.p[3] >= 0 && v.p[1] / v.p[3] <= 480) in++;
        }
        fprintf(stderr, "  [HWVP] probe %08X cpu: %u of %u vertices inside the 640x480 view, %u dropped\n", hv->hash, in, nverts, bad);
    }
    /* (the frame's own state again) */
    gpu_sh_reset();
    gpu_bind_targets(s_3d.depth_test || s_3d.stencil_test);
    gpu_set_raster();
    gpu_bind_stages();
    fflush(stderr);
}
#endif
#if defined(__vita__)
/* (the [VGLPERF] timers: vgl_perf_frame) */
#define VP_TIMERS_FWD
#endif
#if defined(__vita__)
/* (the [VGLPERF] timers: vgl_perf_frame) */
#include <psp2/kernel/processmgr.h>
enum { VP_BATCH, VP_TARGETS, VP_XFORM, VP_STAGES, VP_DRAW, VP_PRESENT, VP_IDX, VP_LOOKUP, VP_PROG, VP_STATE, VP_N };
static unsigned g_vpd_why[4], g_vpd_cmp, g_vpd_inl;
static float *g_cmp_buf[16];
static uint32_t g_cmp_cap[16];

static uint64_t g_vp_us[VP_N];
/* (v98) the timers are a system call each, some twenty a draw (~1500 draws a
 * frame): off unless RECOMP_VGL_TIMERS=1 -- the [RTPROF] sampler tells where
 * the time goes without them */
extern int g_vgl_timers;
#define VP_T0(v) SceUInt64 v = g_vgl_timers ? sceKernelGetProcessTimeWide() : 0
#define VP_ADD(k, v) (g_vgl_timers ? (void)(g_vp_us[k] += sceKernelGetProcessTimeWide() - (v)) : (void)0)
#endif
static int gpu_batch_hw(HwVp *hv, uint32_t nverts)
{
    uint32_t stride = 16 * hv->nin, vb_at = 0, ib_at;
    uint32_t k;
    GLuint prog;
#if defined(__vita__)
    { VWHERE("batch: textures/stages"); VP_T0(t); gpu_stages_vgl(); VP_ADD(VP_STAGES, t); }
    { VWHERE("batch: program"); VP_T0(t); prog = gpu_program_for(hv->vs); VP_ADD(VP_PROG, t); VWHERE("batch: vertex attributes"); }
#else
    gpu_bind_stages();
    prog = gpu_program_for(hv->vs);
#endif
    if (!prog) return 0;
#if defined(__vita__)
    /* (PS Vita) the inputs unpacked on the CPU, drawn from client arrays;
     * the constants through the uniform array c */
    (void)vb_at; (void)ib_at;
    {
        VP_T0(t);
        if (hv->nin) {
            size_t need = (size_t)nverts * 4 * hv->nin;
            if (need > g_xin_cap) {
                float *n = (float *)realloc(g_xin, need * sizeof(float) * 2);
                if (!n) return 1;
                g_xin = n; g_xin_cap = (uint32_t)(need * 2);
            }
            hwvp_pack_range(hv, 0, nverts);
        }
        VP_ADD(VP_XFORM, t);
    }
    if (g_cap_on && g_cap_mode == 3 && g_cur_ds && s_3d.depth_test) {
        /* (capture m3) the depth cleared to 0 again just before the draw, in its own scene */
        vgl_zclear_quad(1, 0.0f, 0);
        gpu_sh_reset();
        gpu_bind_targets(1);
        gpu_set_raster();
        gpu_bind_stages();
    }
    glBindVertexArray(g_vao_x);
    g_sh.vao = 2;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    {
        uint32_t kmax = vgl_va_top();
        if (kmax < hv->nin) kmax = hv->nin;
        for (k = 0; k < kmax; k++) {
            if (k < hv->nin) {
                glEnableVertexAttribArray(k);
                glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, g_xin + 4 * k);
            } else glDisableVertexAttribArray(k);
        }
    }
    VWHERE("batch: vertex constants");
    gpu_vpc_sync();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    static int probe_on = -1;
    if (probe_on < 0) probe_on = getenv("RECOMP_VGL_PROBE") != NULL;
    if (probe_on && !hv->probed && g_pb_n < 64 && hv->len > 3 && g_cur_rt && g_cur_rt->w >= 640 && g_nidx >= 6) {
        hv->probed = 1;
        g_pb_n++;
        hwvp_probe(hv, prog, nverts);
        glBindVertexArray(g_vao_x);
        g_sh.vao = 2;
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    {   /* (v90) THE HUD BUG. vsw (VSC rows 4-7) tells the vertex program
         * which inputs are D3DCOLORs to turn round (B,G,R,A): only the direct
         * path (gpu_batch_hw_direct) set it, and here -- the inputs already
         * floats -- it was left as the last direct draw had it. After a
         * direct draw with a D3DCOLOR in the same input slot (the particles
         * just before the HUD, in most frames of a battle) the HUD's own
         * inputs were turned round: x <-> z of the position, the bar drawn
         * at x = 1, the HUD, the haze, the pause menu gone; with other draws
         * last, there. Since v74 (the direct path). */
        int k2, changed = 0;
        for (k2 = 0; k2 < 16; k2++) if (g_vub[VB_VSC][4 + k2 / 4][k2 % 4] != 0.0f) { g_vub[VB_VSC][4 + k2 / 4][k2 % 4] = 0.0f; changed = 1; }
        if (changed) g_vub_gen[VB_VSC]++;
    }
    VWHERE("batch: GL state"); VP_T0(t_st);
    gpu_set_blend(0);
    gpu_set_depth(g_cur_ds != NULL);
    gpu_use(prog);
    VP_ADD(VP_STATE, t_st);
    if (g_cap_on && g_cap_mode == 1 && s_3d.depth_test) { glDepthFunc(GL_ALWAYS); g_sh.ds_ok = 0; }
    if (g_cap_on && g_cap_mode == 2 && s_3d.depth_test) { glDepthFunc(GL_LEQUAL); g_sh.ds_ok = 0; }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    { VP_T0(t); vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx); VP_ADD(VP_DRAW, t); g_vp_draws++; }
    if (g_cur_ds) vgl_depth_keep();
    if (!hv->dumped) hwvp_dump(hv, nverts);
    g_vp_hw++; g_cap_hw++;
    if (g_cap_on && hv->nin) {
        uint32_t q, nshow = nverts < 4 ? nverts : 4;
        for (q = 0; q < nshow; q++) {
            const float *v = g_xin + (size_t)q * 4 * hv->nin;
            cap_note_text("  vertex %u: in0 %.2f %.2f %.2f %.2f%s", q, v[0], v[1], v[2], v[3], hv->nin > 1 ? "" : "");
            if (hv->nin > 1) cap_note_text("            in1 %.3f %.3f %.3f %.3f", v[4], v[5], v[6], v[7]);
        }
        {   /* (v80) what the vertex program makes of vertex 0 (the CPU
             * interpreter, same program and constants): where it lands, its
             * colour and alpha; and the combiners with their constants --
             * whether a draw that changes no pixel was given alpha 0 by the
             * game itself */
            float in[16][4], out[VP_OUTS][4];
            uint32_t k;
            memset(in, 0, sizeof in);
            for (k = 0; k < 16; k++) in[k][3] = 1.0f;
            for (k = 0; k < hv->nin && k < 16; k++) if (hv->inreg[k] < 16) memcpy(in[hv->inreg[k]], g_xin + 4 * k, 16);
            if (s_3d.mode == 2) {
                vp_run_fast(in, out);
                for (k = 0; k < hv->nin; k++) cap_note_text("    in reg %u: %.3f %.3f %.3f %.3f", hv->inreg[k], in[hv->inreg[k]][0], in[hv->inreg[k]][1], in[hv->inreg[k]][2], in[hv->inreg[k]][3]);
                cap_note_text("  VP out v0: pos %.2f %.2f %.2f %.3f  D0 %.3f %.3f %.3f %.3f  T0 %.3f %.3f %.3f %.3f",
                              out[0][0], out[0][1], out[0][2], out[0][3], out[3][0], out[3][1], out[3][2], out[3][3],
                              out[9][0], out[9][1], out[9][2], out[9][3]);
            }
            cap_note_text("  combiners: ctl %08X final %08X %08X shader %08X; stage0 c %08X/%08X a %08X/%08X f0 %08X f1 %08X; stage1 c %08X/%08X a %08X/%08X f0 %08X f1 %08X",
                          s_3d.comb_ctl, s_3d.spec_fog0, s_3d.spec_fog1, s_3d.shader_prog,
                          s_3d.c_icw[0], s_3d.c_ocw[0], s_3d.a_icw[0], s_3d.a_ocw[0], s_3d.factor0[0], s_3d.factor1[0],
                          s_3d.c_icw[1], s_3d.c_ocw[1], s_3d.a_icw[1], s_3d.a_ocw[1], s_3d.factor0[1], s_3d.factor1[1]);
        }
    }
    cap_note_draw("hw", hv->hash, g_nidx);
#if defined(__vita__)
    /* (v84) in the capture, right after the HUD's first draw: two markers in
     * the top-left corner -- magenta by vitaGL's glClear (scissored), green
     * by our own quad -- to tell, in a frame whose tail draws nothing, whether
     * anything at all still reaches the target */
    if (g_cap_on && s_stage[0].enabled && s_stage[0].t.offset == 0x8148A000u && !g_cap_marked) {
        g_cap_marked = 1;
        /* (v91) the HUD draw again into a 64x64 probe target, read back:
         * its vertex program with a plain red fragment shader (elements,
         * then arrays), then with its own fragment shader -- red pixels and
         * none: the vertex side is right, the fragment side is not; none and
         * none: the vertex side. And the uniforms as they are sent. */
        cap_note_text("UNIFORMS: vsw %g %g %g %g / %g %g %g %g; atest %g %g %g %g; gfb %g %g %g %g; bsrc %g %g %g %g; bmisc %g %g %g %g",
                      g_vub[VB_VSC][4][0], g_vub[VB_VSC][4][1], g_vub[VB_VSC][4][2], g_vub[VB_VSC][4][3],
                      g_vub[VB_VSC][5][0], g_vub[VB_VSC][5][1], g_vub[VB_VSC][5][2], g_vub[VB_VSC][5][3],
                      g_vub[VB_PSC][69][0], g_vub[VB_PSC][69][1], g_vub[VB_PSC][69][2], g_vub[VB_PSC][69][3],
                      g_vub[VB_PSC][74][0], g_vub[VB_PSC][74][1], g_vub[VB_PSC][74][2], g_vub[VB_PSC][74][3],
                      g_vub[VB_PSC][72][0], g_vub[VB_PSC][72][1], g_vub[VB_PSC][72][2], g_vub[VB_PSC][72][3],
                      g_vub[VB_PSC][73][0], g_vub[VB_PSC][73][1], g_vub[VB_PSC][73][2], g_vub[VB_PSC][73][3]);
        hwvp_probe(hv, prog, nverts);
        {   /* (v85) the same HUD draw once more, as it is but for the window
             * z (vp.z = 0: no near/far clipping by z / clip max) and no depth
             * test: if the bar shows in a frame without the HUD, that was it */
            float zs = g_vub[VB_VSC][0][2];
            cap_note_text("REDRAW of the HUD bar: vp %.6g %.6g %.9g %.3g, clip min %.6g max %.6g", g_vub[VB_VSC][0][0], g_vub[VB_VSC][0][1], zs, g_vub[VB_VSC][0][3], s_clip_min, s_clip_max);
            g_vub[VB_VSC][0][2] = 0.0f; g_vub_gen[VB_VSC]++;
            glDisable(GL_DEPTH_TEST);
            vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx);
            {   /* (v86) copies of the bar lower down: y+40 blending off,
                 * y+80 a white texture in place of its own, y+120 both --
                 * which one shows tells whether the bar's alpha (its texture)
                 * is what is missing */
                static GLuint white;
                uint32_t q, k0 = 0, step;
                for (q = 0; q < hv->nin; q++) if (hv->inreg[q] == 0) k0 = q;
                if (!white) {
                    static const uint32_t px[16] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                                     0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
                    glGenTextures(1, &white);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, white);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                {   GLint was = 0;
                    glActiveTexture(GL_TEXTURE0);
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &was);
                    cap_note_text("REDRAW: the bar's texture on unit 0 is GL texture %d; GL error before %04X", (int)was, (unsigned)glGetError());
                    /* (v89) y+40 as it is with blending off; y+80 the same
                     * after every uniform of the program is sent to vitaGL
                     * again (our cache of what it holds forgotten); y+120
                     * the same after vitaGL is made to switch program away
                     * and back (its own per-program state redone) */
                    GLuint myprog = g_vgl_prog;
                    glDisable(GL_BLEND);
                    for (step = 1; step <= 3; step++) {
                        for (q = 0; q < nverts; q++) g_xin[(size_t)q * 4 * hv->nin + 4 * k0 + 1] += 40.0f;
                        if (step >= 2 && g_vgl_cur) memset(g_vgl_cur->gen, 0, sizeof g_vgl_cur->gen);
                        if (step == 3 && g_cc_prog) {
                            vgl_use(g_cc_prog);
                            vgl_use(myprog);
                            if (g_vgl_cur) memset(g_vgl_cur->gen, 0, sizeof g_vgl_cur->gen);
                        }
                        glBindTexture(GL_TEXTURE_2D, (GLuint)was);
                        vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx);
                    }
                    for (q = 0; q < nverts; q++) g_xin[(size_t)q * 4 * hv->nin + 4 * k0 + 1] -= 120.0f;
                    glBindTexture(GL_TEXTURE_2D, (GLuint)was);
                    glEnable(GL_BLEND);
                    cap_note_text("REDRAW: copies (blending off) at y+40 as is, y+80 all uniforms sent again, y+120 also program switched away and back; GL error %04X", (unsigned)glGetError());
                }
            }
            g_vub[VB_VSC][0][2] = zs; g_vub_gen[VB_VSC]++;
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(4, (GLint)g_cur_rt->ph - 12, 8, 8);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(16, (GLint)g_cur_rt->ph - 12, 8, 8);
        vgl_cclear_quad(0.0f, 1.0f, 0.0f, 1.0f, 1, 1, 1, 1);
        gpu_sh_reset();
        cap_note_text("MARKERS drawn (magenta glClear at 4,4; green quad at 16,4; 8x8)");
    }
#endif
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_3d.verts += nverts;
    g_hwvp_stat_hw++;
    return 1;
#endif
    if (hv->nin) {
        size_t need = (size_t)nverts * 4 * hv->nin;
        if (need > g_xin_cap) {
            float *n = (float *)realloc(g_xin, need * sizeof(float) * 2);
            if (!n) return 1;
            g_xin = n; g_xin_cap = (uint32_t)(need * 2);
        }
        hwvp_pack_range(hv, 0, nverts);
        glBindVertexArray(g_vao_x);
        g_sh.vao = 2;
        vb_at = gpu_ring(&g_rvbx, g_xin, (uint32_t)(need * sizeof(float)), stride);
        if (vb_at == 0xFFFFFFFFu) return 1;
        for (k = 0; k < 16; k++) {
            if (k < hv->nin) {
                glEnableVertexAttribArray(k);
                glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (const void *)(uintptr_t)(vb_at + 16 * k));
            } else glDisableVertexAttribArray(k);
        }
    } else {
        glBindVertexArray(g_vao_empty);
        g_sh.vao = 3;
    }
    ib_at = gpu_ring(&g_rib, g_idx, g_nidx * 4, 4);
    if (ib_at == 0xFFFFFFFFu) return 1;
    VWHERE("batch: vertex constants");
    gpu_vpc_sync();
    gpu_set_blend(0);
    gpu_set_depth(g_cur_ds != NULL);
    gpu_use(prog);
    glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, (const void *)(uintptr_t)ib_at, 0);
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_3d.verts += nverts;
    g_hwvp_stat_hw++;
    return 1;
}

static int g_null3_draw;   /* (RECOMP_GPU_NULL=3) */
#if defined(__vita__)
/* (diagnostics) one frame of the level written out as pictures: a render
 * target each time the game leaves it, then every surface at the present --
 * ux0:data/twotowers/cap/<n>_<what>_<base>_<w>x<h>.tga. It starts 15 frames
 * after the first draw with fog (RECOMP_VGL_CAPTURE=0 off, =n after n frames). */

/* (v61) a fixed 64 KB buffer: the v60 capture's 1.2 MB malloc failed (the
 * heap is nearly full in the battle) and every picture came out empty */
#define CAP_CHUNK (64 * 1024)
static uint8_t g_cap_chunk[CAP_CHUNK];
static int cap_write_bgra(FILE *f, const uint8_t *px, uint32_t n)
{
    uint32_t i = 0, k, m;
    while (i < n) {
        m = n - i; if (m > CAP_CHUNK / 4) m = CAP_CHUNK / 4;
        for (k = 0; k < m; k++) {
            const uint8_t *s0 = px + 4 * (size_t)(i + k);
            g_cap_chunk[4*k] = s0[2]; g_cap_chunk[4*k+1] = s0[1]; g_cap_chunk[4*k+2] = s0[0]; g_cap_chunk[4*k+3] = s0[3];
        }
        if (fwrite(g_cap_chunk, 1, m * 4, f) != m * 4) return 0;
        i += m;
    }
    return 1;
}
static void cap_save(const GSurf *sf, const char *what)
{
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    char path[128];
    FILE *f;
    const uint8_t *px;
    uint8_t hd[18];
    uint32_t i, n;
    uint8_t *row;
    if (!sf || !sf->tex || g_cap_n >= 99 || (g_cap_n >= 92 && strcmp(what, "present"))) return;
    fprintf(stderr, "  [CAP] saving %08X %ux%u (%ux%u)\n", sf->base, sf->w, sf->h, sf->pw, sf->ph); fflush(stderr);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gpu_sh_reset();
    glFinish();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sf->tex);
    px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
    if (!px) return;
    if (g_cap_n == 0) { sceIoMkdir("ux0:data/twotowers/cap", 0777); sceIoMkdir(g_cap_dir, 0777); }
    snprintf(path, sizeof path, "%s/%02d_m%d_%s_%08X_%ux%u%s.tga", g_cap_dir, g_cap_n++, g_cap_mode, what, sf->base, sf->pw, sf->ph,
             sf->is_depth ? "_zeta" : "");
    f = fopen(path, "wb");
    if (!f) return;
    memset(hd, 0, sizeof hd);
    hd[2] = 2; hd[12] = sf->pw & 0xFF; hd[13] = sf->pw >> 8; hd[14] = sf->ph & 0xFF; hd[15] = sf->ph >> 8; hd[16] = 32; hd[17] = 8;
    fwrite(hd, 1, 18, f);
    n = sf->pw * sf->ph;
    /* (v60: the whole picture in one write -- row by row, 480 writes, took
     * 3 to 4 s a picture on the memory card: the v58/v59 captures looked
     * like a freeze) */
    (void)row;
    if (!cap_write_bgra(f, px, n)) fprintf(stderr, "  [CAP] write failed (memory card full?)\n");
    fclose(f);
    fprintf(stderr, "  [CAP] %s (gpu draws so far %d, cpu draws %d) in %u ms\n", path, g_cap_hw, g_cap_cpu,
            (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
    fflush(stderr);
}
#endif

#if defined(__vita__)
/* (PS Vita) where the render thread's time goes: [VGLPERF] every 120
 * presented frames (gpu_present) */
static void vgl_perf_frame(void)
{
    static unsigned n;
    static SceUInt64 t0;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (!t0) t0 = now;
    if (++n < 120) return;
    {
        double f = (double)n, ms = (double)(now - t0) / 1000.0 / f;
        uint64_t in = g_vp_us[VP_TARGETS] + g_vp_us[VP_XFORM] + g_vp_us[VP_STAGES] + g_vp_us[VP_DRAW];
        fprintf(stderr, "  [VGLPERF] per frame (%u frames): %.1f ms in all; batches %.1f ms = targets %.1f, vertex transform %.1f, "
                "textures %.1f, draw calls %.1f, rest %.1f; present %.1f; outside batches %.1f\n", n, ms,
                g_vp_us[VP_BATCH] / 1000.0 / f, g_vp_us[VP_TARGETS] / 1000.0 / f, g_vp_us[VP_XFORM] / 1000.0 / f,
                g_vp_us[VP_STAGES] / 1000.0 / f, g_vp_us[VP_DRAW] / 1000.0 / f,
                (double)(g_vp_us[VP_BATCH] - (in < g_vp_us[VP_BATCH] ? in : g_vp_us[VP_BATCH])) / 1000.0 / f,
                g_vp_us[VP_PRESENT] / 1000.0 / f,
                ms - (g_vp_us[VP_BATCH] + g_vp_us[VP_PRESENT]) / 1000.0 / f);
        {   /* (the sampling profiler's report, twotowers/src/prof_vita.c) */
            extern const char *prof_take_report(void) __attribute__((weak));
            extern int prof_alive(void) __attribute__((weak));
            extern const char *prof_start2(void) __attribute__((weak));
            const char *r = prof_start2 ? prof_start2() : NULL;
            if (r) fputs(r, stderr);
            r = prof_take_report ? prof_take_report() : NULL;
            if (r) fputs(r, stderr);
            else if (prof_alive && !prof_alive()) fprintf(stderr, "  [PROF] the sampler has not run\n");
            {
                extern const char *prof_take_rt_report(void) __attribute__((weak));
                r = prof_take_rt_report ? prof_take_rt_report() : NULL;
                if (r) fputs(r, stderr);
            }
            {
                extern const char *prof_take_inc_report(void) __attribute__((weak));
                r = prof_take_inc_report ? prof_take_inc_report() : NULL;
                if (r) fputs(r, stderr);
            }
        }
        fprintf(stderr, "  [VGLPERF] more per frame: %.0f draws straight from the guest's buffers; indices %.1f ms, program lookup %.1f ms, GL program %.1f ms, GL state %.1f ms\n",
                g_vp_direct / f, g_vp_us[VP_IDX] / 1000.0 / f, g_vp_us[VP_LOOKUP] / 1000.0 / f, g_vp_us[VP_PROG] / 1000.0 / f, g_vp_us[VP_STATE] / 1000.0 / f);
        if (g_vgl_timers) fprintf(stderr, "  [VGLBLK] per frame, wall ms / of them not running: draws %.1f / %.1f (x8 of %u sampled), targets %.1f / %.1f (%u), clears %.1f / %.1f (%u), copies %.1f / %.1f (%u), present %.1f / %.1f\n",
                g_bk_wall[BK_DRAW] * 8 / 1000.0 / f, ((double)g_bk_wall[BK_DRAW] - (double)g_bk_run[BK_DRAW]) * 8 / 1000.0 / f, g_bk_n[BK_DRAW],
                g_bk_wall[BK_TARGETS] / 1000.0 / f, ((double)g_bk_wall[BK_TARGETS] - (double)g_bk_run[BK_TARGETS]) / 1000.0 / f, g_bk_n[BK_TARGETS],
                g_bk_wall[BK_CLEAR] / 1000.0 / f, ((double)g_bk_wall[BK_CLEAR] - (double)g_bk_run[BK_CLEAR]) / 1000.0 / f, g_bk_n[BK_CLEAR],
                g_bk_wall[BK_BLIT] / 1000.0 / f, ((double)g_bk_wall[BK_BLIT] - (double)g_bk_run[BK_BLIT]) / 1000.0 / f, g_bk_n[BK_BLIT],
                g_bk_wall[BK_PRESENT] / 1000.0 / f, ((double)g_bk_wall[BK_PRESENT] - (double)g_bk_run[BK_PRESENT]) / 1000.0 / f);
        memset(g_bk_wall, 0, sizeof g_bk_wall); memset(g_bk_run, 0, sizeof g_bk_run); memset(g_bk_n, 0, sizeof g_bk_n);
        fprintf(stderr, "  [VGLPERF] draws joined per frame: %.0f into %.0f lists; vertices read in place: %.0f draws\n", g_jn_joined / f, g_jn_runs / f, g_ip_draws / f);
        fprintf(stderr, "  [VCOPY1] vertices copied once: %.0f draws a frame, %.0f KB (vitaGL's way %.0f KB), %u back to client arrays\n",
                g_dz_draws / f, (double)g_dz_bytes / 1024.0 / f, (double)g_dz_old / 1024.0 / f, g_dz_fb);
        g_dz_draws = g_dz_fb = 0; g_dz_bytes = g_dz_old = 0;
        fprintf(stderr, "  [VCACHE] per frame: %.0f spans drawn from the cache (%.0f KB not copied), %.0f copied (%.0f changed), %.0f new; %.0f KB read to check; %.0f triangle lists kept (%.0f KB), %.0f new; cache %u of %u KB, %u spans; %u went dynamic, %u dropped for room, %u found no room\n",
                g_vc_hit / f, (double)g_vc_hit_b / 1024.0 / f, g_vc_miss / f, g_vc_chg / f, g_vc_new / f, (double)g_vc_hash_b / 1024.0 / f,
                g_ix_hit / f, (double)g_ix_hit_b / 1024.0 / f, g_ix_new / f, g_vc_used >> 10, g_vc_size >> 10, g_vc_entries, g_vc_dyn, g_vc_evict, g_vc_nomem);
        g_vc_hit = g_vc_miss = g_vc_new = g_vc_chg = g_vc_dyn = g_vc_nomem = g_vc_evict = g_ix_hit = g_ix_new = g_ix_miss = 0;
        g_vc_hit_b = g_vc_hash_b = g_ix_hit_b = 0;
        {   /* (v179) the render thread's wake-ups by the game (a system call each) and its short waits */
            extern volatile unsigned g_rt_spin_hits, g_rt_spin_miss, g_rt_wakes;
            fprintf(stderr, "  [RTWAKE] per frame: %.0f wake-ups by the game, %.0f waits ended by a kick while awake, %.0f went to sleep\n",
                    g_rt_wakes / f, g_rt_spin_hits / f, g_rt_spin_miss / f);
            g_rt_wakes = g_rt_spin_hits = g_rt_spin_miss = 0;
        }
        g_ip_draws = 0;
        g_jn_joined = g_jn_runs = 0;
        fprintf(stderr, "  [VGLPERF] joinable draws per frame: %.0f with nothing changed since the draw before (%.0f of them on the same vertex buffers), %.0f with only constants changed; %.0f of 2 triangles or fewer\n",
                g_mg_n[0] / f, g_mg_n[1] / f, g_mg_n[2] / f, g_mg_n[3] / f);
        memset(g_mg_n, 0, sizeof g_mg_n);
        if (g_shc_n) fprintf(stderr, "  [VGLPERF] shaders compiled in these frames: %u, %.1f ms in all (%.1f ms each)\n", g_shc_n, g_shc_us / 1000.0, g_shc_us / 1000.0 / g_shc_n);
        if (g_shc_hit || g_shc_written) fprintf(stderr, "  [VGLPERF] shader cache: %u read from the card, %u written, so far\n", g_shc_hit, g_shc_written);
        g_shc_n = 0; g_shc_us = 0;
        fprintf(stderr, "  [VGLPERF] texture lookups per frame: %.0f found again this frame, %.0f looked up (%.0f of them pushed out by another), %.0f decode-cache scans, %.0f GL slot scans\n",
                g_tq_n[0] / f, g_tq_n[1] / f, g_tq_n[2] / f, g_tq_n[3] / f, g_tq_n[4] / f);
        memset(g_tq_n, 0, sizeof g_tq_n);
        fprintf(stderr, "  [VGLPERF] textures per frame: %.0f binds; surfaces %.1f ms, GL textures %.1f ms of which decode cache %.1f ms "
                "(checksums %.1f ms, %.0f of them)\n", g_tx_binds / f, g_tx_us[0] / 1000.0 / f, g_tx_us[4] / 1000.0 / f,
                g_tx_us[1] / 1000.0 / f, g_tx_us[2] / 1000.0 / f, g_tx_sums / f);
        fprintf(stderr, "  [VGLPERF] stages per frame: %.0f kept, %.0f bound again\n", g_vstg_kept / f, g_vstg_made / f);
        fprintf(stderr, "  [VGLPERF] combiners per frame: %.0f decoded, %.0f kept (only textures changed)\n", g_comb_made / f, g_comb_kept / f);
        g_comb_made = g_comb_kept = 0;
        xbox_VitaUniPoolReport(f);
        fprintf(stderr, "  [VGLPERF] not drawn per frame: %.0f effect quads, %.0f round shadows, %.0f lights' draws\n",
                g_lite_skipped[0] / f, g_lite_skipped[1] / f, g_lite_skipped[2] / f);
        g_lite_skipped[0] = g_lite_skipped[1] = g_lite_skipped[2] = 0;
        fprintf(stderr, "  [VGLPERF] single quads per frame: %.0f, drawn in %.0f runs\n", g_vsm_quads / f, g_vsm_runs / f);
        g_vsm_quads = g_vsm_runs = 0;
        fprintf(stderr, "  [VGLPERF] vertex buffers per frame: %.0f draws, %.0f KB copied; %u draws found the buffer full\n", g_vbs_draws / f, g_vbs_bytes / 1024.0 / f, g_vbs_full);
        g_vbs_draws = g_vbs_bytes = g_vbs_full = 0;
        {   extern volatile uint32_t g_pb_runs;
            fprintf(stderr, "  [VGLPERF] targets per frame: %.0f kept, %.0f looked up again (%.0f of them for a new command run); %.1f command runs; vertex attribute calls %.0f, %.0f of them skipped\n",
                    g_bt_hit / f, g_bt_miss / f, g_bt_miss_run / f, (double)(g_pb_runs - g_bt_runs0) / f, g_va_calls / f, g_va_skipped / f);
            g_bt_hit = g_bt_miss = g_bt_miss_run = 0; g_bt_runs0 = g_pb_runs; g_va_calls = g_va_skipped = 0;
        }
        g_vstg_kept = g_vstg_made = 0;
        fprintf(stderr, "  [VGLPERF] direct draws per frame: %.0f from inline vertices, %.0f packed-normal inputs; not direct: %.0f short, %.0f attribute type, %.0f over 64K vertices, %.0f out of range\n",
                g_vpd_inl / f, g_vpd_cmp / f, g_vpd_why[0] / f, g_vpd_why[1] / f, g_vpd_why[2] / f, g_vpd_why[3] / f);
        memset(g_vpd_why, 0, sizeof g_vpd_why); g_vpd_cmp = g_vpd_inl = 0;
        memset(g_tx_us, 0, sizeof g_tx_us); g_tx_sums = g_tx_binds = 0; g_vp_direct = 0;
        fprintf(stderr, "  [VGLPERF] per frame: %.0f draws (%.0f with the vertex program on the GPU), %.0f vertices transformed on the CPU, "
                "%.1f texture uploads (%.0f KB); vertex programs compiled %u, failed %u\n",
                g_vp_draws / f, g_vp_hw / f, g_vp_verts / f, g_vp_up / f, g_vp_upkb / f, g_hwvp_compiled, g_hwvp_failed);
    }
    memset(g_vp_us, 0, sizeof g_vp_us);
    g_vp_draws = g_vp_verts = g_vp_up = g_vp_upkb = g_vp_hw = 0;
    n = 0; t0 = now;
}


#if defined(__vita__)
/* (diagnostics, the capture frame) every draw listed in cap/draws.txt, and
 * the screen-sized target written out every 150 of its draws */
static FILE *g_cap_list;
static unsigned g_cap_ndraw;
static int g_cap_screen;
static unsigned g_cap_last;    /* (draws of the frame before) */
static unsigned g_cap_fdraws;   /* (draws this frame: the capture waits for a busy one, the level) */
static void cap_note_text(const char *fmt, ...)
{
    va_list ap;
    if (!g_cap_on) return;
    if (!g_cap_list) { char p2[96]; sceIoMkdir(g_cap_dir, 0777); snprintf(p2, sizeof p2, "%s/draws.txt", g_cap_dir); g_cap_list = fopen(p2, "w"); }
    if (!g_cap_list) return;
    fprintf(g_cap_list, "   - after draw %u: ", g_cap_ndraw);
    va_start(ap, fmt); vfprintf(g_cap_list, fmt, ap); va_end(ap);
    fprintf(g_cap_list, "\n");
}
static void cap_note_draw(const char *path, uint32_t hash, uint32_t nidx)
{
    g_cap_fdraws++;
    fr_add(path[0] == 'h' ? 1 : path[0] == 'd' ? 2 : 6, hash, nidx, g_cur_rt ? g_cur_rt->base : 0, g_cur_ds ? g_cur_ds->base : 0,
           ((uint32_t)s_3d.depth_test & 1) | (((uint32_t)s_3d.depth_mask & 1) << 1) | (((uint32_t)s_3d.depth_func & 0xF) << 2)
           | (((uint32_t)s_gpu.blend_enable & 1) << 6) | (((uint32_t)s_3d.stencil_test & 1) << 7)
           | (((s_3d.color_mask >> 16) & 1) << 8) | (((s_3d.color_mask >> 8) & 1) << 9) | ((s_3d.color_mask & 1) << 10)
           | (((s_3d.color_mask >> 24) & 1) << 11) | (((uint32_t)s_3d.st_func & 0xF) << 12) | (((uint32_t)s_gpu.prim & 0xF) << 16));
    if (!g_cap_on) return;
    if (!g_cap_list) { char p2[96]; sceIoMkdir(g_cap_dir, 0777); snprintf(p2, sizeof p2, "%s/draws.txt", g_cap_dir); g_cap_list = fopen(p2, "w"); }
    if (g_cap_list)
        fprintf(g_cap_list, "%4u %-6s prog %08X idx %5u prim %u target %08X %ux%u z %d test %d func %u mask %d blend %d cull %d fog %d stages %d%d%d%d\n",
                g_cap_ndraw, path, hash, nidx, s_gpu.prim, g_cur_rt ? g_cur_rt->base : 0, g_cur_rt ? g_cur_rt->w : 0, g_cur_rt ? g_cur_rt->h : 0,
                g_cur_ds != NULL, (int)s_3d.depth_test, (unsigned)s_3d.depth_func, (int)s_3d.depth_mask, (int)s_gpu.blend_enable,
                (int)s_3d.cull_enable, (int)s_3d.fog_enable, s_stage[0].enabled, s_stage[1].enabled, s_stage[2].enabled, s_stage[3].enabled);
    if (g_cap_list) {
        int k;
        fprintf(g_cap_list, "      blend %04X/%04X eq %04X color %08X mask %08X; stencil %u func %04X ref %u mask %02X; alpha test %d func %04X ref %u; scissor %d,%d %dx%d;",
                s_gpu.blend_sfactor & 0xFFFF, s_gpu.blend_dfactor & 0xFFFF,
                s_3d.blend_eq & 0xFFFF, s_3d.blend_color, s_3d.color_mask, s_3d.stencil_test, s_3d.st_func, s_3d.st_ref, s_3d.st_mask,
                s_3d.alpha_test, s_3d.alpha_func, s_3d.alpha_ref, g_cur_sc[0], g_cur_sc[1], g_cur_sc[2], g_cur_sc[3]);
        for (k = 0; k < 4; k++) if (s_stage[k].enabled)
            fprintf(g_cap_list, " stage %d tex %08X %ux%u fmt %08X mode %u%s;", k, s_stage[k].t.offset, s_stage[k].t.width, s_stage[k].t.height,
                    s_stage[k].t.color, (unsigned)s_stage[k].mode, gsurf_find(s_stage[k].t.offset, 0) ? " (a render target)" : "");
        fprintf(g_cap_list, "\n");
#if defined(__vita__)
        fprintf(g_cap_list, "      clip z %.6g..%.6g, vp %.6g %.6g %.9g %.3g; patcher: vp made %u failed %u, fp made %u failed %u, null %u/%u, last err %08X\n", s_clip_min, s_clip_max, g_vub[VB_VSC][0][0], g_vub[VB_VSC][0][1], g_vub[VB_VSC][0][2], g_vub[VB_VSC][0][3],
                g_gxm_vc, g_gxm_vf, g_gxm_fc, g_gxm_ff, g_gxm_nullv, g_gxm_nullf, (unsigned)g_gxm_last_err);
        fprintf(g_cap_list, "      vitaGL: nopoly %u cull %u/%04X disp %u depth %u/%04X/%u scissor %u region %d %d %d %d %d %d %d %d stencil %u blend %u/%08X phase %u dirtyfb %u err %04X wfb %p inuse %p rt %p prog %p vp %d %d %d %d\n",
                no_polygons_mode, cull_face_state, gl_cull_mode, is_rendering_display, depth_test_state, depth_func, depth_mask_state,
                scissor_test_state, region[0], region[1], region[2], region[3], region[4], region[5], region[6], region[7],
                stencil_test_state, blend_state, blend_info, phase, dirty_framebuffer, vgl_error,
                active_write_fb, in_use_framebuffer, gxm_render_target, cur_program, gl_viewport[0], gl_viewport[1], gl_viewport[2], gl_viewport[3]);
#endif
    }
    g_cap_ndraw++;
    /* (v59: the scene itself is right by now -- the faults are in the frame's
     * last passes, the glow and the HUD: every 3rd draw of the last 200 of
     * the frame before) */
    if (g_cur_rt && g_cur_rt->w >= 640 && (g_cap_ndraw % 150 == 0
                                         || (g_cap_last > 200 && g_cap_ndraw + 200 >= g_cap_last && g_cap_ndraw % 4 == 0))) {
        char w[24];
        snprintf(w, sizeof w, "mid%04u", g_cap_ndraw);
        cap_save(g_cur_rt, w);
    }
}
#endif
#if defined(__vita__)
/* (PS Vita) a draw whose vertex program runs on the GPU, its inputs read by
 * the GPU straight from the guest's vertex buffers: no unpacking to floats on
 * the CPU (hwvp_pack_range) and no remapping of every index to a slot
 * (gpu_vertex_slot) -- the two were ~90 ms of the render thread's frame.
 * The indices become a triangle list relative to the lowest one. A draw it
 * cannot take (inline arrays, the packed CMP format, a buffer across two
 * slices of the guest space, the capture on) returns 0 for the usual path.
 * RECOMP_VGL_DIRECT=0 turns it off. */
static uint16_t *g_didx;
static uint32_t g_didx_cap;

/* (v102) the direct draws' vertices in vertex buffer objects. From client
 * arrays vitaGL copies every attribute's stride x vertices by itself -- an
 * interleaved buffer of five attributes five times (~3 MB a frame, the
 * "vitaGL glDrawElements" share of the render thread). Here the vertex range
 * is copied once into a buffer vitaGL draws from in place: three buffers
 * taken in turn, a frame each (the GPU is at most two frames behind).
 * RECOMP_VGL_VBO_MB (each; default 3, 0 = off). */
static struct { GLuint buf[3]; uint8_t *ptr[3]; uint32_t size, at; int cur, state; } g_vbs;
static void vbs_frame(void) { g_vbs.cur = (g_vbs.cur + 1) % 3; g_vbs.at = 0; }
static int vbs_ready(void)
{
    int i;
    if (g_vbs.state) return g_vbs.state > 0;
    {
        const char *e = getenv("RECOMP_VGL_VBO_MB");
        int mb = e ? atoi(e) : 0;   /* (v103: off -- v102 on the Vita drew garbage geometry at 3 fps) */
        if (mb <= 0) { g_vbs.state = -1; return 0; }
        g_vbs.size = (uint32_t)mb << 20;
    }
    for (i = 0; i < 3; i++) {
        (glGenBuffers)(1, &g_vbs.buf[i]);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbs.buf[i]);
        (glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)g_vbs.size, NULL, GL_DYNAMIC_DRAW);
        g_vbs.ptr[i] = (uint8_t *)(glMapBuffer)(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        (glUnmapBuffer)(GL_ARRAY_BUFFER);
        if (!g_vbs.ptr[i]) { g_vbs.state = -1; fprintf(stderr, "  [VGL] vertex buffers: no memory (%u MB each), client arrays as before\n", g_vbs.size >> 20); glBindBuffer(GL_ARRAY_BUFFER, 0); return 0; }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    g_vbs.state = 1;
    fprintf(stderr, "  [VGL] vertex buffers: 3 x %u MB\n", g_vbs.size >> 20);
    return 1;
}
/* the offsets in the current buffer of each input's first vertex (lo), or
 * 0xFFFFFFFF where it stays a client array; 0 = nothing placed */
static int vbs_place(const HwVp *hv, uint32_t lo, uint32_t hi, uint32_t voff[16])
{
    struct { uint32_t stride, mn, mx; uint32_t at; } g[4];
    int ng = 0, gi[16], i;
    uint32_t k, need = 0, at0;
    for (k = 0; k < 16; k++) { voff[k] = 0xFFFFFFFFu; gi[k] = -1; }
    if (!vbs_ready()) return 0;
    for (k = 0; k < hv->nin; k++) {
        const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
        uint32_t esz;
        if (!at->size || !at->stride || !at->offset || at->type == 6) continue;
        esz = at->type == 2 ? 4u * (at->size > 4 ? 4 : at->size) : at->type == 0 || at->type == 4 ? 4u : 2u * (at->size > 4 ? 4 : at->size);
        for (i = 0; i < ng; i++)
            if (g[i].stride == at->stride && at->offset + at->stride > g[i].mn && at->offset < g[i].mn + at->stride) break;
        if (i == ng) {
            if (ng == 4) continue;
            g[ng].stride = at->stride; g[ng].mn = at->offset; g[ng].mx = at->offset + esz; ng++;
        } else {
            if (at->offset < g[i].mn) g[i].mn = at->offset;
            if (at->offset + esz > g[i].mx) g[i].mx = at->offset + esz;
        }
        gi[k] = i;
    }
    if (!ng) return 0;
    at0 = (g_vbs.at + 15u) & ~15u;
    for (i = 0; i < ng; i++) {
        uint32_t bytes = (hi - lo) * g[i].stride + (g[i].mx - g[i].mn);
        if (g[i].mx - g[i].mn > g[i].stride * 2u) return 0;   /* (not one interleaved vertex after all) */
        g[i].at = at0 + need;
        need += (bytes + 15u) & ~15u;
    }
    if (at0 + need > g_vbs.size) { g_vbs_full++; return 0; }
    for (i = 0; i < ng; i++)
        memcpy(g_vbs.ptr[g_vbs.cur] + g[i].at, XR_PTR(g[i].mn + lo * g[i].stride), (hi - lo) * g[i].stride + (g[i].mx - g[i].mn));
    g_vbs.at = at0 + need;
    g_vbs_bytes += need; g_vbs_draws++;
    for (k = 0; k < hv->nin; k++)
        if (gi[k] >= 0) voff[k] = g[gi[k]].at + (s_gpu.attr[hv->inreg[k]].offset - g[gi[k]].mn);
    return 1;
}
/* (v117) DRAWS JOINED. A fifth of a battle's draws change nothing since the
 * draw before them (no state, no constant, no clear or copy between:
 * s_last_chg == 0, what the D3D11 renderer joins on) and read the same
 * vertex buffers: pieces of one model, the quads of one effect. Each cost a
 * whole trip through vitaGL (~70 us). Now a direct draw is kept back -- its
 * state bound, its triangle list in g_didx -- and such a draw after it only
 * adds its triangles; the list is drawn before anything else touches GL.
 * RECOMP_VGL_JOIN=0: off. */
/* (v170) the attributes of the draw set up, for the one copy of their span (g_dz_obj) */
static struct { int on; uint32_t start, lo, n; uint32_t off[16], st[16], esz[16]; uint8_t k[16]; GLint sz[16]; GLenum ty[16]; GLboolean nm[16]; } g_dz;
extern void *__real_sceClibMemcpy(void *d, const void *s, unsigned n);   /* (v183: sceClibMemcpy is wrapped for [RTPROF]) */
static int g_dz_mode = -1;
/* (v216) THE STATIC VERTEX CACHE. Each direct draw (a joined list) had the
 * span of guest memory its attributes read copied into vitaGL's frame pool
 * (dz_place): ~1.6 MB a frame in Rohan's second level, the render thread's
 * biggest zone (12.7%, "direct: the vertices copied") -- and most of it is
 * the scenery, the same bytes every frame. Now a span (keyed by its guest
 * address and length) whose bytes stay the same over two frames is copied
 * once into memory of its own (an arena from vitaGL's RAM pool, TT_VCACHE_MB,
 * default 6) and drawn from there for as long as its bytes do not change.
 * Every use reads the span once (a 64-bit hash, NEON) and compares: a span
 * that changed is copied into the frame pool as before (so a changed buffer
 * never draws stale vertices); its old copy is freed 4 frames later (the GPU
 * may still read it). A span that changes 3 times is left to the old way
 * (no reading) for 600 frames. Spans under 256 bytes: the old way. The same
 * for the triangle lists: one whose indices were the same in two frames is
 * kept and drawn from an element buffer of ours (vitaGL copies nothing).
 * When the arena is full the copies not used for 6 frames are dropped.
 * TT_VCACHE_MB=0: off. */
#define VC_N 4096u
#define IX_N 2048u
enum { VC_EMPTY, VC_SEEN, VC_HASHED, VC_CACHED, VC_DYN };
typedef struct { uint32_t start, len, f_last, f_use, f_dyn, sz, uses; uint32_t sh[16][2]; uint8_t *mem, *dst; uint8_t st, chg; } VcEnt;
typedef struct { uint32_t h0, h1, n, f_last, f_use, sz; uint8_t *mem, *dst; uint8_t st; } IxEnt;
static VcEnt *g_vc;
static IxEnt *g_ix;
static SceClibMspace g_vc_ms;
static int g_vc_on = -1;
static uint32_t g_vc_now = 0xFFFFFFFFu, g_vc_sweep_f = 0xFFFFFFFFu;
#define VC_RET_N 8192u
static struct { uint8_t *m; uint32_t f, sz; } g_vc_ret[VC_RET_N];
static uint32_t g_vc_rh, g_vc_rt;
static uint32_t g_ix_obj[5] = { 0, 0, 0, 0xFFFFFFFFu, 0 };
static int vc_init(void)
{
    const char *e;
    int mb;
    uint8_t *a = NULL;
    if (g_vc_on >= 0) return g_vc_on;
    g_vc_on = 0;
    e = getenv("TT_VCACHE_MB");
    mb = e ? atoi(e) : 8;
    if (mb <= 0) { fprintf(stderr, "  [VCACHE] off (TT_VCACHE_MB=0)\n"); return 0; }
    g_vc = (VcEnt *)calloc(VC_N, sizeof *g_vc);
    g_ix = (IxEnt *)calloc(IX_N, sizeof *g_ix);
    if (!g_vc || !g_ix) { fprintf(stderr, "  [VCACHE] off: no memory for the tables\n"); return 0; }
    /* (v217) vitaGL's phycont pool first (v216: only 3 of 6 MB found in its RAM pool, the cache too small) */
    for (; mb >= 1 && !a; mb /= 2) {
        a = (uint8_t *)vglAlloc((uint32_t)mb << 20, VGL_MEM_PHYCONT);
        if (!a) a = (uint8_t *)vglAlloc((uint32_t)mb << 20, VGL_MEM_RAM);
        if (a) break;
    }
    if (!a) { fprintf(stderr, "  [VCACHE] off: no room in vitaGL's RAM pool\n"); return 0; }
    g_vc_ms = sceClibMspaceCreate(a, (SceSize)mb << 20);
    if (!g_vc_ms) { vglFree(a); fprintf(stderr, "  [VCACHE] off: no heap\n"); return 0; }
    g_vc_size = (uint32_t)mb << 20;
    g_vc_on = 1;
    fprintf(stderr, "  [VCACHE] static vertex cache: %d MB\n", mb);
    return 1;
}
static void vc_hash(const uint8_t *p, uint32_t len, uint32_t *o0, uint32_t *o1)
{
    static const uint32_t lane[4] = { 1u, 0x01000193u, 0x5BD1E995u, 0x68E31DA4u };
    const uint32x4_t P1 = vdupq_n_u32(2654435761u), P2 = vdupq_n_u32(2246822519u), L = vld1q_u32(lane);
    uint32x4_t a0 = vaddq_u32(vdupq_n_u32(0x9E3779B1u ^ len), L), a1 = vaddq_u32(vdupq_n_u32(0x85EBCA77u), L);
    uint32x4_t a2 = vaddq_u32(vdupq_n_u32(0xC2B2AE3Du), L), a3 = vaddq_u32(vdupq_n_u32(0x27D4EB2Fu ^ len), L);
    uint32_t i = 0, h = 0x165667B1u ^ len, g = 0x3C6EF372u + len, w[16], k;
#define VC_R(a, v) (a = vmlaq_u32(a, v, P2), a = vsriq_n_u32(vshlq_n_u32(a, 13), a, 19), a = vmulq_u32(a, P1))
    for (; i + 64 <= len; i += 64) {
        __builtin_prefetch(p + i + 384);
        VC_R(a0, vreinterpretq_u32_u8(vld1q_u8(p + i)));
        VC_R(a1, vreinterpretq_u32_u8(vld1q_u8(p + i + 16)));
        VC_R(a2, vreinterpretq_u32_u8(vld1q_u8(p + i + 32)));
        VC_R(a3, vreinterpretq_u32_u8(vld1q_u8(p + i + 48)));
    }
#undef VC_R
    for (; i + 4 <= len; i += 4) {
        uint32_t v; memcpy(&v, p + i, 4);
        h += v * 3266489917u; h = (h << 17 | h >> 15) * 668265263u;
    }
    for (; i < len; i++) { h += p[i] * 374761393u; h = (h << 11 | h >> 21) * 2654435761u; }
    vst1q_u32(w, a0); vst1q_u32(w + 4, a1); vst1q_u32(w + 8, a2); vst1q_u32(w + 12, a3);
    for (k = 0; k < 16; k++) {
        h ^= w[k]; h = (h << 13 | h >> 19) * 2654435761u;
        g += w[k] ^ (g >> 15); g = (g << 17 | g >> 15) * 2246822519u;
    }
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    g ^= g >> 16; g *= 0x85EBCA6Bu; g ^= g >> 13; g *= 0xC2B2AE35u; g ^= g >> 16;
    *o0 = h; *o1 = g;
}
static int vc_retire(uint8_t *m, uint32_t sz, uint32_t now)
{
    if (g_vc_rh - g_vc_rt >= VC_RET_N) return 0;
    g_vc_ret[g_vc_rh % VC_RET_N].m = m; g_vc_ret[g_vc_rh % VC_RET_N].f = now; g_vc_ret[g_vc_rh % VC_RET_N].sz = sz;
    g_vc_rh++;
    return 1;
}
/* the copies let go 4+ frames ago: the GPU is done with them (vitaGL reuses its frame pool after 3) */
static void vc_frame(uint32_t now)
{
    g_vc_now = now;
    while (g_vc_rt != g_vc_rh && now - g_vc_ret[g_vc_rt % VC_RET_N].f >= 4u) {
        sceClibMspaceFree(g_vc_ms, g_vc_ret[g_vc_rt % VC_RET_N].m);
        g_vc_used -= g_vc_ret[g_vc_rt % VC_RET_N].sz;
        g_vc_rt++;
    }
}
/* the arena full: the copies not used for 6 frames freed (at most once a frame) */
static void vc_sweep(uint32_t now)
{
    uint32_t i;
    g_vc_sweep_f = now;
    for (i = 0; i < VC_N; i++) {
        VcEnt *e = &g_vc[i];
        if (e->mem && now - e->f_use >= 6u) {
            sceClibMspaceFree(g_vc_ms, e->mem); g_vc_used -= e->sz; g_vc_entries--;
            e->mem = e->dst = NULL; e->sz = 0;
            if (e->st == VC_CACHED) { e->st = VC_HASHED; e->f_last = e->f_use; }
            g_vc_evict++;
        }
    }
    for (i = 0; i < IX_N; i++) {
        IxEnt *x = &g_ix[i];
        if (x->mem && now - x->f_use >= 6u) {
            sceClibMspaceFree(g_vc_ms, x->mem); g_vc_used -= x->sz;
            x->mem = x->dst = NULL; x->sz = 0; x->f_last = x->f_use;
            g_vc_evict++;
        }
    }
}
static uint8_t *vc_alloc(uint32_t sz, uint32_t now)
{
    uint8_t *m = (uint8_t *)sceClibMspaceMalloc(g_vc_ms, sz);
    if (!m && g_vc_sweep_f != now) { vc_sweep(now); m = (uint8_t *)sceClibMspaceMalloc(g_vc_ms, sz); }
    if (m) g_vc_used += sz; else g_vc_nomem++;
    return m;
}
/* an entry's copy let go (deferred); 0: the list of those is full, kept */
static int vc_drop(VcEnt *e, uint32_t now)
{
    if (!e->mem) return 1;
    if (!vc_retire(e->mem, e->sz, now)) return 0;
    e->mem = e->dst = NULL; e->sz = 0; g_vc_entries--;
    return 1;
}
/* (v217) the span's 16 stripes hashed (all, or stripe k only) */
#define VC_ST 16u
static void vc_hash_stripes(const uint8_t *src, uint32_t len, uint32_t (*sh)[2], int k)
{
    uint32_t j;
    for (j = 0; j < VC_ST; j++) {
        uint32_t a = len * j / VC_ST, b = len * (j + 1) / VC_ST;
        if (k >= 0 && (uint32_t)k != j) continue;
        vc_hash(src + a, b - a, &sh[j][0], &sh[j][1]);
        g_vc_hash_b += b - a;
    }
}
static int vc_same(uint32_t (*x)[2], uint32_t (*y)[2])
{
    return memcmp(x, y, sizeof(uint32_t) * 2 * VC_ST) == 0;
}
/* the cached copy of the guest span [start, start + len), or NULL: copy it as before */
static uint8_t *vc_find(uint32_t start, uint32_t len)
{
    uint32_t now, slot, i, sz, k;
    uint32_t sh[VC_ST][2];
    int full = 0;
    VcEnt *e = NULL, *v = NULL;
    const uint8_t *src;
    uint8_t *m, *dst;
    if (len < 256u || !vc_init()) return NULL;
    now = vglGetFrameNumber();
    if (now != g_vc_now) vc_frame(now);
    slot = (start * 2654435761u) ^ (len * 40503u);
    for (i = 0; i < 8; i++) {
        VcEnt *c = &g_vc[(slot + i) & (VC_N - 1)];
        if (c->st != VC_EMPTY && c->start == start && c->len == len) { e = c; break; }
        if (!v || (v->st != VC_EMPTY && (c->st == VC_EMPTY || c->f_use < v->f_use))) v = c;
    }
    if (!e) {
        if (v->st != VC_EMPTY && (v->f_use == now || !vc_drop(v, now))) return NULL;
        memset(v, 0, sizeof *v);
        v->start = start; v->len = len; v->st = VC_SEEN; v->f_last = v->f_use = now;
        g_vc_new++;
        return NULL;
    }
    e->f_use = now;
    src = XR_PTR(start);
    if (e->st == VC_SEEN) {
        if (e->f_last != now) { VWHERE("direct: vertex cache check"); vc_hash_stripes(src, len, e->sh, -1); e->st = VC_HASHED; e->f_last = now; }
        return NULL;
    }
    if (e->st == VC_DYN) {
        if (now - e->f_dyn >= 600u && vc_drop(e, now)) { e->st = VC_SEEN; e->chg = 0; e->f_last = now; }
        return NULL;
    }
    VWHERE("direct: vertex cache check");
    if (e->st == VC_CACHED) {
        /* (v217) one stripe a use, in turn: the whole span read over 16 uses */
        k = e->uses++ & (VC_ST - 1);
        vc_hash_stripes(src, len, sh, (int)k);
        if (sh[k][0] == e->sh[k][0] && sh[k][1] == e->sh[k][1]) { g_vc_hit++; g_vc_hit_b += len; return e->dst; }
    } else {   /* VC_HASHED: all of it, the same as the frame before? */
        if (e->f_last == now) return NULL;   /* (the same frame: not yet two frames the same) */
        vc_hash_stripes(src, len, sh, -1); full = 1;
        if (vc_same(sh, e->sh)) {
            sz = len + 32u;
            m = vc_alloc(sz, now);
            if (!m) { e->f_last = now; return NULL; }
            dst = (uint8_t *)(((uintptr_t)m + 15u) & ~(uintptr_t)15u) + (start & 15u);
            __real_sceClibMemcpy(dst, src, len);
            vc_hash_stripes(src, len, sh, -1);   /* (the guest's bytes unchanged while they were copied?) */
            if (!vc_same(sh, e->sh)) {   /* no: the GPU never saw this copy, freed now */
                sceClibMspaceFree(g_vc_ms, m); g_vc_used -= sz;
                memcpy(e->sh, sh, sizeof sh); e->f_last = now; g_vc_chg++;
                return NULL;
            }
            e->mem = m; e->dst = dst; e->sz = sz; e->st = VC_CACHED; e->uses = 0; g_vc_entries++;
            g_vc_hit++; g_vc_hit_b += len;
            return dst;
        }
    }
    /* changed: copied as before, hashed again in a later frame */
    g_vc_chg++;
    e->f_last = now;
    if (!vc_drop(e, now) || ++e->chg >= 3) { e->st = VC_DYN; e->f_dyn = now; g_vc_dyn++; }
    else {
        e->st = VC_HASHED;
        if (full) memcpy(e->sh, sh, sizeof sh); else vc_hash_stripes(src, len, e->sh, -1);
    }
    return NULL;
}
/* the kept copy of a triangle list (16-bit, nt indices), or NULL */
static const uint16_t *ix_find(const uint16_t *d, uint32_t nt)
{
    uint32_t now, slot, i, h0, h1, sz;
    IxEnt *x = NULL, *v = NULL;
    uint8_t *m;
    if (nt < 96u || !vc_init()) return NULL;
    now = vglGetFrameNumber();
    if (now != g_vc_now) vc_frame(now);
    vc_hash((const uint8_t *)d, nt * 2u, &h0, &h1);
    slot = h0 ^ (nt * 40503u);
    for (i = 0; i < 8; i++) {
        IxEnt *c = &g_ix[(slot + i) & (IX_N - 1)];
        if (c->st != VC_EMPTY && c->h0 == h0 && c->h1 == h1 && c->n == nt) { x = c; break; }
        if (!v || (v->st != VC_EMPTY && (c->st == VC_EMPTY || c->f_use < v->f_use))) v = c;
    }
    if (!x) {
        if (v->st != VC_EMPTY) {
            if (v->f_use == now) return NULL;
            if (v->mem) { if (!vc_retire(v->mem, v->sz, now)) return NULL; }
        }
        memset(v, 0, sizeof *v);
        v->h0 = h0; v->h1 = h1; v->n = nt; v->st = VC_SEEN; v->f_last = v->f_use = now;
        g_ix_new++;
        return NULL;
    }
    x->f_use = now;
    if (x->mem) { g_ix_hit++; g_ix_hit_b += nt * 2u; return (const uint16_t *)x->dst; }
    if (x->f_last == now) { g_ix_miss++; return NULL; }
    sz = nt * 2u + 16u;
    m = vc_alloc(sz, now);
    if (!m) { x->f_last = now; g_ix_miss++; return NULL; }
    x->mem = m; x->sz = sz;
    x->dst = (uint8_t *)(((uintptr_t)m + 15u) & ~(uintptr_t)15u);
    __real_sceClibMemcpy(x->dst, d, nt * 2u);
    g_ix_hit++; g_ix_hit_b += nt * 2u;
    return (const uint16_t *)x->dst;
}
/* the direct path's draw: the triangle list in g_didx, indices 0..top */
static void dz_draw(uint32_t nt, uint32_t top)
{
    const uint16_t *ci = ix_find(g_didx, nt);
    if (ci) {
        { extern void glt_store32(volatile uint32_t *, uint32_t); glt_store32(&g_ix_obj[0], (uint32_t)(uintptr_t)ci); }   /* (v231: read by vitaGL at the draw, on the GL worker) */
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)(uintptr_t)g_ix_obj);
        vgl_draw_range(GL_TRIANGLES, top, (GLsizei)nt, NULL);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else vgl_draw_range(GL_TRIANGLES, top, (GLsizei)nt, g_didx);
}
static int dz_place(uint32_t hi)
{
    uint32_t j, end = 0, len, old = 0;
    uint8_t *p, *dst;
    if (!g_dz.on) return 1;
    g_dz.on = 0;
    for (j = 0; j < g_dz.n; j++) {
        uint32_t e = g_dz.off[j] + hi * g_dz.st[j] + g_dz.esz[j];
        if (e > end) end = e;
        old += (hi - g_dz.lo + 1) * g_dz.st[j];
    }
    len = end - g_dz.start;
    if (len <= (1u << 20) && (g_dz.start >> 24) == ((end - 1) >> 24)) {   /* (v216) the static vertex cache */
        uint8_t *c = vc_find(g_dz.start, len);
        if (c) { extern void glt_store32(volatile uint32_t *, uint32_t); glt_store32(&g_dz_obj[0], (uint32_t)(uintptr_t)c); g_dz_draws++; g_dz_old += old; return 1; }
        g_vc_miss++;
        VWHERE("direct: the vertices copied");
    }
    {   /* (v231) with the GL worker (vgl_glt.c): the span copied into the queue here, into vitaGL's pool there */
        extern int glt_active(void);
        extern void glt_dz_copy(volatile uint32_t *, const void *, uint32_t, uint32_t);
        if (glt_active() && len <= (1u << 20) && (g_dz.start >> 24) == ((end - 1) >> 24)) {
            glt_dz_copy(&g_dz_obj[0], XR_PTR(g_dz.start), len, g_dz.start & 15u);
            g_dz_draws++; g_dz_bytes += len; g_dz_old += old;
            return 1;
        }
    }
    p = (len <= (1u << 20) && (g_dz.start >> 24) == ((end - 1) >> 24)) ? (uint8_t *)vgl_reserve_data_pool(len + 32) : NULL;
    if (!p) {   /* the attributes back to client arrays, as before */
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (j = 0; j < g_dz.n; j++)
            glVertexAttribPointer(g_dz.k[j], g_dz.sz[j], g_dz.ty[j], g_dz.nm[j], (GLsizei)g_dz.st[j], XR_PTR(g_dz.off[j] + g_dz.lo * g_dz.st[j]));
        g_dz_fb++;
        return 0;
    }
    dst = (uint8_t *)(((uintptr_t)p + 15u) & ~(uintptr_t)15u) + (g_dz.start & 15u);
    __real_sceClibMemcpy(dst, XR_PTR(g_dz.start), len);   /* (v171) the copy vitaGL itself uses (v170: newlib memcpy, the copy cost as much as it saved) */
    g_dz_obj[0] = (uint32_t)(uintptr_t)dst;
    g_dz_draws++; g_dz_bytes += len; g_dz_old += old;
    return 1;
}
static struct { int active, has_ds; HwVp *hv; uint32_t sig, lo, hi, nt; } g_jn;
static int g_jn_mode = -1;
static void jn_flush(void)
{
    if (!g_jn.active) return;
    g_jn.active = 0;
    if (!g_jn.nt) { g_dz.on = 0; return; }
    VWHERE("direct: the vertices copied");
    dz_place(g_jn.hi);
    VWHERE("batch: vitaGL glDrawElements");
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    { VP_T0(t); dz_draw(g_jn.nt, g_jn.hi - g_jn.lo); VP_ADD(VP_DRAW, t); g_vp_draws++; }
    VWHERE("direct: after the draw");
    if (g_jn.has_ds) vgl_depth_keep();
    g_jn_runs++;
}
static int gpu_batch_hw_direct(HwVp *hv)
{
    static int on = -1;
    uint32_t n = s_gpu.idx_count, i, lo = 0xFFFFFFFFu, hi = 0, nt = 0, k;
    uint32_t jsig, jbase;
    int has_cmp = 0, join = 0;
    const uint32_t *ix = s_gpu.idx;
    GLuint prog;
    uint16_t *d;
    int inl = s_gpu.inline_active;
    static int inl_on = -1;
    if (on < 0) { const char *e = getenv("RECOMP_VGL_DIRECT"); on = !(e && e[0] == '0'); }
    /* (v77) the inline vertices (the HUD, the menus) back on the CPU path:
     * drawn direct (v74-v76) the HUD and the pause menu were gone.
     * RECOMP_VGL_DIRECT_INLINE=1: direct */
    if (inl_on < 0) { const char *e = getenv("RECOMP_VGL_DIRECT_INLINE"); inl_on = e && e[0] == '1'; }
    if (inl && !inl_on) { g_vpd_why[1]++; return 0; }
    if (!on || n < 3) { g_vpd_why[0]++; return 0; }
    {   /* (v125) TT_LIGHTS=1: the lights' pass. Each torch or fire draws the
         * walls and ground it lights once more, added on (ONE/ONE, vertex
         * program 453E894B): 164 of a crowded frame's 810 draws. Off by
         * default: the scene keeps its own lighting, without the flicker. */
        static int lights = -1;
        if (lights < 0) { const char *e = getenv("TT_LIGHTS"); lights = e && e[0] == '1'; }
        if (!lights && !g_cap_on && hv->hash == 0x453E894Bu && s_gpu.blend_enable
            && (s_gpu.blend_sfactor & 0xFFFF) == 1 && (s_gpu.blend_dfactor & 0xFFFF) == 1) { g_lite_skipped[2]++; jn_flush(); return 1; }
    }
    VWHERE("direct: index range");   /* (v108: finer zones) */
    /* (v74) the vertices inline in the pushbuffer (INLINE_ARRAY) and the
     * packed normals (CMP, type 6) too: before, those draws -- the
     * characters -- had every input unpacked to floats on the CPU (the
     * render thread's biggest share, "vertex attributes", v73) */
    for (k = 0; k < hv->nin; k++) {
        const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
        if (!at->size || !at->stride) continue;
        if (!inl && !at->offset) continue;   /* (v95) no buffer: a constant (0, 0, 0, 1), as on the CPU path -- such draws went to the CPU path */
        if (at->type == 3 || at->type > 6) { g_vpd_why[1]++; return 0; }
        if (at->type == 6) has_cmp = 1;
    }
#if defined(__vita__) && defined(__ARM_NEON)
    {   /* (v159) the index range four at a time (NEON): every direct draw scans its indices, 3-4% of the render thread */
        i = 0;
        if (n >= 16) {
            uint32x4_t vlo = vdupq_n_u32(lo), vhi = vdupq_n_u32(hi);
            for (; i + 4 <= n; i += 4) { uint32x4_t v = vld1q_u32(&ix[i]); vlo = vminq_u32(vlo, v); vhi = vmaxq_u32(vhi, v); }
            {
                uint32x2_t a = vpmin_u32(vget_low_u32(vlo), vget_high_u32(vlo)), b = vpmax_u32(vget_low_u32(vhi), vget_high_u32(vhi));
                a = vpmin_u32(a, a); b = vpmax_u32(b, b);
                lo = vget_lane_u32(a, 0); hi = vget_lane_u32(b, 0);
            }
        }
        for (; i < n; i++) { uint32_t v = ix[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
    }
#else
    for (i = 0; i < n; i++) { uint32_t v = ix[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
#endif
    if (hi - lo > 65535u) { g_vpd_why[2]++; return 0; }
    for (k = 0; k < hv->nin; k++) {
        const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
        if (!at->size || !at->stride) continue;
        if (!inl && !at->offset) continue;
        if (inl) {
            if ((size_t)at->offset + (size_t)hi * at->stride + 4 * (at->size > 4 ? 4 : at->size) > (size_t)s_gpu.inline_count * 4) { g_vpd_why[3]++; return 0; }
        } else if (((at->offset + lo * at->stride) >> 24) != ((at->offset + hi * at->stride + 15) >> 24)) { g_vpd_why[3]++; return 0; }
    }
    if (g_jn_mode < 0) { const char *e = getenv("RECOMP_VGL_JOIN"); g_jn_mode = !(e && e[0] == '0'); }
    jsig = (uint32_t)(uintptr_t)hv * 2654435761u;
    for (k = 0; k < hv->nin; k++) {
        const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
        jsig = (jsig ^ at->offset ^ (at->stride << 8) ^ (at->type << 16) ^ (at->size << 20)) * 16777619u;
    }
    if (g_jn.active) {
        if (!s_last_chg && !inl && !has_cmp && !g_cap_on && g_jn.hv == hv && g_jn.sig == jsig
            && lo >= g_jn.lo && hi - g_jn.lo <= 65535u && g_jn.nt + n * 3 < 60000u) join = 1;
        else jn_flush();
    }
    jbase = join ? g_jn.lo : lo;
    nt = join ? g_jn.nt : 0;
    {
        uint32_t need = nt + n * 3 + 6;
        if (need > g_didx_cap) {
            uint16_t *nb = (uint16_t *)realloc(g_didx, need * 2);
            if (!nb) { jn_flush(); return 0; }
            g_didx = nb; g_didx_cap = need;
        }
    }
    d = g_didx;
    VWHERE("direct: triangle list");
#define DT(a, b, c) (d[nt] = (uint16_t)((a) - jbase), d[nt + 1] = (uint16_t)((b) - jbase), d[nt + 2] = (uint16_t)((c) - jbase), nt += 3)
    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:      for (i = 0; i + 2 < n; i += 3) DT(ix[i], ix[i+1], ix[i+2]); break;
    case NV_PRIM_TRIANGLE_STRIP: for (i = 0; i + 2 < n; i++) { if (i & 1) DT(ix[i+1], ix[i], ix[i+2]); else DT(ix[i], ix[i+1], ix[i+2]); } break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:        for (i = 1; i + 1 < n; i++) DT(ix[0], ix[i], ix[i+1]); break;
    case NV_PRIM_QUADS:          for (i = 0; i + 3 < n; i += 4) { DT(ix[i], ix[i+1], ix[i+2]); DT(ix[i], ix[i+2], ix[i+3]); } break;
    case NV_PRIM_QUAD_STRIP:     for (i = 0; i + 3 < n; i += 2) { DT(ix[i], ix[i+1], ix[i+3]); DT(ix[i], ix[i+3], ix[i+2]); } break;
    default: jn_flush(); return 0;
    }
#undef DT
    if (join) {   /* the run goes on: nothing of GL is touched */
        s_gpu.tris_drawn += (nt - g_jn.nt) / 3;
        g_jn.nt = nt;
        if (hi > g_jn.hi) g_jn.hi = hi;
        g_jn_joined++;
        g_vp_hw++; g_vp_direct++; g_cap_hw++;
        s_gpu.drawn_offset = s_gpu.color_offset;
        s_3d.verts += hi - lo + 1;
        g_hwvp_stat_hw++;
        return 1;
    }
    if (!nt) return 1;
    {   /* (v116) how many draws could be joined to the one before: nothing
         * changed since it (no state, no constants) -- and of those, the same
         * vertex buffers ([VGLPERF] joinable) */
        static uint32_t prev_sig;
        uint32_t sig = (uint32_t)(uintptr_t)hv * 2654435761u ^ (uint32_t)inl;
        for (k = 0; k < hv->nin; k++) {
            const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
            sig = (sig ^ at->offset ^ (at->stride << 8) ^ (at->type << 16) ^ (at->size << 20)) * 16777619u;
        }
        if (!s_last_chg) { g_mg_n[0]++; if (sig == prev_sig) g_mg_n[1]++; }
        else if (s_last_chg == CHG_CONST) g_mg_n[2]++;
        if (nt <= 6) g_mg_n[3]++;
        prev_sig = sig;
    }
    { VWHERE("batch: textures/stages"); VP_T0(t); gpu_stages_vgl(); VP_ADD(VP_STAGES, t); }
    { VWHERE("batch: program"); VP_T0(t); prog = gpu_program_for(hv->vs); VP_ADD(VP_PROG, t); VWHERE("batch: vertex attributes"); }
    if (!prog) return 0;
    glBindVertexArray(g_vao_x);
    g_sh.vao = 2;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    {
        float sw[16];
        int changed = 0;
        memset(sw, 0, sizeof sw);
        uint32_t kmax = vgl_va_top();   /* (v96) not all 16 every draw: the inputs, and what is still enabled above them */
        if (kmax < hv->nin) kmax = hv->nin;
        uint32_t voff[16];
        int zc = 0;
        if (inl || !vbs_place(hv, lo, hi, voff)) memset(voff, 0xFF, sizeof voff);
        if (!inl && ip_on() && hi - lo + 1 >= 24u && !g_cap_on) {   /* (v122: read in place) */
            zc = 1;
            for (k = 0; k < hv->nin && zc; k++) {
                const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
                uint32_t ga;
                if (!at->size || !at->stride || !at->offset || at->type == 6) continue;
                ga = at->offset + lo * at->stride;
                if (!ip_slice(ga, XR_PTR(ga & 0xFF000000u))) zc = 0;
            }
            if (zc) g_ip_draws++;
        }
        g_dz.on = 0;
        if (g_dz_mode < 0) { const char *e = getenv("TT_VCOPY1"); g_dz_mode = !(e && e[0] == '0'); }
        if (g_dz_mode && !inl && !zc && !has_cmp && !g_cap_on && voff[0] == 0xFFFFFFFFu) {
            /* (v170) one copy of the span the attributes read, if no larger than vitaGL's copies */
            uint32_t st0 = 0xFFFFFFFFu, en = 0, sum = 0, j = 0;
            int ok = 1;
            for (k = 0; k < hv->nin && k < kmax; k++) {
                const VertexAttr *at = &s_gpu.attr[hv->inreg[k]];
                uint32_t sz1 = at->size > 4 ? 4 : at->size, es, a0;
                if (!at->size || !at->stride || !at->offset) continue;
                if (voff[k] != 0xFFFFFFFFu) { ok = 0; break; }
                switch (at->type) { case 0: es = 4; break; case 2: es = 4 * sz1; break; case 4: es = sz1; break; default: es = 2 * sz1; break; }
                a0 = at->offset + lo * at->stride;
                if (a0 < st0) st0 = a0;
                if (at->offset + hi * at->stride + es > en) en = at->offset + hi * at->stride + es;
                sum += (hi - lo + 1) * at->stride;
                g_dz.off[j] = at->offset; g_dz.st[j] = at->stride; g_dz.esz[j] = es; g_dz.k[j] = (uint8_t)k;
                j++;
            }
            if (ok && j && en - st0 <= sum && (st0 >> 24) == ((en - 1) >> 24)) { g_dz.on = 1; g_dz.n = j; g_dz.start = st0; g_dz.lo = lo; }
        }
        for (k = 0; k < kmax; k++) {
            const VertexAttr *at;
            GLenum ty; GLboolean nm; GLint sz;
            if (k >= hv->nin) { glDisableVertexAttribArray(k); continue; }
            at = &s_gpu.attr[hv->inreg[k]];
            if (!at->size || !at->stride || (!inl && !at->offset)) {
                glDisableVertexAttribArray(k);
                glVertexAttrib4f(k, 0.0f, 0.0f, 0.0f, 1.0f);
                continue;
            }
            sz = (GLint)(at->size > 4 ? 4 : at->size);
            {
                const uint8_t *src = inl ? (const uint8_t *)s_gpu.inline_buf + at->offset + (size_t)lo * at->stride
                                         : (const uint8_t *)XR_PTR(at->offset + lo * at->stride);
                if (at->type == 6) {   /* CMP: 11:11:10 signed, unpacked to floats for these vertices */
                    uint32_t nv = hi - lo + 1, j;
                    float *o;
                    if ((size_t)nv * 3 > g_cmp_cap[k]) {
                        float *nb = (float *)realloc(g_cmp_buf[k], (size_t)nv * 3 * 2 * sizeof(float));
                        if (!nb) return 0;
                        g_cmp_buf[k] = nb; g_cmp_cap[k] = nv * 3 * 2;
                    }
                    o = g_cmp_buf[k];
                    for (j = 0; j < nv; j++) {
                        uint32_t v; int32_t x, y, z;
                        memcpy(&v, src + (size_t)j * at->stride, 4);
                        x = (int32_t)(v << 21) >> 21; y = (int32_t)(v << 10) >> 21; z = (int32_t)v >> 22;
                        o[3 * j] = (float)x * (1.0f / 1023.0f); o[3 * j + 1] = (float)y * (1.0f / 1023.0f); o[3 * j + 2] = (float)z * (1.0f / 511.0f);
                    }
                    glEnableVertexAttribArray(k);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, 12, o);
                    g_vpd_cmp++;
                    continue;
                }
                switch (at->type) {
                case 0: ty = GL_UNSIGNED_BYTE; nm = GL_TRUE; sz = 4; sw[k] = 1.0f; break;   /* D3DCOLOR */
                case 2: ty = GL_FLOAT; nm = GL_FALSE; break;
                case 4: ty = GL_UNSIGNED_BYTE; nm = GL_TRUE; break;
                case 1: ty = GL_SHORT; nm = GL_TRUE; break;
                default: ty = GL_SHORT; nm = GL_FALSE; break;   /* 5: S32K */
                }
                glEnableVertexAttribArray(k);
                if (voff[k] != 0xFFFFFFFFu) {
                    glBindBuffer(GL_ARRAY_BUFFER, g_vbs.buf[g_vbs.cur]);
                    glVertexAttribPointer(k, sz, ty, nm, (GLsizei)at->stride, (const void *)(uintptr_t)voff[k]);
                } else if (zc) {
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    vgl_va_pointer_inplace(k, sz, ty, nm, (GLsizei)at->stride, src);
                } else if (g_dz.on) {   /* (v170) the offset in the span, copied once before the draw */
                    uint32_t j;
                    for (j = 0; j < g_dz.n && g_dz.k[j] != k; j++) ;
                    if (j < g_dz.n) { g_dz.sz[j] = sz; g_dz.ty[j] = ty; g_dz.nm[j] = nm; }
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    vgl_va_pointer_dz(k, sz, ty, nm, (GLsizei)at->stride, (const void *)(uintptr_t)(at->offset + lo * at->stride - g_dz.start));
                } else {
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    glVertexAttribPointer(k, sz, ty, nm, (GLsizei)at->stride, src);
                }
            }
        }
        VWHERE("direct: swizzle constants");
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (k = 0; k < 16; k++) if (g_vub[VB_VSC][4 + k / 4][k % 4] != sw[k]) { g_vub[VB_VSC][4 + k / 4][k % 4] = sw[k]; changed = 1; }
        if (changed) g_vub_gen[VB_VSC]++;
    }
    VWHERE("batch: vertex constants");
    gpu_vpc_sync();
    {
        VWHERE("batch: GL state"); VP_T0(t_st);
        gpu_set_blend(0);
        gpu_set_depth(g_cur_ds != NULL);
        gpu_use(prog);
        VP_ADD(VP_STATE, t_st);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (g_jn_mode && !inl && !has_cmp && !g_cap_on) {   /* (v117) kept back: the draws after it may join */
        g_jn.active = 1; g_jn.has_ds = g_cur_ds != NULL; g_jn.hv = hv; g_jn.sig = jsig; g_jn.lo = lo; g_jn.hi = hi; g_jn.nt = nt;
    } else {
    VWHERE("direct: the vertices copied");
    dz_place(hi);
    { VP_T0(t); dz_draw(nt, hi - lo); VP_ADD(VP_DRAW, t); g_vp_draws++; }
    VWHERE("direct: after the draw");
    if (g_cur_ds) vgl_depth_keep();
    }
    g_vp_hw++; g_vp_direct++; g_cap_hw++;
    if (inl) g_vpd_inl++;
    cap_note_draw("direct", hv->hash, nt);
    s_gpu.tris_drawn += nt / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_3d.verts += hi - lo + 1;
    g_hwvp_stat_hw++;
    return 1;
}
#endif
/* (v104) the single quads. Half of a battle's draws (~700 of 1500 a frame)
 * are one quad each -- sparks, embers, blood, decals -- and each cost what a
 * whole character does on the way to vitaGL. Their four vertices are run
 * through the vertex program on the CPU instead (the CPU path), and the
 * quads that follow one another with the same textures, combiners, blending
 * and target are collected and drawn in one call. The GL state is bound
 * when a run starts; the run is drawn before anything else touches GL
 * (the next different draw, a clear, a copy, the present).
 * RECOMP_VGL_QUADS=0 turns it off. */
typedef struct {
    uint32_t co, pitch, cx, cy, cw, ch, fmt, zo, be, bs, bd, mode;
    SwStage st[4];
    uint8_t s3[sizeof s_3d];
} VsmKey;
#define VSM_VERTS 2048
#define VSM_IDX 3072
/* (v218) THE ADDITIVE QUADS, ONE DRAW PER TEXTURE. A burning level (Rohan's
 * second) draws ~250 flame and ember quads a frame (vertex program 9D5CFD70:
 * a unit quad placed by its constants, one draw each), added on (blend
 * dfactor ONE, no depth write), three 32x32 textures in random turn: v104's
 * runs broke at every texture change. Added-on quads can be drawn in any
 * order (the sum is the same, also when it saturates), so now the run keeps
 * up to VSM_NB buckets, one per texture, all else of the state the same
 * (g_vsm_gk: the key without the textures); each quad's four vertices are
 * run through its program on the CPU into its bucket; all buckets are drawn
 * (each with its own textures bound) before anything else touches GL.
 * RECOMP_VGL_QUADS: 2 (default) the added-on ones; 1 v104's (every single
 * quad, no reordering); 0 off. */
#define VSM_NB 4
typedef struct { VsmKey key; GVert *v; uint16_t *ix; uint32_t nv, ni; int has_ds; GLuint tex[4], smp[4], prog; } VsmBucket;
static struct { VsmBucket b[VSM_NB]; int nb; int on; } g_vsm = { .on = -1 };
static VsmKey g_vsm_k, g_vsm_gk, g_vsm_kg;
static size_t vsm_s3_from(void) { return offsetof(__typeof__(s_3d), alpha_test); }
static size_t vsm_s3_to(void) { return offsetof(__typeof__(s_3d), comb_seen); }
static void vsm_key(VsmKey *k)
{
    memset(k, 0, sizeof *k);
    k->co = s_gpu.color_offset; k->pitch = s_gpu.pitch; k->cx = s_gpu.clip_x; k->cy = s_gpu.clip_y; k->cw = s_gpu.clip_w; k->ch = s_gpu.clip_h;
    k->fmt = s_gpu.format; k->zo = s_zeta_offset; k->be = s_gpu.blend_enable; k->bs = s_gpu.blend_sfactor; k->bd = s_gpu.blend_dfactor; k->mode = s_3d.mode;
    memcpy(k->st, s_stage, sizeof k->st);
    memcpy(k->s3, (const uint8_t *)&s_3d + vsm_s3_from(), vsm_s3_to() - vsm_s3_from());
}
/* the key without what tells the textures apart (their address, their decoded texels) */
static void vsm_group(VsmKey *g, const VsmKey *k)
{
    int i;
    *g = *k;
    for (i = 0; i < 4; i++) { g->st[i].t.offset = 0; g->st[i].texels = NULL; }
}
static void vsm_flush(void)
{
    uint32_t i;
    int b, ds = 0, n = 0, rebound = 0;
    if (!g_vsm.nb) return;
    VWHERE("batch: the quads' run");
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    g_sh.vao = 0;
    for (b = 0; b < g_vsm.nb; b++) {
        VsmBucket *B = &g_vsm.b[b];
        if (!B->nv || !B->ni) continue;
        if (!g_sh.tex_ok || memcmp(g_sh.tex, B->tex, sizeof B->tex) || memcmp(g_sh.smp, B->smp, sizeof B->smp)) {
            int k;
            for (k = 0; k < 4; k++) {
                glActiveTexture(GL_TEXTURE0 + k);
                glBindTexture(GL_TEXTURE_2D, B->tex[k]);
                glBindSampler((GLuint)k, B->smp[k]);
            }
            memcpy(g_sh.tex, B->tex, sizeof B->tex); memcpy(g_sh.smp, B->smp, sizeof B->smp); g_sh.tex_ok = 1;
            rebound = 1;
        }
        gpu_use(B->prog);
        for (i = 0; i < 8; i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GVert), (const char *)B->v + 16 * i);
        }
        (glDrawElements)(GL_TRIANGLES, (GLsizei)B->ni, GL_UNSIGNED_SHORT, B->ix);   /* (the uniforms went up when the run began) */
        ds |= B->has_ds;
        g_vp_draws++; n++;
        B->nv = B->ni = 0;
    }
    if (ds) vgl_depth_keep();
    if (rebound) g_sh.tex_ok = 0;   /* (textures bound behind gpu_stages_vgl's back: the next draw binds its own again) */
    g_vsm_runs += n;
    g_vsm.nb = 0;
}
/* the draw just transformed (g_verts, g_idx): into bucket b */
static void vsm_append(int b, uint32_t nverts)
{
    uint32_t i;
    VsmBucket *B = &g_vsm.b[b];
    if (B->nv + nverts > VSM_VERTS || B->ni + g_nidx > VSM_IDX) return;   /* (the caller has made room) */
    memcpy(B->v + B->nv, g_verts, nverts * sizeof(GVert));
    for (i = 0; i < g_nidx; i++) B->ix[B->ni + i] = (uint16_t)(g_idx[i] + B->nv);
    B->nv += nverts; B->ni += g_nidx;
    g_vsm_quads++;
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
}
/* indices and CPU-transformed vertices of the current draw, as gpu_batch_impl
 * makes them; 0 if nothing is left to draw */
static int vsm_gather(uint32_t *nverts_out)
{
    uint32_t i, n = s_gpu.idx_count, nverts = 0;
    g_gen++; if (!g_gen) g_gen = 1;
    g_nidx = 0;
    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:      for (i = 0; i + 2 < n; i += 3) gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts); break;
    case NV_PRIM_TRIANGLE_STRIP: for (i = 0; i + 2 < n; i++) { if (i & 1) gpu_tri(s_gpu.idx[i+1], s_gpu.idx[i], s_gpu.idx[i+2], &nverts); else gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts); } break;
    case NV_PRIM_TRIANGLE_FAN:   for (i = 1; i + 1 < n; i++) gpu_tri(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1], &nverts); break;
    case NV_PRIM_QUADS:          for (i = 0; i + 3 < n; i += 4) { gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts); gpu_tri(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3], &nverts); } break;
    default: return 0;
    }
    if (!g_nidx || nverts > 8) return 0;
    g_frame_3d = 1;
    gpu_transform_all(nverts);
    g_vp_verts += nverts;
    gpu_drop_bad_tris(nverts);
    if (!g_nidx) return 0;
    *nverts_out = nverts;
    return 1;
}
static void gpu_batch_impl(void);
static void gpu_batch(void)
{
    VP_T0(t);
    VWHERE("batch: indices");
    gpu_batch_impl();
    VP_ADD(VP_BATCH, t);
}
static void gpu_batch_impl(void)
#else
static void gpu_batch(void)
#endif
{
#if defined(__vita__)
    vgl_trf("gpu_batch mode %u", (unsigned)s_3d.mode);
#endif
    {   static int nul = -1;   /* testing: RECOMP_GPU_NULL=1 -- no drawing (CPU profiles of the game); =3: only the frames a picture is asked of */
        if (nul < 0) { const char *e = getenv("RECOMP_GPU_NULL"); nul = !e ? 0 : *e == '3' ? 3 : 1; }
        if (nul == 1 || (nul == 3 && !g_null3_draw)) return;
    }
    uint32_t i, n = s_gpu.idx_count, nverts = 0;
    uint32_t vb_at, ib_at;
    int need_depth = s_3d.depth_test || s_3d.stencil_test;

    {
        static int lo = -2, hi;
        if (lo == -2) { const char *e = getenv("RECOMP_GPU_SKIP"); lo = -1; if (e) sscanf(e, "%d,%d", &lo, &hi); }
        if (lo >= 0 && (int)s_3d.batches >= lo && (int)s_3d.batches <= hi) return;
    }
    if (!g_slot) {
        g_slot = (uint32_t *)calloc(SW3D_CACHE * 2, 4);
        g_slot_gen = (uint32_t *)calloc(SW3D_CACHE, 4);
        if (!g_slot || !g_slot_gen) return;
    }
#if defined(__vita__)
    {   /* (v105) the light mode: what weighs on the frame and is not needed
         * to play is not drawn -- the single quads of the effects (sparks,
         * embers, smoke, blood, decals), everything drawn into the small
         * render targets (the characters' shadows) and every draw that
         * samples a render target (the shadows laid on the ground, the heat
         * haze). RECOMP_VGL_LITE=0: all of it drawn. */
        static int lite = -1;
        if (lite < 0) { const char *e = getenv("RECOMP_VGL_LITE"); lite = e && e[0] == '1';   /* (v106: off -- no gain, the game thread is the limit; black squares) */ }
        {   /* (v117) TT_EFFECTS=0: the single quads drawn blended (sparks,
             * embers, smoke, blood, marks on the ground) not drawn -- the
             * rest of the picture as it is. (v121: all the blended ones;
             * only the additive were 4-27 a frame, nothing gained.)
             * TT_EFFECTS=2: only the additive, as in v117. */
            static int fx = -1;
            /* (v122) the default is now 3: the blended single quads not
             * drawn, but the additive ones (glows: few, 4-27 a frame) are.
             * 1 = all drawn, 0 = none, 2 = only the additive not drawn. */
            if (fx < 0) { const char *e = getenv("TT_EFFECTS"); fx = e ? atoi(e) : 3; }
            /* (v125) TT_BLOBS=1: the characters' round shadows on the ground.
             * The game paints them, two quads a character, into a 256x256
             * texture laid on the ground: 192 of a crowded frame's 810 draws
             * (the fourth level). Off by default: that texture stays clear. */
            static int blobs = -1;
            if (blobs < 0) { const char *e = getenv("TT_BLOBS"); blobs = e && e[0] == '1'; }
            if (!blobs && !g_cap_on && !s_gpu.inline_active && n <= 6 && s_gpu.blend_enable
                && s_gpu.clip_x + s_gpu.clip_w == 256 && s_gpu.clip_y + s_gpu.clip_h == 256) { g_lite_skipped[1]++; return; }
            if (fx != 1 && !g_cap_on && !s_gpu.inline_active && n <= 6 && s_gpu.blend_enable && s_3d.mode == 2 && s_vpd_len > 3) {
                int add = (s_gpu.blend_dfactor & 0xFFFF) == 1;
                static int glows = -1;   /* (v218) TT_GLOWS=0: the added-on ones (flames, embers, glows) not drawn either */
                if (glows < 0) { const char *e = getenv("TT_GLOWS"); glows = !(e && e[0] == '0'); }
                if (fx == 0 || (fx == 2 && add) || (fx == 3 && !add) || (add && !glows)) { g_lite_skipped[0]++; return; }
            }
        }
        if (lite && !g_cap_on && !s_gpu.inline_active) {
            int st;
            if (n <= 6 && s_gpu.blend_enable && s_3d.mode == 2 && s_vpd_len > 3) { g_lite_skipped[0]++; return; }
            if (s_gpu.clip_x + s_gpu.clip_w < 640) { g_lite_skipped[1]++; return; }
            for (st = 0; st < 4; st++)
                if (s_stage[st].enabled) {
                    int q;
                    if (gsurf_find(s_stage[st].t.offset, 0)) { g_lite_skipped[2]++; return; }
                    for (q = 0; q < 8; q++) if (g_lite_dst[q] && g_lite_dst[q] == s_stage[st].t.offset) { g_lite_skipped[2]++; return; }   /* (a copy that was not made) */
                }
        }
    }
    int vsm_small = 0, vsm_keep = 0;
    if (g_vsm.on < 0) {
        const char *e = getenv("RECOMP_VGL_QUADS");
        int b;
        g_vsm.on = e ? atoi(e) : 0;   /* (v105-v217: off -- v104's log: 633 quads in 323 runs, the CPU path cost more than it saved; v218: 2 by default -- grouped well, 170 quads in 3 runs, but "batch: indices" (their CPU transform) 28% of the render thread: Rohan 2 70 -> 82 ms. v219: off again) */
        for (b = 0; g_vsm.on && b < VSM_NB; b++) {
            g_vsm.b[b].v = (GVert *)malloc(VSM_VERTS * sizeof(GVert)); g_vsm.b[b].ix = (uint16_t *)malloc(VSM_IDX * sizeof(uint16_t));
            if (!g_vsm.b[b].v || !g_vsm.b[b].ix) g_vsm.on = 0;
        }
        fprintf(stderr, "  [VGL] single quads collected: %s\n", g_vsm.on == 1 ? "all (in order)" : g_vsm.on ? "the added-on ones, one draw per texture" : "off");
    }
    if (g_vsm.on && n >= 3 && n <= 6 && s_3d.mode == 2 && s_vpd_len > 3 && !s_gpu.inline_active && !g_cap_on && !s_3d.stencil_test
        && (s_gpu.prim == NV_PRIM_QUADS || s_gpu.prim == NV_PRIM_TRIANGLE_STRIP || s_gpu.prim == NV_PRIM_TRIANGLE_FAN || s_gpu.prim == NV_PRIM_TRIANGLES)
        && (g_vsm.on == 1 || (s_gpu.blend_enable && (s_gpu.blend_dfactor & 0xFFFF) == 1 && !s_3d.depth_mask && (s_3d.blend_eq & 0xFFFF) == 0x8006))) {
        int b;
        vsm_key(&g_vsm_k);
        if (g_vsm.on == 1) g_vsm_kg = g_vsm_k; else vsm_group(&g_vsm_kg, &g_vsm_k);
        vsm_small = 1;
        if (g_vsm.nb && !memcmp(&g_vsm_kg, &g_vsm_gk, sizeof g_vsm_kg)) {
            for (b = 0; b < g_vsm.nb; b++) if (!memcmp(&g_vsm_k, &g_vsm.b[b].key, sizeof g_vsm_k)) break;
            if (b < g_vsm.nb) {
                /* the run goes on: nothing of GL is touched */
                VsmBucket *B = &g_vsm.b[b];
                if (B->nv + 8 <= VSM_VERTS && B->ni + 12 <= VSM_IDX) {
                    uint32_t nv2 = 0;
                    if (vsm_gather(&nv2)) vsm_append(b, nv2);
                    return;
                }
            } else if (g_vsm.nb < (g_vsm.on == 1 ? 1 : VSM_NB)) vsm_keep = 1;   /* (another texture: a bucket more, the others not drawn yet) */
        }
    }
    if (!vsm_keep) vsm_flush();
    if (!(g_jn.active && !s_last_chg && !s_gpu.inline_active && !g_cap_on)) jn_flush();   /* (v117: unless this draw may join the one kept back) */
    {   /* (the first batches, written out at once: a crash keeps them) */
        static unsigned nb;
        if (nb < 4) {
            fprintf(stderr, "  [VGL] batch %u: prim %u, %u indices, target %08X %ux%u%s\n", nb, s_gpu.prim, n,
                    dma_resolve(s_gpu.color_offset), s_gpu.clip_x + s_gpu.clip_w, s_gpu.clip_y + s_gpu.clip_h,
                    need_depth ? " + zeta" : "");
            fflush(stderr);
        }
        nb++;
    }
#endif
#if defined(__vita__)
    {   /* (v231) the lights' pass (v125, TT_LIGHTS=1 to draw it) dropped here, before the targets, the raster state and
         * the program are set up for it: ~100 of Helm's Deep's draws a frame went through all of that only to be
         * dropped in gpu_batch_hw_direct. Only the added-on draws (ONE/ONE) look the program up a second time. */
        static int lights = -1;
        if (lights < 0) { const char *e = getenv("TT_LIGHTS"); lights = e && e[0] == '1'; }
        if (!lights && !g_cap_on && !s_gpu.inline_active && s_3d.mode == 2 && s_vpd_len > 3 && s_gpu.blend_enable
            && (s_gpu.blend_sfactor & 0xFFFF) == 1 && (s_gpu.blend_dfactor & 0xFFFF) == 1) {
            HwVp *hl = hwvp_lookup();
            if (hl && hl->hash == 0x453E894Bu) { g_lite_skipped[2]++; jn_flush(); return; }
        }
    }
    {
        VP_T0(t);
        GSurf *was = (VWHERE("batch: targets"), g_cur_rt);
        int ok;
        { BK_T0(); ok = gpu_bind_targets(need_depth); BK_END(BK_TARGETS); }
        if (g_cap_wait == -2) { const char *e = getenv("RECOMP_VGL_CAPTURE"); g_cap_wait = e ? atoi(e) : 0; if (g_cap_wait <= 0) g_cap_wait = -3; }

        if (g_cap_on && was && was != g_cur_rt) { cap_save(was, "left"); ok = gpu_bind_targets(need_depth); }
        VP_ADD(VP_TARGETS, t);
        if (!ok) { if (g_cap_on) cap_note_text("SKIP: no target (colour %08X clip %ux%u)", s_gpu.color_offset, s_gpu.clip_w, s_gpu.clip_h); return; }
    }
#else
    if (!gpu_bind_targets(need_depth)) return;
#endif
    VWHERE("batch: raster state");
    if (!gpu_set_raster()) { if (g_cap_on) cap_note_text("SKIP: raster state"); return; }
    g_gen++; if (!g_gen) g_gen = 1;
    g_nidx = 0;
#if defined(__vita__)
    if (s_3d.mode == 2 && s_vpd_len > 3) {
        VP_T0(t_lk);
        VWHERE("batch: vertex program lookup");
        HwVp *hv = hwvp_lookup();
        VP_ADD(VP_LOOKUP, t_lk);
        g_frame_3d = 1;
        if (hv && !vsm_small && gpu_batch_hw_direct(hv)) return;
    }
    jn_flush();   /* (v117: another path draws) */
    VP_T0(t_idx);
#endif
    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3) gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; i++) {
            if (i & 1) gpu_tri(s_gpu.idx[i+1], s_gpu.idx[i], s_gpu.idx[i+2], &nverts);
            else       gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        }
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < n; i++) gpu_tri(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1], &nverts);
        break;
    case NV_PRIM_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3], &nverts);
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < n; i += 2) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+3], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+3], s_gpu.idx[i+2], &nverts);
        }
        break;
    default:
        return;
    }
#if defined(__vita__)
    VP_ADD(VP_IDX, t_idx);
    VWHERE("batch: after indices");
#endif
    if (!g_nidx) { if (g_cap_on) cap_note_text("SKIP: no triangles (prim %u, %u indices, inline %d)", s_gpu.prim, n, s_gpu.inline_active); return; }
    if (s_3d.mode == 2 && s_vpd_len > 3) g_frame_3d = 1;
    if (g_wide_prev && s_3d.mode == 2 && s_vpd_len <= 3 && g_aspect > 1.34f) {
        static int hud = -1;
        if (hud < 0) hud = getenv("RECOMP_HUD_STRETCH") == NULL;
        if (hud) {
            /* the 2D overlay at 4:3 proportions in a widened frame (see
             * nv2a_d3d11.inc) */
            uint32_t k; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f, kk = g_aspect / (4.0f / 3.0f);
            int samples_frame = 0, st;
#if defined(__vita__)
            { VP_T0(t); gpu_transform_all(nverts); VP_ADD(VP_XFORM, t); g_vp_verts += nverts; }
#else
            gpu_transform_all(nverts);
#endif
            gpu_drop_bad_tris(nverts);
            if (!g_nidx) return;
            for (k = 0; k < nverts; k++) if (g_vok[k]) {
                float x = g_verts[k].p[0] / g_verts[k].p[3], y = g_verts[k].p[1] / g_verts[k].p[3];
                if (x < mnx) mnx = x; if (x > mxx) mxx = x;
                if (y < mny) mny = y; if (y > mxy) mxy = y;
            }
            for (st = 0; st < 4; st++) {
                int isd;
                if (s_stage[st].enabled && gpu_surface_for_texture(s_stage[st].t.offset, s_stage[st].t.width, s_stage[st].t.height, &isd)) samples_frame = 1;
            }
            if (!samples_frame && (mxx - mnx) < 400.0f && (mxx - mnx) * (mxy - mny) < 0.2f * 640.0f * 480.0f
                && g_cur_rt && g_cur_rt->w >= 640)
            {
                s_hud_own = n > 0 && hud_own_piece(vertex_color(s_gpu.idx[0]), mnx, mxx, mny, mxy);
                hud_note(mnx, mxx, mny, mxy);
                for (k = 0; k < nverts; k++) if (g_vok[k]) {
                    float w = g_verts[k].p[3], x = g_verts[k].p[0] / w;
                    g_verts[k].p[0] = hud_wide_x(x, (mnx + mxx) * 0.5f, (mny + mxy) * 0.5f, kk) * w;
                }
                s_hud_own = 0;   /* (the overlay's quads use hud_wide_x too) */
            }
            goto cpu_verts_ready;
        }
    }
    {
#if defined(__vita__)
        VP_T0(t_lk);
        VWHERE("batch: vertex program lookup");
        HwVp *hv = hwvp_lookup();
        VP_ADD(VP_LOOKUP, t_lk);
#else
        HwVp *hv = hwvp_lookup();
#endif
#if defined(__vita__)
        if (vsm_small) hv = NULL;
#endif
        if (hv) {
            if (gpu_batch_hw(hv, nverts)) return;
        }
        if (s_3d.mode == 2) g_hwvp_stat_cpu++;
    }
#if defined(__vita__)
    { VP_T0(t); gpu_transform_all(nverts); VP_ADD(VP_XFORM, t); g_vp_verts += nverts; }
#else
    gpu_transform_all(nverts);
#endif
    gpu_drop_bad_tris(nverts);
    if (!g_nidx) return;
cpu_verts_ready:
    {
        GLuint prog;
#if defined(__vita__)
        /* vitaGL copies client arrays at the draw */
        (void)vb_at; (void)ib_at;
        { VWHERE("batch: textures/stages"); VP_T0(t); gpu_stages_vgl(); VP_ADD(VP_STAGES, t); }
        gpu_set_blend(0);
        gpu_set_depth(g_cur_ds != NULL);
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (i = 0; i < 8; i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GVert), (const char *)g_verts + 16 * i);
        }
        g_sh.vao = 0;
        prog = gpu_program_for(g_vs_main);
        gpu_use(prog);
        if (vsm_small && nverts <= 8 && g_nidx <= 12 && g_vsm.nb < VSM_NB) {
            /* a run of quads begins (or a bucket more): its state is bound, its uniforms sent */
            VsmBucket *B = &g_vsm.b[g_vsm.nb];
            vgl_ub_flush();
            if (!g_vsm.nb) g_vsm_gk = g_vsm_kg;
            B->key = g_vsm_k; B->has_ds = g_cur_ds != NULL; B->nv = B->ni = 0;
            memcpy(B->tex, g_sh.tex, sizeof B->tex); memcpy(B->smp, g_sh.smp, sizeof B->smp); B->prog = prog;
            vsm_append(g_vsm.nb, nverts);
            g_vsm.nb++;
            return;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        { VP_T0(t); vgl_draw_elements(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, g_idx); VP_ADD(VP_DRAW, t); g_vp_draws++; g_cap_cpu++; }
        if (g_cur_ds) vgl_depth_keep();
        cap_note_draw("cpu", s_vpd_len ? 1 : 0, g_nidx);
        if (0)
#endif
        {
        vb_at = gpu_ring(&g_rvb, g_verts, nverts * sizeof(GVert), sizeof(GVert));
        ib_at = gpu_ring(&g_rib, g_idx, g_nidx * 4, 4);
        if (vb_at == 0xFFFFFFFFu || ib_at == 0xFFFFFFFFu) return;
        gpu_bind_stages();
        gpu_set_blend(0);
        gpu_set_depth(g_cur_ds != NULL);
        if (g_sh.vao != 1) {
            /* the CPU path's vertex layout: 8 float4s (GVert) */
            glBindVertexArray(g_vao);
            glBindBuffer(GL_ARRAY_BUFFER, g_rvb.buf);
            for (i = 0; i < 8; i++) {
                glEnableVertexAttribArray(i);
                glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GVert), (const void *)(uintptr_t)(16 * i));
            }
            g_sh.vao = 1;
        }
        prog = gpu_program_for(g_vs_main);
        gpu_use(prog);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_rib.buf);
        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, (const void *)(uintptr_t)ib_at,
                                 (GLint)(vb_at / sizeof(GVert)));
        }
    }
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
}

/* ---------------- clears, copies, read-backs ---------------- */

/* CLEAR_SURFACE. Colour with the per-channel mask inside the clip rectangle
 * (D3D draws a quad for it; a GL clear honours the scissor and colour mask
 * the same way); depth and stencil clear the whole view, as D3D's
 * ClearDepthStencilView does. */
#if defined(__vita__)
static void gpu_clear_impl(uint32_t param);
static void gpu_clear(uint32_t param) { BK_T0(); gpu_clear_impl(param); BK_END(BK_CLEAR); }
static void gpu_clear_impl(uint32_t param)
#else
static void gpu_clear(uint32_t param)
#endif
{
    int need_depth = (param & 3) != 0;
#if defined(__vita__)
    vsm_flush(); jn_flush();
#endif
    fr_add(7, param & 0xFF, g_cur_rt ? g_cur_rt->base : 0, g_cur_ds ? g_cur_ds->base : 0, s_cb_scratch[0], s_gpu.clear_color);
    cap_note_text("CLEAR param %02X colour %08X target %08X %ux%u scissor %d,%d %dx%d", param & 0xFF, s_gpu.clear_color,
                  g_cur_rt ? g_cur_rt->base : 0, g_cur_rt ? g_cur_rt->w : 0, g_cur_rt ? g_cur_rt->h : 0,
                  g_cur_sc[0], g_cur_sc[1], g_cur_sc[2], g_cur_sc[3]);
    if (!gpu_bind_targets(need_depth)) return;
    if ((param & 3) && g_cur_ds) {
        uint32_t zv = s_cb_scratch[0];
        float z = (((s_gpu.format >> 4) & 0xF) == 1) ? (float)(zv & 0xFFFF) / 65535.0f : (float)(zv >> 8) / 16777215.0f;
        gpu_clear_ds(gsurf_draw_fbo(g_cur_rt, g_cur_ds), (int)(param & 3), z, (int)(zv & 0xFF));
        gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
    }
    if (param & 0xF0) {
        uint32_t c = s_gpu.clear_color;
        gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_cur_sc[0], g_cur_sc[1], g_cur_sc[2], g_cur_sc[3]);
        g_sh.sc_ok = 0;
        glColorMask((param & 0x10) != 0, (param & 0x20) != 0, (param & 0x40) != 0, (param & 0x80) != 0);
#if defined(__vita__)
        if ((param & 0xF0) != 0xF0) {
            vgl_cclear_quad(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, (c >> 24) / 255.0f,
                            (param & 0x10) != 0, (param & 0x20) != 0, (param & 0x40) != 0, (param & 0x80) != 0);
            gpu_sh_reset();
            return;
        }
#endif
        glClearColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, (c >> 24) / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_sh.bs_ok = 0;                 /* the colour mask */
    }
}

/* CopyRects between surfaces the GPU owns (the heat-haze copy of the back
 * buffer). 0 when the source is not a GPU surface: the caller copies guest
 * memory as before. */
#if defined(__vita__)
static int gpu_blit_impl(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp);
static int gpu_blit(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp)
{ int r; BK_T0(); r = gpu_blit_impl(sb, db, ix, iy, ox, oy, w, h, dp, bpp); BK_END(BK_BLIT); return r; }
static int gpu_blit_impl(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp)
#else
static int gpu_blit(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp)
#endif
{
    GSurf *src, *dst;
    uint32_t dw;
#if defined(__vita__)
    vsm_flush(); jn_flush();
    {   static int lite = -1;
        if (lite < 0) { const char *e = getenv("RECOMP_VGL_LITE"); lite = e && e[0] == '1';   /* (v106: off -- no gain, the game thread is the limit; black squares) */ }
        if (lite && !g_cap_on && gsurf_find(sb, 0)) {   /* (v105: the haze's copies of the frame) */
            int q;
            for (q = 0; q < 8; q++) if (g_lite_dst[q] == db) return 1;
            for (q = 0; q < 8; q++) if (!g_lite_dst[q]) { g_lite_dst[q] = db; break; }
            return 1;
        }
    }
#endif
    src = gsurf_find(sb, 0);
    cap_note_text("BLIT %08X (%s) -> %08X src %u,%u dst %u,%u %ux%u pitch %u bpp %u", sb, src ? "gpu" : "guest", db, ix, iy, ox, oy, w, h, dp, bpp);
    if (!src || bpp != 4) return 0;
    dw = dp / bpp;
    if (dw < ox + w) dw = ox + w;
    {
        /* a frame-pitch buffer is made frame sized at once */
        uint32_t dh = oy + h;
        if (dw == src->w && dh < src->h) dh = src->h;
        dst = gsurf_get(db, dw, dh, 0);
    }
    if (!dst) return 0;
    if (ix + w > src->w) w = src->w > ix ? src->w - ix : 0;
    if (iy + h > src->h) h = src->h > iy ? src->h - iy : 0;
    if (!w || !h) return 1;
    gsurf_resolve(src); gsurf_resolve(dst);   /* (MSAA: copies are between the textures) */
    if (dst->mrb) dst->ss_new = 1;
#if defined(__vita__)
    /* (v88) not vitaGL's glBlitFramebuffer: in the v87 capture (and the
     * v78-v86 ones), right after the heat haze's three copies 80004000 ->
     * 82BF0000 nothing more reached the frame -- the haze, the HUD, the copies
     * of the HUD bar, even a glClear: the frames whose HUD is missing all
     * have the blits just before. The region is drawn instead (the path
     * below for surfaces of different scales). RECOMP_VGL_BLIT=1: vitaGL's. */
    static int use_blit = -1;
    if (use_blit < 0) { const char *e = getenv("RECOMP_VGL_BLIT"); use_blit = !(e && e[0] == '0'); }   /* (v89: back to vitaGL's -- v88 changed nothing) */
    if (use_blit && src->pw * dst->w == dst->pw * src->w && src->ph * dst->h == dst->ph * src->h) {
#else
    if (src->pw * dst->w == dst->pw * src->w && src->ph * dst->h == dst->ph * src->h) {
#endif
        uint32_t l = SX(src, ix), t = SY(src, iy), r = SX(src, ix + w), b = SY(src, iy + h);
        if (r > src->pw) r = src->pw;
        if (b > src->ph) b = src->ph;
        if (SX(dst, ox) + (r - l) > dst->pw || SY(dst, oy) + (b - t) > dst->ph) return 1;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(src->tex, 0));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gpu_fbo(dst->tex, 0));
        glDisable(GL_SCISSOR_TEST);
#if defined(__vita__)
        /* (PS Vita) vitaGL's blit counts rows from the bottom of the memory,
         * the surfaces keep the guest's top row first: the v61 capture -- the
         * heat haze's copies of the fire landed mirrored, and the haze then
         * drew the copy texture's stale contents (the dark rectangles over
         * the fire). Both rectangles turned upside down. */
        {
            GLint dy0 = (GLint)SY(dst, oy), dy1 = dy0 + (GLint)(b - t);
            glBlitFramebuffer((GLint)l, (GLint)(src->ph - b), (GLint)r, (GLint)(src->ph - t),
                              (GLint)SX(dst, ox), (GLint)dst->ph - dy1, (GLint)(SX(dst, ox) + (r - l)), (GLint)dst->ph - dy0,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
#else
        glBlitFramebuffer((GLint)l, (GLint)t, (GLint)r, (GLint)b, (GLint)SX(dst, ox), (GLint)SY(dst, oy),
                          (GLint)(SX(dst, ox) + (r - l)), (GLint)(SY(dst, oy) + (b - t)), GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif
        gpu_sh_reset();
    } else {
        /* different scales: draw the region */
        float c[4];
        glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo(dst->tex, 0));
        glViewport((GLint)SX(dst, ox), (GLint)SY(dst, oy), (GLsizei)(SX(dst, ox + w) - SX(dst, ox)),
                   (GLsizei)(SY(dst, oy + h) - SY(dst, oy)));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        c[0] = (float)w / (float)src->w; c[1] = (float)h / (float)src->h;
        c[2] = (float)ix / (float)src->w; c[3] = (float)iy / (float)src->h;
        gpu_ub(UB_CLC, c, 16);
        glBindVertexArray(g_vao_empty);
        glUseProgram(g_pr_blit);
        glUniform1f(g_loc_ys_blit, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src->tex);
        glBindSampler(0, g_copy_smp);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        gpu_sh_reset();
    }
#if defined(__vita__)
    if (g_cap_on) { cap_note_text("BLIT done: %s path, dst %08X %ux%u", use_blit && src->pw * dst->w == dst->pw * src->w ? "glBlitFramebuffer" : "drawn", dst->base, dst->w, dst->h);
                    cap_save(dst, "blitdst"); }
#endif
    return 1;
}

/* A GPU surface back into guest memory (frame dumps, replay, captures). */
static void gpu_readback(uint32_t base, uint32_t pitch)
{
    GSurf *s = gsurf_find(base, 0);
    uint8_t *m = XR_PTR(base);
    if (!s) return;
#if defined(__vita__)
    jn_flush();
#endif
    gsurf_resolve(s);
    if (!pitch) pitch = s->w * 4;
#if defined(__vita__)
    {
        /* the surface's memory (its row 0 is the guest's top row: see
         * nv2a_gl_vita.inc), RGBA bytes back to the guest's BGRA */
        GLuint src = s->tex;
        const uint8_t *tp;
        uint32_t stride, x, y, wb = s->w * 4 < pitch ? s->w : pitch / 4;
        if (s->pw != s->w || s->ph != s->h) {
            if (!gpu_one(s->w, s->h)) return;
            gpu_copy_scaled(s->tex, s->pw, s->ph, s->pw, s->ph, gpu_fbo(g_one_tex, 0), s->w, s->h);
            src = g_one_tex;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFinish();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        tp = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
        {
            SceGxmTexture *gt = vglGetGxmTexture(GL_TEXTURE_2D);
            stride = gt ? sceGxmTextureGetStride(gt) : 0;
        }
        if (!stride) stride = ((src == g_one_tex ? g_one_w : s->w) + 7) / 8 * 8 * 4;
        if (tp)
            for (y = 0; y < s->h; y++) {
                const uint8_t *r = tp + (size_t)y * stride;
                uint8_t *d = m + (size_t)y * pitch;
                for (x = 0; x < wb; x++) {
                    d[4 * x + 0] = r[4 * x + 2]; d[4 * x + 1] = r[4 * x + 1];
                    d[4 * x + 2] = r[4 * x + 0]; d[4 * x + 3] = r[4 * x + 3];
                }
            }
        s->mem_sum = gpu_mem_sum(base, (s->pitch ? s->pitch : s->w * 4) * s->h);
        gpu_sh_reset();
        return;
    }
#endif
    if (s->pw == s->w && s->ph == s->h) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(s->tex, 0));
    } else {
        /* down to the title's size first */
        if (!gpu_one(s->w, s->h)) return;
        gpu_copy_scaled(s->tex, s->pw, s->ph, s->pw, s->ph, gpu_fbo(g_one_tex, 0), s->w, s->h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(g_one_tex, 0));
    }
    if (pitch % 4 == 0 && pitch >= s->w * 4) {
        glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(pitch / 4));
        glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    } else {
        uint32_t y, wb = s->w * 4 < pitch ? s->w * 4 : pitch;
        uint8_t *tmp = (uint8_t *)malloc((size_t)s->w * 4 * s->h);
        if (tmp) {
            glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, tmp);
            for (y = 0; y < s->h; y++) memcpy(m + (size_t)y * pitch, tmp + (size_t)y * s->w * 4, wb);
            free(tmp);
        }
    }
    s->mem_sum = gpu_mem_sum(base, (s->pitch ? s->pitch : s->w * 4) * s->h);
    gpu_sh_reset();
}

/* ---------------- graphics settings overlay ---------------- */

static int g_ov;
static GLuint g_menu_tex_a[3];
static int g_menu_w_a[3], g_menu_h_a[3];
static unsigned g_menu_ver_a[3];
#define g_menu_tex g_menu_tex_a[g_ov]
#define g_menu_w g_menu_w_a[g_ov]
#define g_menu_h g_menu_h_a[g_ov]
#define g_menu_ver g_menu_ver_a[g_ov]
static void gpu_set_alpha_blend(void)
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
}
/* The panel (src/video/gfx_menu.c) over the finished frame, centred, at one
 * texel per screen pixel; the window framebuffer is current. */
static void gpu_draw_menu_at(int slot, const uint32_t *px, int w, int h, unsigned ver, int ox, int oy)
{
    float cb[4] = { 1, 1, 0, 0 };
    int left, top;
    g_ov = slot % 3;
    if (!g_menu_tex || g_menu_w != w || g_menu_h != h) {
        if (g_menu_tex) glDeleteTextures(1, &g_menu_tex);
        g_menu_tex = gpu_tex2d((uint32_t)w, (uint32_t)h, 0);
        g_menu_w = w; g_menu_h = h; g_menu_ver = ver - 1;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_menu_tex);
    if (g_menu_ver != ver) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        g_menu_ver = ver;
    }
    left = ox >= 0 ? ox : (g_sc_w - w) / 2; top = oy >= 0 ? oy : (g_sc_h - h) / 2;
    glViewport(left, g_sc_h - top - h, w, h);
#if defined(__vita__)
    glEnable(GL_SCISSOR_TEST);   /* (the triangle past the viewport: gpu_present) */
    glScissor(left, g_sc_h - top - h, w, h);
#endif
    gpu_ub(UB_CLC, cb, 16);
    gpu_set_alpha_blend();
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, -1.0f);
    glBindSampler(0, g_present_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    gpu_sh_reset();
}

/* A setting changed in the overlay: take the new values, between frames. */
static void gpu_apply_settings(void)
{
    int i, rs, aniso, mips;
    float asp, rsf;
    g_gfx_seen = xbox_gfx_rev;
    g_aa_on = xbox_GfxFxaa();
    g_vsync_on = xbox_GfxGet(GFX_VSYNC);
    g_stretch_on = xbox_GfxGet(GFX_STRETCH);
    g_hwvp_user = xbox_GfxGet(GFX_VP);
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
#if defined(__vita__)
    {   /* (v167) the present sharpens by default on the Vita (the 85% picture stretched to the screen looked soft):
         * TT_SHARP=0..2 (0 off; default 0.8) */
        const char *e = getenv("TT_SHARP");
        if (e) g_post[1] = (float)atof(e); else if (g_post[1] <= 0.0f) g_post[1] = 0.8f;
        if (g_post[1] < 0.0f) g_post[1] = 0.0f;
        if (g_post[1] > 2.0f) g_post[1] = 2.0f;
    }
#endif
    gpu_filter_opts();
    aniso = xbox_GfxGet(GFX_ANISO);
    mips = xbox_GfxGet(GFX_MIPS);
#if defined(__vita__)
    mips = g_mips; aniso = g_aniso;   /* (v198: the Vita's own choice, gpu_filter_opts) */
#endif
    if (aniso != g_aniso || mips != g_mips) {
        gpu_samplers_drop();
        if (mips != g_mips)
            for (i = 0; i < GTEX_MAX; i++) {
                if (g_tex[i].tex) glDeleteTextures(1, &g_tex[i].tex);
                memset(&g_tex[i], 0, sizeof g_tex[i]);
            }
        g_aniso = aniso; g_mips = mips;
        g_present_smp = gpu_sampler(3, 3, 1);
        g_copy_smp = g_present_smp;
    }
    rs = xbox_GfxGet(GFX_SCALE);
    rsf = xbox_GfxScaleF();
#if defined(__vita__)
    rs = 1; rsf = vita_res_scale();   /* (PS Vita: 1x or under -- a larger scale remade every surface and crashed) */
#endif
    asp = xbox_GfxAspect();
    if (asp < 1.34f) asp = 4.0f / 3.0f;
    {
#if defined(__vita__)
        int ms = 1;
#else
        int ms = g_msaa_bad ? 1 : xbox_GfxMsaa();
#endif
        if (ms != g_msaa) { g_msaa = ms; g_rsf = -1.0f; }   /* (MSAA changed: every surface made again) */
    }
    if (rsf != g_rsf || asp != g_aspect) {
        for (i = 0; i < GSURF_MAX; i++)
            if (g_surf[i].tex) gsurf_free(&g_surf[i]);
        g_cur_rt = g_cur_ds = NULL;
        g_rs = rs; g_rsf = rsf; g_aspect = asp;
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
    }
    gpu_sh_reset();
    fprintf(stderr, "  [GL] settings: scale %g (%.2f x %.2f), picture %.3f:1, aniso x%d, mipmaps %s, %s, vsync %s, %s, vertex programs on the %s\n",
            (double)g_rsf, g_rsx, g_rsy, g_aspect, g_aniso, g_mips ? "on" : "off", g_msaa > 1 ? (g_aa_on ? "MSAA + FXAA" : "MSAA") : g_aa_on ? "FXAA" : "no AA",
            g_vsync_on ? "on" : "off", g_stretch_on ? "stretched" : "shape kept", g_hwvp_user ? "GPU" : "CPU");
    fflush(stderr);
}

/* ---------------- title overlays (HUD mods) ---------------- */

typedef struct {
    const uint32_t *px;          /* BGRA, straight alpha, w*h; NULL = slot unused */
    int w, h;
    unsigned ver;                /* change when px changes */
    float x, y, dw, dh;          /* 640x480 game coordinates */
} XboxOverlayQuad;
#define OVL_MAX 8
int (*volatile xbox_overlay_cb)(XboxOverlayQuad *q, int max);
static struct { GLuint tex; int w, h; unsigned ver; } g_ovl[OVL_MAX];

static void gpu_draw_overlays(GSurf *s, int wide)
{
    XboxOverlayQuad q[OVL_MAX];
    int n, i;
    float k = wide ? g_aspect / (4.0f / 3.0f) : 1.0f;
    if (!xbox_overlay_cb || !s || s->is_depth) return;
    memset(q, 0, sizeof q);
    VWHERE("present: the HUD's overlays made");   /* (v160) */
    n = xbox_overlay_cb(q, OVL_MAX);
    VWHERE("present: the HUD's overlays drawn");
    if (n <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo(s->tex, 0));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gpu_set_alpha_blend();
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, 1.0f);
    glBindSampler(0, g_present_smp);
    glActiveTexture(GL_TEXTURE0);
    for (i = 0; i < n && i < OVL_MAX; i++) {
        float cb[4] = { 1, 1, 0, 0 }, x0, x1;
        if (!q[i].px || q[i].w <= 0 || q[i].h <= 0) continue;
        if (!g_ovl[i].tex || g_ovl[i].w != q[i].w || g_ovl[i].h != q[i].h) {
            if (g_ovl[i].tex) glDeleteTextures(1, &g_ovl[i].tex);
            memset(&g_ovl[i], 0, sizeof g_ovl[i]);
            g_ovl[i].tex = gpu_tex2d((uint32_t)q[i].w, (uint32_t)q[i].h, 0);
            g_ovl[i].w = q[i].w; g_ovl[i].h = q[i].h; g_ovl[i].ver = q[i].ver - 1;
        }
        glBindTexture(GL_TEXTURE_2D, g_ovl[i].tex);
        if (g_ovl[i].ver != q[i].ver) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, q[i].w, q[i].h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, q[i].px);
            g_ovl[i].ver = q[i].ver;
        }
        /* HUD art keeps its 4:3 shape in a widened frame (as the game's own) */
        x0 = ovl_wide_x(q[i].x, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k, q[i].ver);
        x1 = ovl_wide_x(q[i].x + q[i].dw, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k, q[i].ver);
        {
            float vx = x0 * (float)s->pw / (float)s->w, vy = q[i].y * (float)s->ph / (float)s->h;
            float vw = (x1 - x0) * (float)s->pw / (float)s->w, vh = q[i].dh * (float)s->ph / (float)s->h;
            glViewport((GLint)floorf(vx + 0.5f), (GLint)floorf(vy + 0.5f), (GLsizei)floorf(vw + 0.5f), (GLsizei)floorf(vh + 0.5f));
        }
        gpu_ub(UB_CLC, cb, 16);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    gpu_sh_reset();
}

/* A one-off window dump requested by the title (testing). */
static volatile char g_dump_req[260];
void xbox_PresentDumpRequest(const char *path)
{
    if (path && strlen(path) < sizeof g_dump_req - 1) { memcpy((char *)g_dump_req, path, strlen(path) + 1); }
}

static void gpu_dump_window(const char *nm)
{
    uint8_t *buf;
    FILE *o;
    uint32_t w = (uint32_t)g_sc_w, h = (uint32_t)g_sc_h;
#if defined(__vita__)
    (void)buf; (void)o; (void)nm; (void)w; (void)h;
    return;
#endif
    if (!w || !h) return;
    buf = (uint8_t *)malloc((size_t)w * h * 4);
    if (!buf) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);
    if ((o = fopen(nm, "wb"))) {
        /* GL's rows run bottom-up, which is a BMP's own order */
        uint8_t hd[54] = { 'B', 'M' };
        uint32_t row = w * 4, img = row * h, tot = img + 54;
        int32_t hh = (int32_t)h;
        memcpy(hd + 2, &tot, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &w, 4); memcpy(hd + 22, &hh, 4);
        hd[26] = 1; hd[28] = 32; memcpy(hd + 34, &img, 4);
        fwrite(hd, 1, 54, o);
        fwrite(buf, 1, img, o);
        fclose(o);
    }
    free(buf);
    gpu_sh_reset();
}

/* The flip: the finished surface onto the window. */
#if defined(__vita__)
/* (the flight recorder, at each present: see fr_add) */
static void fr_dump(GSurf *s, float mean, float avg)
{
    static int nd;
    char path[96];
    FILE *f;
    uint32_t k, i;
    if (nd >= 3) return;
    sceIoMkdir("ux0:data/twotowers/cap", 0777);
    snprintf(path, sizeof path, "ux0:data/twotowers/cap/flash_%d.txt", nd);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "frame mean %.1f, the frames before %.1f; surface %08X; the last 4 frames, oldest first\n"
               "kinds: 1 hw draw, 2 direct draw, 6 cpu draw (prog idx target zeta flags[dt,dm,func<<2,blend<<6,stencil<<7,rgba mask<<8,stfunc<<12,prim<<16])\n"
               "       7 CLEAR (param target zeta zvalue colour), 3 depth-clear quad (which z*1e6 stencil viewport w h),\n"
               "       4 zeta clear carried (fbo which z*1e6 st), 5 clear viewport (fbo surface w h)\n", mean, avg, s->base);
    for (k = 1; k <= 4; k++) {
        uint32_t fi = (g_fr_cur + k) & 3;
        fprintf(f, "\n=== frame -%u: %u records, mean %.1f\n", 4 - k, g_fr_n[fi], g_fr_mean[fi]);
        for (i = 0; i < g_fr_n[fi]; i++) {
            const FrRec *r = &g_fr[fi][i];
            fprintf(f, "%4u k%u %08X %u %08X %08X %08X\n", i, r->kind, r->a, r->b, r->c, r->d, r->e);
        }
    }
    fclose(f);
    {   /* the picture as it is in memory (the frame the GPU finished last) */
        const uint8_t *px;
        uint8_t hd[18];
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
        snprintf(path, sizeof path, "ux0:data/twotowers/cap/flash_%d_%08X.tga", nd, s->base);
        if (px && (f = fopen(path, "wb")) != NULL) {
            memset(hd, 0, sizeof hd);
            hd[2] = 2; hd[12] = s->pw & 0xFF; hd[13] = s->pw >> 8; hd[14] = s->ph & 0xFF; hd[15] = s->ph >> 8; hd[16] = 32; hd[17] = 8;
            fwrite(hd, 1, 18, f);
            cap_write_bgra(f, px, s->pw * s->ph);
            fclose(f);
        }
        gpu_sh_reset();
    }
    fprintf(stderr, "  [FLASH] a dark frame (mean %.1f, before %.1f): ux0:data/twotowers/cap/flash_%d.txt\n", mean, avg, nd);
    fflush(stderr);
    nd++;
}
static void fr_frame_end(GSurf *s)
{
    static float avg;
    static int warm;
    float mean = -1.0f;
    /* (v67: off -- reading the picture each frame (vglGetTexDataPointer)
     * waited for the GPU in the present; RECOMP_VGL_FLASH=1 turns it on) */
    static int on = -1;
    if (on < 0) { const char *e = getenv("RECOMP_VGL_FLASH"); on = e && e[0] == '1'; }
    if (on && s && s->tex && !s->is_depth && s->pw >= 640 && g_fr_n[g_fr_cur] > 600) {
        const uint8_t *px;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        px = (const uint8_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
        g_sh.tex_ok = 0;
        if (px) {
            uint32_t x, y, sum = 0, n = 0;
            for (y = 24; y < s->ph; y += 24)
                for (x = 16; x < s->pw; x += 16) {
                    const uint8_t *q = px + ((size_t)y * s->pw + x) * 4;
                    sum += (uint32_t)q[0] + q[1] + q[2]; n++;
                }
            mean = n ? (float)sum / (3.0f * (float)n) : -1.0f;
        }
    }
    g_fr_mean[g_fr_cur] = mean;
    if (mean >= 0.0f) {
        if (warm > 30 && avg > 15.0f && mean < avg * 0.4f) fr_dump(s, mean, avg);
        else { avg = warm ? avg * 0.9f + mean * 0.1f : mean; warm++; }
    }
    g_fr_cur = (g_fr_cur + 1) & 3;
    g_fr_n[g_fr_cur] = 0;
}
static void gpu_present_impl(uint32_t base, uint32_t pitch);
#if defined(__vita__)
/* (v161, PS Vita) the dynamic resolution. Where the GPU (the SGX) is the limit -- the screen's clear at the
 * present waits for it, 25-40% of the render thread in some places (v160) -- the internal resolution steps down
 * from TT_RES (0.85: 544x408) to 0.72 (461x346); where it waits no more, back up. Measured each second: the
 * clear's wait against the frame time. Down after 2 seconds over 15%, up after 8 seconds under 3%, never
 * twice within 10 s. Only in 3D play (no movie, no menu). A step makes the surfaces again (their content comes
 * back from memory; a frame may lose a shadow or a glow). TT_DYNRES=0: TT_RES always; TT_DYNRES=0.65..0.84:
 * the lower step. */
float g_vgl_dynres;
long long g_vgl_clear_ticks;
static void vgl_dynres_set(float f)
{
    int i;
    g_vgl_dynres = f;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex) gsurf_free(&g_surf[i]);
    g_cur_rt = g_cur_ds = NULL;
    g_rsf = vita_res_scale();
    g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
    g_rsy = g_rsf;
    rs_fit();
    gpu_sh_reset();
    fprintf(stderr, "  [DYNRES] internal resolution now %.0f x %.0f (%.2f)\n", 640.0f * g_rsf, 480.0f * g_rsf, (double)g_rsf);
}
static void vgl_dynres(void)
{
    static int on = -1, hot, cool;
    static float low = 0.72f;
    static long long fq, t_last, t_sw, frame_sum, clear0;
    extern int xbox_MoviePlaying(void);
    extern volatile int xbox_frontend_hint;
    LARGE_INTEGER now;
    if (on < 0) {
        const char *e = getenv("TT_DYNRES");
        /* (v162: off unless asked -- v161 on the console: after the step down, every other frame drew no figures, the
         * two screen surfaces made again with one zeta surface between them; TT_DYNRES=1 or a step: on) */
        on = e && (e[0] == '1' || atof(e) >= 0.5f);
        if (e && atof(e) >= 0.5f && atof(e) < 0.85f) low = (float)atof(e);
        { LARGE_INTEGER f; QueryPerformanceFrequency(&f); fq = f.QuadPart; }
    }
    QueryPerformanceCounter(&now);
    if (!on) return;
    if (!t_last) { t_last = now.QuadPart; t_sw = now.QuadPart; clear0 = g_vgl_clear_ticks; return; }
    frame_sum = now.QuadPart - t_last;
    if (frame_sum < fq) return;   /* (once a second) */
    {
        double r = (double)(g_vgl_clear_ticks - clear0) / (double)frame_sum;
        int play = !xbox_MoviePlaying() && xbox_frontend_hint != 1;
        t_last = now.QuadPart; clear0 = g_vgl_clear_ticks;
        if (!play) { hot = cool = 0; return; }
        if (r > 0.15) { hot++; cool = 0; } else if (r < 0.03) { cool++; hot = 0; } else hot = cool = 0;
        if (now.QuadPart - t_sw < fq * 10) return;
        if (g_vgl_dynres == 0.0f && hot >= 2 && low < vita_res_scale()) { vgl_dynres_set(low); t_sw = now.QuadPart; hot = cool = 0; }
        else if (g_vgl_dynres > 0.0f && cool >= 8) { vgl_dynres_set(0.0f); t_sw = now.QuadPart; hot = cool = 0; }
    }
}
#else
static void vgl_dynres(void) {}
#endif
static void gpu_present(uint32_t base, uint32_t pitch)
{
    VP_T0(t);
    vsm_flush(); jn_flush();
    { BK_T0(); gpu_present_impl(base, pitch); BK_END(BK_PRESENT); }
    VP_ADD(VP_PRESENT, t);
    vgl_perf_frame();
}
static void gpu_present_impl(uint32_t base, uint32_t pitch)
#else
static void gpu_present(uint32_t base, uint32_t pitch)
#endif
{
#if defined(__vita__)
    vgl_trf("gpu_present %08X", base);
#endif
    hud_frame_end();   /* this frame's HUD pieces, grouped (hud_wide_x) */
#if defined(__vita__)
    if (g_cap_on) {
        int i;
        GSurf *pr = gsurf_find(base, 0);
        cap_save(pr, "present");
        if (!g_cap_mode)
            for (i = 0; i < GSURF_MAX; i++) if (g_surf[i].tex && &g_surf[i] != pr && !g_surf[i].is_depth && g_surf[i].w >= 640) cap_save(&g_surf[i], "atpresent");
        g_cap_on = 0; g_cap_screen = 1;
        g_cap_wait = -3;   /* (one capture, as drawn) */
        if (g_cap_list) { fclose(g_cap_list); g_cap_list = NULL; }
        gpu_sh_reset();
    }
    if (g_cap_wait > 0 && g_cap_fdraws > 1000 && --g_cap_wait == 0) g_cap_wait = -11;   /* (the n-th frame of over 1000 draws: the battle, not the cut-scene) */
    {   /* (v69) L + R + SELECT held: a capture of the next frame, when the
         * fault is on the screen (the black HUD panels, the frame in negative) */
        SceCtrlData pad;
        static int held;
        memset(&pad, 0, sizeof pad);
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            int combo = (pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))
                        == (SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
            if (combo && !held && !g_cap_on && g_cap_wait <= -3 && g_cap_wait != -2) {
                g_cap_wait = -11; g_cap_n = 0; g_cap_ndraw = 0;
                snprintf(g_cap_dir, sizeof g_cap_dir, "ux0:data/twotowers/cap/%d", ++g_cap_idx);
                if (g_cap_list) { fclose(g_cap_list); g_cap_list = NULL; }
                sceIoMkdir("ux0:data/twotowers/cap", 0777);
                fprintf(stderr, "  [CAP] L+R+SELECT: capturing the next frame\n"); fflush(stderr);
            }
            held = combo;
        }
    }
    g_cap_last = g_cap_fdraws;
    g_cap_fdraws = 0;
    if (g_cap_wait < -10) { if (++g_cap_wait == -10) { g_cap_on = 1; g_cap_marked = 0; g_cap_hw = g_cap_cpu = 0; fprintf(stderr, "  [CAP] capturing this frame\n"); } }
    {   /* (v231) the GL worker thread (vgl_glt.c): started after the first frames; everything on this thread while a
         * capture is made (it reads vitaGL's state) */
        extern void glt_present_hook(int direct);
        glt_present_hook(g_cap_on || g_cap_screen || g_cap_wait < -3);
    }
#endif
    GSurf *s = gsurf_find(base, 0);
    int wide;
    {   /* testing: RECOMP_GPU_NULL=2 -- nothing shown either (the game's own speed, unpaced by the window) */
        static int nul = -1;
        if (nul < 0) { const char *e = getenv("RECOMP_GPU_NULL"); nul = !e ? 0 : *e == '2' ? 2 : *e == '3' ? 3 : 0; }
        if (nul == 2) return;
        if (nul == 3) {   /* (=3: a frame drawn and shown only when a picture is asked for -- the whole of the frame after the ask) */
            if (!g_dump_req[0]) { g_null3_draw = 0; return; }
            if (!g_null3_draw) { g_null3_draw = 1; return; }
        }
    }
    if (getenv("RECOMP_FB_DUMP")) {
        static unsigned every, k;
        if (!every) {
            const char *e = getenv("RECOMP_FB_WINDOW_DUMP_EVERY");
            every = (e && atoi(e) > 0) ? (unsigned)atoi(e) / 4 : 15;
            if (!every) every = 1;
        }
        if (k++ % every == 0) gpu_readback(base, pitch);
    }
    if (!g_win || !s) return;
    VWHERE("present: surface");
    gsurf_sync_from_mem(s);
    gsurf_resolve(s);   /* (MSAA: the picture from its samples) */
#if defined(__vita__)
    VWHERE("present: flight recorder");
    fr_frame_end(s);
    VWHERE("present: overlays");
#endif
    {
        extern int xbox_MoviePlaying(void);
        wide = g_aspect > 1.34f && g_frame_3d && !xbox_MoviePlaying() && g_ws_cam_tick != 0;
        gpu_draw_overlays(s, wide);
    }
#if defined(__vita__)
    VWHERE("present: after the overlays");   /* (v160: finer zones) */
#endif
    g_fhash_cur ^= 1; g_fhash_n[g_fhash_cur] = 0; g_fh_cur_gen++; if (!g_fh_cur_gen) { memset(g_fh_gen, 0, sizeof g_fh_gen); g_fh_cur_gen = 1; }
#if defined(__vita__)
    g_sc_w = 960; g_sc_h = 544;
#else
    if (SDL_GetWindowFlags(g_win) & SDL_WINDOW_MINIMIZED) goto settings;
    SDL_GL_GetDrawableSize(g_win, &g_sc_w, &g_sc_h);
#endif
    if (g_sc_w < 8 || g_sc_h < 8) goto settings;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, g_sc_w, g_sc_h);
#if defined(__vita__)
    VWHERE("present: clear the screen");
#endif
    glClearColor(0, 0, 0, 1);
#if defined(__vita__)
    {   /* (v161) how long the screen's clear waits for the GPU: the dynamic resolution's measure */
        extern long long g_vgl_clear_ticks;
        LARGE_INTEGER c0, c1;
        QueryPerformanceCounter(&c0);
        glClear(GL_COLOR_BUFFER_BIT);
        QueryPerformanceCounter(&c1);
        g_vgl_clear_ticks += c1.QuadPart - c0.QuadPart;
    }
    VWHERE("present: the picture to the screen");
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif
    {
        /* the picture's shape: g_aspect (4:3 unless widescreen), movies 4:3,
         * black bars round it; stretch fills the window */
        float a, ww = (float)g_sc_w, wh = (float)g_sc_h, pw, ph, uvs[4];
        int vx, vy;
        float mz;
        a = wide ? g_aspect : 4.0f / 3.0f;
        { extern int xbox_MoviePlaying(void); extern volatile int xbox_frontend_hint; mz = menu_fill_zoom((!g_frame_3d || xbox_frontend_hint == 1) && !xbox_MoviePlaying()); }
        g_wide_prev = wide; g_frame_3d = 0; g_present_n++; s_texc_vframe++;
#if defined(__vita__)
        xbox_VitaUniPoolFrame();
        vbs_frame();
#endif
        if (g_stretch_on < 0) g_stretch_on = getenv("RECOMP_PRESENT_STRETCH") != NULL;
        if (g_stretch_on) { pw = ww; ph = wh; }
        else if (ww / wh > a) { ph = wh; pw = wh * a; }
        else { pw = ww; ph = ww / a; }
        pw *= mz; ph *= mz;
        { extern int xbox_MoviePlaying(void); movie_fill(xbox_MoviePlaying(), ww, wh, &pw, &ph); }
        vx = (int)((ww - pw) * 0.5f); vy = (int)((wh - ph) * 0.5f);
        glViewport(vx, g_sc_h - vy - (int)ph, (GLsizei)pw, (GLsizei)ph);
#if defined(__vita__)
        /* (PS Vita) the viewport does not clip on the GXM: the full-screen
         * triangle (twice the viewport) went on past the picture into the
         * right-hand black bar -- the picture's last column stretched over
         * it, cut by the triangle's diagonal (the v61 screen capture) */
        glEnable(GL_SCISSOR_TEST);
        glScissor(vx, g_sc_h - vy - (int)ph, (GLsizei)pw, (GLsizei)ph);
#endif
        present_map_set(vx / ww, vy / wh, pw / ww, ph / wh, wide ? g_aspect / (4.0f / 3.0f) : 1.0f);
        uvs[0] = 640.0f / (float)s->w; uvs[1] = 480.0f / (float)s->h;
        uvs[2] = 1.0f / (float)s->pw; uvs[3] = 1.0f / (float)s->ph;
        gpu_ub(UB_CLC, uvs, 16);
        gpu_ub(UB_POST, g_post, 16);
    }
    glBindVertexArray(g_vao_empty);
    if (g_aa_on < 0) g_aa_on = xbox_GfxFxaa();
    if (g_aa_on && g_pr_fxaa) { glUseProgram(g_pr_fxaa); glUniform1f(g_loc_ys_fxaa, -1.0f); }
    else { glUseProgram(g_pr_present); glUniform1f(g_loc_ys_present, -1.0f); }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glBindSampler(0, g_present_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    {
        const uint32_t *mpx; int mw, mh; unsigned mver;
        xbox_GfxMenuSetAvailable(1);
        VWHERE("present: menus, toasts, banners");
        if (xbox_GfxMenuRender(g_sc_w, g_sc_h, &mpx, &mw, &mh, &mver)) gpu_draw_menu_at(0, mpx, mw, mh, mver, -1, -1);
        { int tx, ty; if (xbox_ToastRender(g_sc_w, g_sc_h, &mpx, &mw, &mh, &mver, &tx, &ty)) gpu_draw_menu_at(1, mpx, mw, mh, mver, tx, ty); }
        { int tx, ty; if (xbox_BannerRender(g_sc_w, g_sc_h, &mpx, &mw, &mh, &mver, &tx, &ty)) gpu_draw_menu_at(2, mpx, mw, mh, mver, tx, ty); }
    }
    {
        /* RECOMP_PRESENT_DUMP=prefix: every Nth presented frame as a BMP of
         * the window's picture (testing the final picture) */
        static const char *pd = (const char *)1; static unsigned pk, seq;
        static int pevery;
        int req = g_dump_req[0] != 0;
        if (pd == (const char *)1) pd = getenv("RECOMP_PRESENT_DUMP");
        if (!pevery) { const char *e = getenv("RECOMP_PRESENT_DUMP_EVERY"); pevery = e && atoi(e) > 0 ? atoi(e) : 300; }
        if (req || (pd && pk++ % pevery == (unsigned)pevery / 2)) {
            char nm[260];
            if (req) { snprintf(nm, sizeof nm, "%s", (const char *)g_dump_req); g_dump_req[0] = 0; }
            else snprintf(nm, sizeof nm, "%s%03u.bmp", pd, seq++);
            gpu_dump_window(nm);
        }
    }
    if (g_vsync_on < 0) {
        const char *v = getenv("RECOMP_VSYNC");
        g_vsync_on = v && v[0] == '1';
#if defined(__vita__)
        /* (v148: the paced picture shown at a refresh, not torn mid-screen.) (v197) Not any more unless
         * TT_PACE_VSYNC=1: with the swap waiting for a refresh a frame of 52-55 ms went out at 66.7 (15 fps)
         * and one of 68 at 83 (12 fps) -- v196 log: 25-37% of the render thread in vglSwapBuffers, the game
         * waiting on it. The pace itself already waits for the refresh when the frame is early; a late one
         * now goes out at once. */
        { extern int vita_pace_on(void); const char *pv = getenv("TT_PACE_VSYNC"); if (!v && vita_pace_on() && pv && pv[0] == '1') g_vsync_on = 1; }
#endif
    }
#if defined(__vita__)
    if (g_vsync_applied != g_vsync_on) {
        vglWaitVblankStart(g_vsync_on ? GL_TRUE : GL_FALSE);
        g_vsync_applied = g_vsync_on;
    }
    VWHERE("present: frame limit");
    frame_limit();
    if (g_cap_screen) {
        /* (v60, the capture: the Vita's screen itself, what is seen) */
        /* (16 rows at a time through the fixed buffer: no big malloc) */
        static uint8_t strip[960 * 16 * 4];
        FILE *f;
        uint8_t hd[18];
        int y;
        g_cap_screen = 0;
        glFinish();
        { char p2[96]; snprintf(p2, sizeof p2, "%s/99_screen_960x544.tga", g_cap_dir); f = fopen(p2, "wb"); }
        if (f) {
            int ok = 1;
            memset(hd, 0, sizeof hd);
            hd[2] = 2; hd[12] = 960 & 0xFF; hd[13] = 960 >> 8; hd[14] = 544 & 0xFF; hd[15] = 544 >> 8; hd[16] = 32; hd[17] = 8;
            fwrite(hd, 1, 18, f);
            for (y = 0; y < 544 && ok; y += 16) {
                memset(strip, 0, sizeof strip);
                glReadPixels(0, y, 960, 16, GL_RGBA, GL_UNSIGNED_BYTE, strip);
                ok = cap_write_bgra(f, strip, 960 * 16);
            }
            fclose(f);
            fprintf(stderr, "  [CAP] the screen: ux0:data/twotowers/cap/99_screen_960x544.tga%s\n", ok ? "" : " (write failed)");
        }
        if (g_cap_list) { fclose(g_cap_list); g_cap_list = NULL; }
        fprintf(stderr, "  [CAP] done: the game goes on\n"); fflush(stderr);
    }
    VWHERE("present: vglSwapBuffers");
#if defined(__vita__)
    vgl_tgt_frame();
#endif
#if defined(__vita__)
    {   /* (v206) [AB2]: 8 s turns of the alpha test dropped where it cannot fail / kept everywhere as before --
         * the picture is the same in both; each turn's frame time and the swap's wait. TT_AB2=0: no turns. */
        static int ab = -1;
        static unsigned long long t_ph, sw_us;
        static unsigned nfr;
        unsigned long long t0 = sceKernelGetProcessTimeWide(), t1;
        if (ab < 0) { const char *e = getenv("TT_AB2"); ab = e && e[0] == '1'; t_ph = t0; }   /* (v207: turns only with TT_AB2=1) */
        vglSwapBuffers(GL_FALSE);
        t1 = sceKernelGetProcessTimeWide();
        sw_us += t1 - t0; nfr++;
        if (t1 - t_ph > 8000000ull) {
            double f = nfr ? nfr : 1;
            fprintf(stderr, "  [AB2] alpha test %s: %u frames, %.1f ms each, %.1f ms of it the swap waiting for the GPU; stage setups with the test dropped %.0f, kept %.0f a frame\n",
                    g_ab2_ph ? "kept everywhere (as before)" : "dropped where it cannot fail", nfr, (t1 - t_ph) / 1000.0 / f, sw_us / 1000.0 / f, g_ab2_drop / f, g_ab2_keep / f);
            if (ab) { g_ab2_ph ^= 1; gpu_sh_reset(); }
            t_ph = t1; nfr = 0; sw_us = 0; g_ab2_drop = g_ab2_keep = 0;
        }
    }
#else
    vglSwapBuffers(GL_FALSE);
#endif
    VWHERE("present: deletions");
    vgl_frame_done();
    vgl_dynres();
    VWHERE("after the present");   /* (deletions three frames old: nv2a_gl_vita.inc) */
    {   /* (the first frames: say it works) */
        static unsigned shown;
        if (shown < 3 || (shown % 1800) == 0)
            fprintf(stderr, "  [VGL] frame %u presented (surface %ux%u)\n", shown, s->w, s->h);
        if (shown % 300 == 0) vgl_mem_report("every 300 frames");
        shown++;
    }
#else
    if (g_vsync_applied != g_vsync_on) {
        /* adaptive vsync where the driver has it, else plain */
        if (!g_vsync_on || SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(g_vsync_on ? 1 : 0);
        g_vsync_applied = g_vsync_on;
    }
    frame_limit();
    SDL_GL_SwapWindow(g_win);
#endif
    gpu_sh_reset();
    {
        static int said;
        GLenum err = glGetError();
        if (err != GL_NO_ERROR && said++ < 10) fprintf(stderr, "  [GL] error 0x%04X during the frame\n", err);
    }
settings:
    if (g_msaa_bad && g_msaa > 1) {
        fprintf(stderr, "  [GL] MSAA: a multisampled surface could not be made (video memory?): MSAA off\n"); fflush(stderr);
        gpu_apply_settings();
    }
    if (xbox_gfx_rev != g_gfx_seen) gpu_apply_settings();
}
