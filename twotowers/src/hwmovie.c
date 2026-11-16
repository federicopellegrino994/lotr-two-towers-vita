/* PS Vita: the game's Bink films played by the Vita's video decoder (v132).
 *
 * The recompiled Bink decoder makes about 15 frames a second of a 29.97 fps
 * film. A film with an H.264 copy at ux0:data/twotowers/movies/<name>.mp4
 * (made from the game's .bik with ffmpeg) is played by sceAvPlayer instead:
 * the hardware decodes it, its sound goes out on its own audio port, and the
 * game's film loop (0x000E8B70) gets the pictures:
 *   BinkDoFrame     -- no decoding; waits until the film's clock reaches the
 *                      frame the loop is on (the film's pace), and ends the
 *                      film when the player has played it all;
 *   BinkCopyToBuffer -- the newest picture, NV12 -> YUY2, into the game's
 *                      texture;
 *   BinkSetVolume   -- 0 (the Bink sound would play twice);
 *   BinkClose       -- the player stopped.
 * The hooks are in twotowers_manual.c. TT_HWMOVIES=0: Bink as before. */
#if defined(__vita__)
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/io/stat.h>
#include <psp2/display.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

extern void tt_dbg(const char *fmt, ...);
extern int sceGxmMapMemory(void *base, unsigned int size, int attr);
extern int sceGxmUnmapMemory(void *base);

static SceAvPlayerHandle s_h;
static volatile int s_on, s_audio_run;
static SceUID s_audio_thid = -1;
static int s_port = -1;
static uint8_t *s_last;          /* the newest picture, NV12, in cached memory */
static uint32_t s_w, s_h_px, s_have;
static unsigned s_frames_in, s_frames_out;
static uint64_t s_t0;
static int s_was_active, s_vc_started;
static uint32_t s_black_addr[2];
volatile uint32_t g_hwm_tex_addr[2], g_hwm_tex_ver[2];
uint32_t *volatile g_hwm_argb[2]; volatile uint32_t g_hwm_argb_w, g_hwm_argb_h;   /* (v138) */
void *volatile g_hwm_frame; volatile uint32_t g_hwm_fw, g_hwm_fh;   /* (v139) the newest decoder frame, for the renderer */
/* (v140) how a film picture reaches the screen: 2 (default) converted here to
 * RGBA in GPU-visible memory that the renderer's texture points at; 1 the
 * decoder's NV12 frame as a vitaGL YUV texture (v139: black here); 0 the
 * game's texture path (v138). TT_HWMOVIE_YUV=0/1/2. */
static int hwm_mode(void) { static int m = -1; if (m < 0) { const char *e = getenv("TT_HWMOVIE_YUV"); m = (e && *e >= '0' && *e <= '2') ? *e - '0' : 2; } return m; }
static int hwm_yuv_on(void) { return hwm_mode() == 1; }
void *volatile g_hwm_rgba; volatile uint32_t g_hwm_rgba_w, g_hwm_rgba_h;   /* (v140, unused since v141) */
/* (v141) the renderer's two film textures: their memory (from the renderer), the size
 * wanted (from here), which one to show (-1: none yet) */
void *volatile g_hwm_texmem[2]; volatile uint32_t g_hwm_texmem_w, g_hwm_texmem_h, g_hwm_texmem_stride;
volatile uint32_t g_hwm_req_w, g_hwm_req_h; volatile int g_hwm_show = -1;
static uint32_t *s_rgba_buf[2]; static uint32_t s_rgba_w, s_rgba_h; static int s_rgba_i;
static uint32_t s_pic, s_tex_pic[2];

/* ---- memory for the player ---- */
static void *mv_alloc(void *p, uint32_t align, uint32_t size) { (void)p; return memalign(align < 16 ? 16 : align, size); }
static void mv_free(void *p, void *ptr) { (void)p; free(ptr); }
#define MV_BLOCKS 16
static struct { void *base; SceUID uid; } s_blk[MV_BLOCKS];
static void *mv_alloc_frame(void *p, uint32_t align, uint32_t size)
{
    void *base = NULL; SceUID uid; int i;
    (void)p; (void)align;
    size = (size + 0x3FFFFu) & ~0x3FFFFu;   /* (CDRAM blocks are 256 KB) */
    uid = sceKernelAllocMemBlock("tt_movie", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
    if (uid < 0) {
        size = (size + 0xFFFFFu) & ~0xFFFFFu;
        uid = sceKernelAllocMemBlock("tt_movie", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
    }
    if (uid < 0) uid = sceKernelAllocMemBlock("tt_movie", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);   /* (v135: CDRAM and phycont are all vitaGL's) */
    if (uid < 0) {
        /* (v136) the system has no block of that size left: vitaGL's own video
         * memory pool (60+ MB free, already mapped for the GPU) */
        extern void *vglForceAllocAligned(uint32_t alignment, uint32_t size, unsigned char for_gpu);
        void *v = vglForceAllocAligned(0x40000u, size, 1);
        { static int said; if (said++ < 4) tt_dbg("  [HWMOVIE] a %u KB frame buffer from vitaGL's pool: %p\n", size >> 10, v); }
        if (v) for (i = 0; i < MV_BLOCKS; i++) if (!s_blk[i].base) { s_blk[i].base = v; s_blk[i].uid = -2; break; }
        if (!v) tt_dbg("  [HWMOVIE] no memory for a %u byte frame (%08X)\n", size, uid);
        return v;
    }
    { static int said; if (said++ < 4) tt_dbg("  [HWMOVIE] a %u KB frame buffer\n", size >> 10); }
    sceKernelGetMemBlockBase(uid, &base);
    sceGxmMapMemory(base, size, 3 /* read | write */);
    for (i = 0; i < MV_BLOCKS; i++) if (!s_blk[i].base) { s_blk[i].base = base; s_blk[i].uid = uid; break; }
    return base;
}
static void mv_free_frame(void *p, void *ptr)
{
    int i;
    (void)p;
    for (i = 0; i < MV_BLOCKS; i++) if (s_blk[i].base == ptr) {
        if (s_blk[i].uid == -2) { extern void vglFree(void *); vglFree(ptr); s_blk[i].base = NULL; return; }
        sceGxmUnmapMemory(ptr);
        sceKernelFreeMemBlock(s_blk[i].uid);
        s_blk[i].base = NULL;
        return;
    }
}

/* ---- its sound ---- */
static int mv_audio_thread(SceSize args, void *argp)
{
    SceAvPlayerFrameInfo fi;
    static int16_t silence[1024 * 2];
    (void)args; (void)argp;
    while (s_audio_run) {
        if (s_on && sceAvPlayerIsActive(s_h) && sceAvPlayerGetAudioData(s_h, &fi) && fi.pData) {
            if (s_port < 0) {
                int grain = fi.details.audio.size / 4;   /* (s16 stereo) */
                if (grain < 64 || grain > 4096) grain = 1024;
                s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain, fi.details.audio.sampleRate,
                                             fi.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
                tt_dbg("  [HWMOVIE] sound: %u Hz, %u channels, %u bytes a block (port %d)\n",
                       fi.details.audio.sampleRate, fi.details.audio.channelCount, fi.details.audio.size, s_port);
            }
            if (s_port >= 0) sceAudioOutOutput(s_port, fi.pData);
        } else if (s_port >= 0) {
            sceAudioOutOutput(s_port, silence);   /* (keeps the pace while the player has none ready) */
        } else {
            sceKernelDelayThread(2000);
        }
    }
    return 0;
}

int hwm_enabled(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("TT_HWMOVIES"); on = !(e && *e == '0'); }
    return on;
}

/* name: the game's file name (any path, .bik); 1 if the player has it */
/* (v142) 1 when <name>.mp4 is there (path: where); the placeholder .bik's
 * guest path for a film whose own .bik need not exist */
static int hwm_find(const char *name, char *path);
static int hwm_find(const char *name, char *path)
{
    char base[96];
    const char *b = name, *q;
    SceIoStat st;
    int n = 0;
    for (q = name; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    while (b[n] && b[n] != '.' && n < (int)sizeof base - 1) { char c = b[n]; base[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; n++; }
    base[n] = 0;
    {   /* (v133) wherever it was put: beside env.txt, or with the game's films */
        static const char *const dirs[] = {
            "ux0:data/twotowers/movies/", "ux0:data/twotowers/game_files/FMV/", "ux0:data/twotowers/game_files/movies/",
            "ux0:data/twotowers/game_files/", "ux0:data/twotowers/", "ux0:data/twotowers/movies/movies/" };
        int k;
        for (k = 0; k < (int)(sizeof dirs / sizeof dirs[0]); k++) {
            strcpy(path, dirs[k]); strcat(path, base); strcat(path, ".mp4");
            if (sceIoGetstat(path, &st) >= 0) return 1;
            strcpy(path, dirs[k]); strcat(path, base); strcat(path, ".MP4");
            if (sceIoGetstat(path, &st) >= 0) return 1;
        }
    }
    return 0;
}

int hwm_have(const char *name) { char p[256]; return hwm_enabled() && !s_on && hwm_find(name, p); }
int hwm_placeholder_ok(void)
{
    SceIoStat st;
    return sceIoGetstat("ux0:data/twotowers/game_files/_hwm.bik", &st) >= 0;
}
static int s_loop;
void hwm_set_loop(int on) { s_loop = on; }
int hwm_open(const char *name)
{
    static int loaded;
    char path[256];
    SceAvPlayerInitData init;
    int n = 0;
    if (!hwm_enabled() || s_on) return 0;
    if (!hwm_find(name, path)) { tt_dbg("  [HWMOVIE] %s: no .mp4 in movies/, game_files/FMV/, game_files/movies/, game_files/ or data/twotowers/: Bink plays it\n", name); return 0; }
    if (!loaded) { sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER); loaded = 1; }
    memset(&init, 0, sizeof init);
    init.memoryReplacement.allocate = mv_alloc;
    init.memoryReplacement.deallocate = mv_free;
    init.memoryReplacement.allocateTexture = mv_alloc_frame;
    init.memoryReplacement.deallocateTexture = mv_free_frame;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 3;   /* (v139: one shown, one ready, one being decoded) */
    init.autoStart = 1;
    s_h = sceAvPlayerInit(&init);
    /* (v134: the handle is an address -- 0x8BF8xxxx reads negative; the errors are 0x806Axxxx) */
    if (!s_h || ((uint32_t)s_h & 0xFFFF0000u) == 0x806A0000u) { tt_dbg("  [HWMOVIE] the player did not start (%08X)\n", (uint32_t)s_h); s_h = 0; return 0; }
    if (s_loop) sceAvPlayerSetLooping(s_h, 1);   /* (v142: a film the game plays round and round) */
    if ((n = sceAvPlayerAddSource(s_h, path)) < 0) {
        tt_dbg("  [HWMOVIE] the player refused %s (%08X)\n", path, (uint32_t)n);
        sceAvPlayerClose(s_h); s_h = 0; return 0;
    }
    g_hwm_show = -1;
    s_vc_started = 0; s_black_addr[0] = s_black_addr[1] = 0;
    s_have = 0; s_frames_in = s_frames_out = 0; s_was_active = 0; s_pic = 0; s_tex_pic[0] = s_tex_pic[1] = 0;
    s_on = 1;
    s_t0 = sceKernelGetProcessTimeWide();
    s_audio_run = 1;
    s_audio_thid = sceKernelCreateThread("tt_movie_snd", mv_audio_thread, 0x40, 0x4000, 0, 0, NULL);
    if (s_audio_thid >= 0) sceKernelStartThread(s_audio_thid, 0, NULL);
    tt_dbg("  [HWMOVIE] %s plays from %s on the video decoder (player %08X)\n", name, path, (uint32_t)s_h);
    return 1;
}

int hwm_active(void) { return s_on; }

/* The film loop is on frame `frame` (rate num/den per second): wait until
 * the player's clock is there. 1: the player has played the film to its end. */
int hwm_wait_frame(uint32_t frame, uint32_t num, uint32_t den)
{
    uint64_t want, t0 = sceKernelGetProcessTimeWide();
    if (!s_on) return 1;
    if (!num || !den) { num = 2997; den = 100; }
    want = (uint64_t)frame * den * 1000u / num;   /* ms */
    for (;;) {
        /* (v135) The player is not active until it has opened and started the
         * file -- a moment after AddSource. Inactive counts as the end only
         * once it has been active, or 5 s after the start (it never began). */
        if (!sceAvPlayerIsActive(s_h)) {
            uint64_t since = sceKernelGetProcessTimeWide() - s_t0;
            if (s_was_active || since > 5000000ull) {
                static int said;
                if (said++ < 4) tt_dbg("  [HWMOVIE] the player has stopped (%s, %.1f s, frame %u, %u pictures): the film ends\n",
                                       s_was_active ? "played" : "never started", since / 1e6, frame, s_frames_in);
                return 1;
            }
            if (sceKernelGetProcessTimeWide() - t0 > 200000) return 0;
            sceKernelDelayThread(2000);
            continue;
        }
        if (!s_was_active) { s_was_active = 1; tt_dbg("  [HWMOVIE] the player is playing after %.2f s\n", (sceKernelGetProcessTimeWide() - s_t0) / 1e6); }
        {   /* (v143) the pace: a new picture every second screen refresh
             * (59.94 Hz / 2 = the films' 29.97 fps), on a steady grid. Waiting
             * for the player's clock instead let the loop's presents fall one
             * and two refreshes apart in turn: 60 and 15-30 fps by turns. */
            static unsigned last;
            unsigned vc = (unsigned)sceDisplayGetVcount();
            (void)want;
            if (!s_vc_started || vc - last > 6) { last = vc; s_vc_started = 1; return 0; }   /* (the first picture, or after a stall: a new grid) */
            if (vc - last >= 2) { last += 2; return 0; }
        }
        if (sceKernelGetProcessTimeWide() - t0 > 200000) return 0;   /* (never hold the loop long) */
        sceKernelDelayThread(1000);
    }
}

/* The newest picture into the game's YUY2 texture (host pointer; guest: its
 * address, for the renderer). (v137) Only the newest of the pictures due is
 * read from the decoder's (uncached) memory, a texture already holding that
 * picture is left alone, and the renderer is told by a version number
 * (g_hwm_tex_ver) instead of checksumming the texture. */
void hwm_copy(uint8_t *dst, uint32_t guest, uint32_t pitch, uint32_t height)
{
    SceAvPlayerFrameInfo fi, last;
    int got = 0, k;
    uint32_t x, y, w, h, o = guest & 0x03FFFFFFu;
    if (!s_on) return;
    while (sceAvPlayerGetVideoData(s_h, &fi)) { last = fi; got = 1; s_frames_in++; }   /* (all that are due: the newest wins) */
    {   /* (v144) the game's own texture black (YUY2 16/128/128) once: before the
         * first picture and after the last the game draws it -- zeroes, which
         * read as green for an instant at each film's start and end */
        uint32_t oo = guest & 0x03FFFFFFu; int q;
        for (q = 0; q < 2; q++) if (s_black_addr[q] == oo) break;
        if (q == 2 && dst && pitch >= 4 && height) {
            uint32_t yy, xx, *row;
            for (q = 0; q < 2 && s_black_addr[q]; q++) {}
            if (q < 2) s_black_addr[q] = oo;
            for (yy = 0; yy < height; yy++) { row = (uint32_t *)(dst + yy * pitch); for (xx = 0; xx < pitch / 4; xx++) row[xx] = 0x80108010u; }
        }
    }
    if (hwm_mode() == 2) {
        /* (v141) the renderer owns two RGBA textures (nv2a_gl.inc gtex_get) and
         * gives their memory here: the picture is converted straight into the
         * one not on screen, then shown. (v140 pointed a texture at our own
         * memory -- vglOverloadTexDataPointer -- and drew black.) */
        o = guest & 0x03FFFFFFu;
        if (g_hwm_tex_addr[0] != o && g_hwm_tex_addr[1] != o) { if (!g_hwm_tex_addr[0]) g_hwm_tex_addr[0] = o; else g_hwm_tex_addr[1] = o; }
        if (got && last.pData && last.details.video.width && last.details.video.height && last.details.video.width <= 1920 && last.details.video.height <= 1088) {
            uint32_t fw = last.details.video.width, fh = last.details.video.height, *out;
            static uint8_t clip[1024]; static int ci;
            int nx;
            g_hwm_req_w = fw; g_hwm_req_h = fh;
            if (!g_hwm_texmem[0] || !g_hwm_texmem[1] || g_hwm_texmem_w != fw || g_hwm_texmem_h != fh) return;   /* (the renderer makes them at its next draw) */
            if (!s_last || fw != s_w || fh != s_h_px) { free(s_last); s_last = malloc(fw * fh * 3 / 2); s_w = fw; s_h_px = fh; }
            if (!s_last) return;
            memcpy(s_last, last.pData, fw * fh * 3 / 2);   /* (one read of the decoder's uncached memory) */
            if (!ci) { int i; for (i = 0; i < 1024; i++) { int v = i - 384; clip[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); } ci = 1; }
            nx = g_hwm_show >= 0 ? (g_hwm_show ^ 1) : 0;
            out = (uint32_t *)g_hwm_texmem[nx];
            for (y = 0; y < fh; y++) {
                const uint8_t *Y = s_last + y * fw, *UV = s_last + fw * fh + (y >> 1) * fw;
                uint32_t *op = out + y * g_hwm_texmem_stride;
                for (x = 0; x + 1 < fw; x += 2) {
                    int cu = (int)UV[0] - 128, cv = (int)UV[1] - 128;
                    int rv = 409 * cv + 128, gv = -100 * cu - 208 * cv + 128, bv = 516 * cu + 128;
                    int c0 = 298 * ((int)Y[0] - 16), c1 = 298 * ((int)Y[1] - 16);
                    /* (RGBA8 in memory: R, G, B, A -- little-endian 0xAABBGGRR) */
                    op[0] = 0xFF000000u | ((uint32_t)clip[((c0 + bv) >> 8) + 384] << 16) | ((uint32_t)clip[((c0 + gv) >> 8) + 384] << 8) | clip[((c0 + rv) >> 8) + 384];
                    op[1] = 0xFF000000u | ((uint32_t)clip[((c1 + bv) >> 8) + 384] << 16) | ((uint32_t)clip[((c1 + gv) >> 8) + 384] << 8) | clip[((c1 + rv) >> 8) + 384];
                    op += 2; Y += 2; UV += 2;
                }
            }
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            g_hwm_show = nx;
            s_frames_out++;
        }
        (void)dst; (void)pitch; (void)height;
        return;
    }
    if (hwm_yuv_on()) {
        /* (v139) the renderer draws the decoder's frame itself (nv2a_gl.inc gtex_get) */
        if (got && last.pData && last.details.video.width && last.details.video.height) {
            g_hwm_fw = last.details.video.width; g_hwm_fh = last.details.video.height;
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            g_hwm_frame = last.pData;
            s_frames_out++;
        }
        o = guest & 0x03FFFFFFu;
        if (g_hwm_tex_addr[0] != o && g_hwm_tex_addr[1] != o) { if (!g_hwm_tex_addr[0]) g_hwm_tex_addr[0] = o; else g_hwm_tex_addr[1] = o; }
        (void)dst; (void)pitch; (void)height;
        return;
    }
    if (got) {
        uint32_t fw = last.details.video.width, fh = last.details.video.height;
        if (fw && fh && fw <= 1920 && fh <= 1088 && last.pData) {
            if (!s_last || fw != s_w || fh != s_h_px) { free(s_last); s_last = malloc(fw * fh * 3 / 2); s_w = fw; s_h_px = fh; }
            if (s_last) { memcpy(s_last, last.pData, fw * fh * 3 / 2); s_have = 1; s_pic++; }
        }
    }
    if (!s_have || !s_last) return;
    k = (g_hwm_tex_addr[0] == o) ? 0 : (g_hwm_tex_addr[1] == o) ? 1 : (!g_hwm_tex_addr[0] ? 0 : !g_hwm_tex_addr[1] ? 1 : (int)(s_pic & 1));
    if (g_hwm_tex_addr[k] == o && s_tex_pic[k] == s_pic) return;   /* (it has this picture already) */
    w = s_w; h = s_h_px;
    if (h > height) h = height;
    if (w > pitch / 2) w = pitch / 2;
    s_frames_out++;
    /* (v138) the picture as the renderer keeps it (ARGB), for this texture */
    if (!g_hwm_argb[k] || g_hwm_argb_w != w || g_hwm_argb_h != h) {
        free(g_hwm_argb[k]); g_hwm_argb[k] = (uint32_t *)malloc((size_t)w * h * 4);
        g_hwm_argb_w = w; g_hwm_argb_h = h;
    }
    if (g_hwm_argb[k]) {
        static uint8_t clip[1024]; static int ci;
        uint32_t *out = g_hwm_argb[k];
        if (!ci) { int i; for (i = 0; i < 1024; i++) { int v = i - 384; clip[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); } ci = 1; }
        for (y = 0; y < h; y++) {
            const uint8_t *Y = s_last + y * s_w, *UV = s_last + s_w * s_h_px + (y >> 1) * s_w;
            uint32_t *op = out + y * w;
            for (x = 0; x + 1 < w; x += 2) {
                int cu = (int)UV[0] - 128, cv = (int)UV[1] - 128;
                int rv = 409 * cv + 128, gv = -100 * cu - 208 * cv + 128, bv = 516 * cu + 128;
                int c0 = 298 * ((int)Y[0] - 16), c1 = 298 * ((int)Y[1] - 16);
                op[0] = 0xFF000000u | ((uint32_t)clip[((c0 + rv) >> 8) + 384] << 16) | ((uint32_t)clip[((c0 + gv) >> 8) + 384] << 8) | clip[((c0 + bv) >> 8) + 384];
                op[1] = 0xFF000000u | ((uint32_t)clip[((c1 + rv) >> 8) + 384] << 16) | ((uint32_t)clip[((c1 + gv) >> 8) + 384] << 8) | clip[((c1 + bv) >> 8) + 384];
                op += 2; Y += 2; UV += 2;
            }
        }
    }
    (void)dst;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    g_hwm_tex_addr[k] = o; s_tex_pic[k] = s_pic;
    g_hwm_tex_ver[k]++;
}

void hwm_close(void)
{
    if (!s_on) return;
    s_on = 0;
    g_hwm_frame = NULL; g_hwm_rgba = NULL; g_hwm_show = -1; g_hwm_req_w = 0;   /* (the renderer stops using the film's pictures) */
    sceKernelDelayThread(50000);   /* (the frame being drawn finishes before the player lets its memory go) */
    s_audio_run = 0;
    if (s_audio_thid >= 0) { sceKernelWaitThreadEnd(s_audio_thid, NULL, NULL); sceKernelDeleteThread(s_audio_thid); s_audio_thid = -1; }
    g_hwm_tex_addr[0] = g_hwm_tex_addr[1] = 0;   /* (the renderer checks those textures as before) */
    { extern void vglFree(void *); int b; for (b = 0; b < 2; b++) if (s_rgba_buf[b]) { vglFree(s_rgba_buf[b]); s_rgba_buf[b] = NULL; } s_rgba_w = s_rgba_h = 0; }
    sceAvPlayerStop(s_h);
    sceAvPlayerClose(s_h);
    s_h = 0;
    if (s_port >= 0) { sceAudioOutReleasePort(s_port); s_port = -1; }
    tt_dbg("  [HWMOVIE] closed after %.1f s: %u pictures from the decoder, %u given to the game\n",
           (sceKernelGetProcessTimeWide() - s_t0) / 1e6, s_frames_in, s_frames_out);
}
#endif
#if defined(__vita__)
/* (v136) a little room: vita-elf-create needs space after the code segment for its own data */
__attribute__((used)) static const char s_seg_pad[16384] = { 1 };
#endif
