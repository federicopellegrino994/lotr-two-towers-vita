void __real_glActiveTexture(GLenum texture);
void __real_glAttachShader(GLuint prog, GLuint shad);
void __real_glBindAttribLocation(GLuint program, GLuint index, const GLchar * name);
void __real_glBindBuffer(GLenum target, GLuint buffer);
void __real_glBindFramebuffer(GLenum target, GLuint framebuffer);
void __real_glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void __real_glBindSampler(GLuint unit, GLuint smp);
void __real_glBindTexture(GLenum target, GLuint texture);
void __real_glBindVertexArray(GLuint array);
void __real_glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void __real_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void __real_glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
void __real_glBufferData(GLenum target, GLsizei size, const GLvoid * data, GLenum usage);
GLenum __real_glCheckFramebufferStatus(GLenum target);
void __real_glClear(GLbitfield mask);
void __real_glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void __real_glClearDepth(GLdouble depth);
void __real_glClearStencil(GLint s);
void __real_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void __real_glCompileShader(GLuint shader);
GLuint __real_glCreateProgram(void);
GLuint __real_glCreateShader(GLenum shaderType);
void __real_glCullFace(GLenum mode);
void __real_glDeleteFramebuffers(GLsizei n, const GLuint * framebuffers);
void __real_glDeleteProgram(GLuint prog);
void __real_glDeleteRenderbuffers(GLsizei n, const GLuint * renderbuffers);
void __real_glDeleteShader(GLuint shad);
void __real_glDeleteTextures(GLsizei n, const GLuint * textures);
void __real_glDepthFunc(GLenum func);
void __real_glDepthMask(GLboolean flag);
void __real_glDisable(GLenum cap);
void __real_glDisableVertexAttribArray(GLuint index);
void __real_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void __real_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid * indices);
void __real_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void * indices);
void __real_glEnable(GLenum cap);
void __real_glEnableVertexAttribArray(GLuint index);
void __real_glFinish(void);
void __real_glFlush(void);
void __real_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void __real_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void __real_glFrontFace(GLenum mode);
void __real_glGenBuffers(GLsizei n, GLuint * buffers);
void __real_glGenFramebuffers(GLsizei n, GLuint * framebuffers);
void __real_glGenRenderbuffers(GLsizei n, GLuint * renderbuffers);
void __real_glGenSamplers(GLsizei n, GLuint * smps);
void __real_glGenTextures(GLsizei n, GLuint * textures);
void __real_glGenVertexArrays(GLsizei n, GLuint * res);
void __real_glGenerateMipmap(GLenum target);
GLenum __real_glGetError(void);
void __real_glGetIntegerv(GLenum pname, GLint * data);
void __real_glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei * length, GLenum * binaryFormat, void * binary);
void __real_glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei * length, GLchar * infoLog);
void __real_glGetProgramiv(GLuint program, GLenum pname, GLint * params);
void __real_glGetShaderInfoLog(GLuint handle, GLsizei maxLength, GLsizei * length, GLchar * infoLog);
void __real_glGetShaderiv(GLuint handle, GLenum pname, GLint * params);
const GLubyte * __real_glGetString(GLenum name);
GLuint __real_glGetUniformBlockIndex(GLuint prog, const GLchar * uniformBlockName);
GLint __real_glGetUniformLocation(GLuint prog, const GLchar * name);
void __real_glLinkProgram(GLuint progr);
void * __real_glMapBuffer(GLenum target, GLbitfield access);
void __real_glPixelStorei(GLenum pname, GLint param);
void __real_glReadBuffer(GLenum mode);
void __real_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid * data);
void __real_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void __real_glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
void __real_glSamplerParameteri(GLuint target, GLenum pname, GLint param);
void __real_glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void __real_glShaderBinary(GLsizei count, const GLuint * handles, GLenum binaryFormat, const void * binary, GLsizei length);
void __real_glShaderSource(GLuint handle, GLsizei count, const GLchar *const * string, const GLint * length);
void __real_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void __real_glStencilMask(GLuint mask);
void __real_glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
void __real_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * data);
void __real_glTexParameteri(GLenum target, GLenum pname, GLint param);
void __real_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid * pixels);
void __real_glUniform1f(GLint location, GLfloat v0);
void __real_glUniform1i(GLint location, GLint v0);
void __real_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void __real_glUniform4fv(GLint location, GLsizei count, const GLfloat * value);
GLboolean __real_glUnmapBuffer(GLenum target);
void __real_glUseProgram(GLuint program);
void __real_glVertexAttrib4f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void __real_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void * pointer);
void __real_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void * __real_vglAlloc(uint32_t size, vglMemType type);
void __real_vglFree(void * addr);
SceGxmTexture * __real_vglGetGxmTexture(GLenum target);
void * __real_vglGetTexDataPointer(GLenum target);
void * __real_vglMalloc(uint32_t size);
size_t __real_vglMemFree(vglMemType type);
void __real_vglOverloadTexDataPointer(GLenum target, void * data);
void __real_vglSwapBuffers(GLboolean has_commondialog);
void __real_vglWaitVblankStart(GLboolean enable);
uint32_t __real_vglGetFrameNumber(void);

/* GENERATED by tools/glt/gen_glt.py -- do not edit: vgl_glt.c includes it */
static void glt_t_glActiveTexture(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glActiveTexture((GLenum)a[0]);
}
void __wrap_glActiveTexture(GLenum texture)
{
    if (glt_direct()) { __real_glActiveTexture(texture); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)texture;
        glt_post(glt_t_glActiveTexture, w, 1, NULL, 0, "glActiveTexture");
    }
}
static void glt_t_glAttachShader(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glAttachShader((GLuint)a[0], (GLuint)a[1]);
}
void __wrap_glAttachShader(GLuint prog, GLuint shad)
{
    if (glt_direct()) { __real_glAttachShader(prog, shad); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)prog;
        w[1] = (uint32_t)shad;
        uint32_t r = glt_sync(glt_t_glAttachShader, w, 2, "glAttachShader");
        (void)r;
    }
}
static void glt_t_glBindAttribLocation(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBindAttribLocation((GLuint)a[0], (GLuint)a[1], (const GLchar *)(uintptr_t)a[2]);
}
void __wrap_glBindAttribLocation(GLuint program, GLuint index, const GLchar * name)
{
    if (glt_direct()) { __real_glBindAttribLocation(program, index, name); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)program;
        w[1] = (uint32_t)index;
        w[2] = (uint32_t)(uintptr_t)name;
        uint32_t r = glt_sync(glt_t_glBindAttribLocation, w, 3, "glBindAttribLocation");
        (void)r;
    }
}
static void glt_t_glBindFramebuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBindFramebuffer((GLenum)a[0], (GLuint)a[1]);
}
void __wrap_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (glt_direct()) { __real_glBindFramebuffer(target, framebuffer); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)framebuffer;
        glt_post(glt_t_glBindFramebuffer, w, 2, NULL, 0, "glBindFramebuffer");
    }
}
static void glt_t_glBindRenderbuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBindRenderbuffer((GLenum)a[0], (GLuint)a[1]);
}
void __wrap_glBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
    if (glt_direct()) { __real_glBindRenderbuffer(target, renderbuffer); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)renderbuffer;
        glt_post(glt_t_glBindRenderbuffer, w, 2, NULL, 0, "glBindRenderbuffer");
    }
}
static void glt_t_glBindSampler(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBindSampler((GLuint)a[0], (GLuint)a[1]);
}
void __wrap_glBindSampler(GLuint unit, GLuint smp)
{
    if (glt_direct()) { __real_glBindSampler(unit, smp); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)unit;
        w[1] = (uint32_t)smp;
        glt_post(glt_t_glBindSampler, w, 2, NULL, 0, "glBindSampler");
    }
}
static void glt_t_glBindTexture(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBindTexture((GLenum)a[0], (GLuint)a[1]);
}
void __wrap_glBindTexture(GLenum target, GLuint texture)
{
    if (glt_direct()) { __real_glBindTexture(target, texture); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)texture;
        glt_post(glt_t_glBindTexture, w, 2, NULL, 0, "glBindTexture");
    }
}
static void glt_t_glBlendEquationSeparate(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBlendEquationSeparate((GLenum)a[0], (GLenum)a[1]);
}
void __wrap_glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
    if (glt_direct()) { __real_glBlendEquationSeparate(modeRGB, modeAlpha); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)modeRGB;
        w[1] = (uint32_t)modeAlpha;
        glt_post(glt_t_glBlendEquationSeparate, w, 2, NULL, 0, "glBlendEquationSeparate");
    }
}
static void glt_t_glBlendFuncSeparate(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBlendFuncSeparate((GLenum)a[0], (GLenum)a[1], (GLenum)a[2], (GLenum)a[3]);
}
void __wrap_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    if (glt_direct()) { __real_glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)srcRGB;
        w[1] = (uint32_t)dstRGB;
        w[2] = (uint32_t)srcAlpha;
        w[3] = (uint32_t)dstAlpha;
        glt_post(glt_t_glBlendFuncSeparate, w, 4, NULL, 0, "glBlendFuncSeparate");
    }
}
static void glt_t_glBlitFramebuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBlitFramebuffer((GLint)a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLint)a[4], (GLint)a[5], (GLint)a[6], (GLint)a[7], (GLbitfield)a[8], (GLenum)a[9]);
}
void __wrap_glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{
    if (glt_direct()) { __real_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter); return; }
    {
        uint32_t w[10];
        w[0] = (uint32_t)srcX0;
        w[1] = (uint32_t)srcY0;
        w[2] = (uint32_t)srcX1;
        w[3] = (uint32_t)srcY1;
        w[4] = (uint32_t)dstX0;
        w[5] = (uint32_t)dstY0;
        w[6] = (uint32_t)dstX1;
        w[7] = (uint32_t)dstY1;
        w[8] = (uint32_t)mask;
        w[9] = (uint32_t)filter;
        glt_post(glt_t_glBlitFramebuffer, w, 10, NULL, 0, "glBlitFramebuffer");
    }
}
static void glt_t_glBufferData(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glBufferData((GLenum)a[0], (GLsizei)a[1], (const GLvoid *)(uintptr_t)a[2], (GLenum)a[3]);
}
void __wrap_glBufferData(GLenum target, GLsizei size, const GLvoid * data, GLenum usage)
{
    if (glt_direct()) { __real_glBufferData(target, size, data, usage); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)size;
        w[2] = (uint32_t)(uintptr_t)data;
        w[3] = (uint32_t)usage;
        uint32_t r = glt_sync(glt_t_glBufferData, w, 4, "glBufferData");
        (void)r;
    }
}
static void glt_t_glCheckFramebufferStatus(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glCheckFramebufferStatus((GLenum)a[0]);
}
GLenum __wrap_glCheckFramebufferStatus(GLenum target)
{
    if (glt_direct()) return __real_glCheckFramebufferStatus(target);
    {
        uint32_t w[1];
        w[0] = (uint32_t)target;
        uint32_t r = glt_sync(glt_t_glCheckFramebufferStatus, w, 1, "glCheckFramebufferStatus");
        return (GLenum)(uintptr_t)r;
    }
}
static void glt_t_glClear(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glClear((GLbitfield)a[0]);
}
void __wrap_glClear(GLbitfield mask)
{
    if (glt_direct()) { __real_glClear(mask); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)mask;
        glt_post(glt_t_glClear, w, 1, NULL, 0, "glClear");
    }
}
static void glt_t_glClearColor(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glClearColor(glt_u2f(a[0]), glt_u2f(a[1]), glt_u2f(a[2]), glt_u2f(a[3]));
}
void __wrap_glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    if (glt_direct()) { __real_glClearColor(red, green, blue, alpha); return; }
    {
        uint32_t w[4];
        w[0] = glt_f2u(red);
        w[1] = glt_f2u(green);
        w[2] = glt_f2u(blue);
        w[3] = glt_f2u(alpha);
        glt_post(glt_t_glClearColor, w, 4, NULL, 0, "glClearColor");
    }
}
static void glt_t_glClearDepth(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glClearDepth(({ union { double d; uint32_t u[2]; } c; c.u[0] = a[0]; c.u[1] = a[1]; c.d; }));
}
void __wrap_glClearDepth(GLdouble depth)
{
    if (glt_direct()) { __real_glClearDepth(depth); return; }
    {
        uint32_t w[2];
        { union { double d; uint32_t u[2]; } c; c.d = depth; w[0] = c.u[0]; w[1] = c.u[1]; }
        glt_post(glt_t_glClearDepth, w, 2, NULL, 0, "glClearDepth");
    }
}
static void glt_t_glClearStencil(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glClearStencil((GLint)a[0]);
}
void __wrap_glClearStencil(GLint s)
{
    if (glt_direct()) { __real_glClearStencil(s); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)s;
        glt_post(glt_t_glClearStencil, w, 1, NULL, 0, "glClearStencil");
    }
}
static void glt_t_glColorMask(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glColorMask((GLboolean)a[0], (GLboolean)a[1], (GLboolean)a[2], (GLboolean)a[3]);
}
void __wrap_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    if (glt_direct()) { __real_glColorMask(red, green, blue, alpha); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)red;
        w[1] = (uint32_t)green;
        w[2] = (uint32_t)blue;
        w[3] = (uint32_t)alpha;
        glt_post(glt_t_glColorMask, w, 4, NULL, 0, "glColorMask");
    }
}
static void glt_t_glCompileShader(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glCompileShader((GLuint)a[0]);
}
void __wrap_glCompileShader(GLuint shader)
{
    if (glt_direct()) { __real_glCompileShader(shader); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)shader;
        uint32_t r = glt_sync(glt_t_glCompileShader, w, 1, "glCompileShader");
        (void)r;
    }
}
static void glt_t_glCreateProgram(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glCreateProgram();
}
GLuint __wrap_glCreateProgram(void)
{
    if (glt_direct()) return __real_glCreateProgram();
    {
        uint32_t w[1];
        w[0] = 0;
        uint32_t r = glt_sync(glt_t_glCreateProgram, w, 0, "glCreateProgram");
        return (GLuint)(uintptr_t)r;
    }
}
static void glt_t_glCreateShader(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glCreateShader((GLenum)a[0]);
}
GLuint __wrap_glCreateShader(GLenum shaderType)
{
    if (glt_direct()) return __real_glCreateShader(shaderType);
    {
        uint32_t w[1];
        w[0] = (uint32_t)shaderType;
        uint32_t r = glt_sync(glt_t_glCreateShader, w, 1, "glCreateShader");
        return (GLuint)(uintptr_t)r;
    }
}
static void glt_t_glCullFace(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glCullFace((GLenum)a[0]);
}
void __wrap_glCullFace(GLenum mode)
{
    if (glt_direct()) { __real_glCullFace(mode); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)mode;
        glt_post(glt_t_glCullFace, w, 1, NULL, 0, "glCullFace");
    }
}
static void glt_t_glDeleteFramebuffers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDeleteFramebuffers((GLsizei)a[0], (const GLuint *)(void *)pl);
}
void __wrap_glDeleteFramebuffers(GLsizei n, const GLuint * framebuffers)
{
    if (glt_direct()) { __real_glDeleteFramebuffers(n, framebuffers); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)framebuffers;
        glt_post(glt_t_glDeleteFramebuffers, w, 2, framebuffers, (uint32_t)n * 4u, "glDeleteFramebuffers");
    }
}
static void glt_t_glDeleteProgram(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDeleteProgram((GLuint)a[0]);
}
void __wrap_glDeleteProgram(GLuint prog)
{
    if (glt_direct()) { __real_glDeleteProgram(prog); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)prog;
        glt_post(glt_t_glDeleteProgram, w, 1, NULL, 0, "glDeleteProgram");
    }
}
static void glt_t_glDeleteRenderbuffers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDeleteRenderbuffers((GLsizei)a[0], (const GLuint *)(void *)pl);
}
void __wrap_glDeleteRenderbuffers(GLsizei n, const GLuint * renderbuffers)
{
    if (glt_direct()) { __real_glDeleteRenderbuffers(n, renderbuffers); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)renderbuffers;
        glt_post(glt_t_glDeleteRenderbuffers, w, 2, renderbuffers, (uint32_t)n * 4u, "glDeleteRenderbuffers");
    }
}
static void glt_t_glDeleteShader(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDeleteShader((GLuint)a[0]);
}
void __wrap_glDeleteShader(GLuint shad)
{
    if (glt_direct()) { __real_glDeleteShader(shad); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)shad;
        glt_post(glt_t_glDeleteShader, w, 1, NULL, 0, "glDeleteShader");
    }
}
static void glt_t_glDeleteTextures(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDeleteTextures((GLsizei)a[0], (const GLuint *)(void *)pl);
}
void __wrap_glDeleteTextures(GLsizei n, const GLuint * textures)
{
    if (glt_direct()) { __real_glDeleteTextures(n, textures); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)textures;
        glt_post(glt_t_glDeleteTextures, w, 2, textures, (uint32_t)n * 4u, "glDeleteTextures");
    }
}
static void glt_t_glDepthFunc(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDepthFunc((GLenum)a[0]);
}
void __wrap_glDepthFunc(GLenum func)
{
    if (glt_direct()) { __real_glDepthFunc(func); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)func;
        glt_post(glt_t_glDepthFunc, w, 1, NULL, 0, "glDepthFunc");
    }
}
static void glt_t_glDepthMask(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDepthMask((GLboolean)a[0]);
}
void __wrap_glDepthMask(GLboolean flag)
{
    if (glt_direct()) { __real_glDepthMask(flag); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)flag;
        glt_post(glt_t_glDepthMask, w, 1, NULL, 0, "glDepthMask");
    }
}
static void glt_t_glDisable(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glDisable((GLenum)a[0]);
}
void __wrap_glDisable(GLenum cap)
{
    if (glt_direct()) { __real_glDisable(cap); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)cap;
        glt_post(glt_t_glDisable, w, 1, NULL, 0, "glDisable");
    }
}
static void glt_t_glEnable(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glEnable((GLenum)a[0]);
}
void __wrap_glEnable(GLenum cap)
{
    if (glt_direct()) { __real_glEnable(cap); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)cap;
        glt_post(glt_t_glEnable, w, 1, NULL, 0, "glEnable");
    }
}
static void glt_t_glFinish(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glFinish();
}
void __wrap_glFinish(void)
{
    if (glt_direct()) { __real_glFinish(); return; }
    {
        uint32_t w[1];
        w[0] = 0;
        uint32_t r = glt_sync(glt_t_glFinish, w, 0, "glFinish");
        (void)r;
    }
}
static void glt_t_glFlush(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glFlush();
}
void __wrap_glFlush(void)
{
    if (glt_direct()) { __real_glFlush(); return; }
    {
        uint32_t w[1];
        w[0] = 0;
        glt_post(glt_t_glFlush, w, 0, NULL, 0, "glFlush");
    }
}
static void glt_t_glFramebufferRenderbuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glFramebufferRenderbuffer((GLenum)a[0], (GLenum)a[1], (GLenum)a[2], (GLuint)a[3]);
}
void __wrap_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
    if (glt_direct()) { __real_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)attachment;
        w[2] = (uint32_t)renderbuffertarget;
        w[3] = (uint32_t)renderbuffer;
        glt_post(glt_t_glFramebufferRenderbuffer, w, 4, NULL, 0, "glFramebufferRenderbuffer");
    }
}
static void glt_t_glFramebufferTexture2D(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glFramebufferTexture2D((GLenum)a[0], (GLenum)a[1], (GLenum)a[2], (GLuint)a[3], (GLint)a[4]);
}
void __wrap_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    if (glt_direct()) { __real_glFramebufferTexture2D(target, attachment, textarget, texture, level); return; }
    {
        uint32_t w[5];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)attachment;
        w[2] = (uint32_t)textarget;
        w[3] = (uint32_t)texture;
        w[4] = (uint32_t)level;
        glt_post(glt_t_glFramebufferTexture2D, w, 5, NULL, 0, "glFramebufferTexture2D");
    }
}
static void glt_t_glFrontFace(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glFrontFace((GLenum)a[0]);
}
void __wrap_glFrontFace(GLenum mode)
{
    if (glt_direct()) { __real_glFrontFace(mode); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)mode;
        glt_post(glt_t_glFrontFace, w, 1, NULL, 0, "glFrontFace");
    }
}
static void glt_t_glGenBuffers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenBuffers((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenBuffers(GLsizei n, GLuint * buffers)
{
    if (glt_direct()) { __real_glGenBuffers(n, buffers); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)buffers;
        uint32_t r = glt_sync(glt_t_glGenBuffers, w, 2, "glGenBuffers");
        (void)r;
    }
}
static void glt_t_glGenFramebuffers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenFramebuffers((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenFramebuffers(GLsizei n, GLuint * framebuffers)
{
    if (glt_direct()) { __real_glGenFramebuffers(n, framebuffers); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)framebuffers;
        uint32_t r = glt_sync(glt_t_glGenFramebuffers, w, 2, "glGenFramebuffers");
        (void)r;
    }
}
static void glt_t_glGenRenderbuffers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenRenderbuffers((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenRenderbuffers(GLsizei n, GLuint * renderbuffers)
{
    if (glt_direct()) { __real_glGenRenderbuffers(n, renderbuffers); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)renderbuffers;
        uint32_t r = glt_sync(glt_t_glGenRenderbuffers, w, 2, "glGenRenderbuffers");
        (void)r;
    }
}
static void glt_t_glGenSamplers(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenSamplers((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenSamplers(GLsizei n, GLuint * smps)
{
    if (glt_direct()) { __real_glGenSamplers(n, smps); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)smps;
        uint32_t r = glt_sync(glt_t_glGenSamplers, w, 2, "glGenSamplers");
        (void)r;
    }
}
static void glt_t_glGenTextures(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenTextures((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenTextures(GLsizei n, GLuint * textures)
{
    if (glt_direct()) { __real_glGenTextures(n, textures); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)textures;
        uint32_t r = glt_sync(glt_t_glGenTextures, w, 2, "glGenTextures");
        (void)r;
    }
}
static void glt_t_glGenVertexArrays(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenVertexArrays((GLsizei)a[0], (GLuint *)(uintptr_t)a[1]);
}
void __wrap_glGenVertexArrays(GLsizei n, GLuint * res)
{
    if (glt_direct()) { __real_glGenVertexArrays(n, res); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)n;
        w[1] = (uint32_t)(uintptr_t)res;
        uint32_t r = glt_sync(glt_t_glGenVertexArrays, w, 2, "glGenVertexArrays");
        (void)r;
    }
}
static void glt_t_glGenerateMipmap(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGenerateMipmap((GLenum)a[0]);
}
void __wrap_glGenerateMipmap(GLenum target)
{
    if (glt_direct()) { __real_glGenerateMipmap(target); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)target;
        glt_post(glt_t_glGenerateMipmap, w, 1, NULL, 0, "glGenerateMipmap");
    }
}
static void glt_t_glGetError(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glGetError();
}
GLenum __wrap_glGetError(void)
{
    if (glt_direct()) return __real_glGetError();
    {
        uint32_t w[1];
        w[0] = 0;
        uint32_t r = glt_sync(glt_t_glGetError, w, 0, "glGetError");
        return (GLenum)(uintptr_t)r;
    }
}
static void glt_t_glGetProgramBinary(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGetProgramBinary((GLuint)a[0], (GLsizei)a[1], (GLsizei *)(uintptr_t)a[2], (GLenum *)(uintptr_t)a[3], (void *)(uintptr_t)a[4]);
}
void __wrap_glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei * length, GLenum * binaryFormat, void * binary)
{
    if (glt_direct()) { __real_glGetProgramBinary(program, bufSize, length, binaryFormat, binary); return; }
    {
        uint32_t w[5];
        w[0] = (uint32_t)program;
        w[1] = (uint32_t)bufSize;
        w[2] = (uint32_t)(uintptr_t)length;
        w[3] = (uint32_t)(uintptr_t)binaryFormat;
        w[4] = (uint32_t)(uintptr_t)binary;
        uint32_t r = glt_sync(glt_t_glGetProgramBinary, w, 5, "glGetProgramBinary");
        (void)r;
    }
}
static void glt_t_glGetProgramInfoLog(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGetProgramInfoLog((GLuint)a[0], (GLsizei)a[1], (GLsizei *)(uintptr_t)a[2], (GLchar *)(uintptr_t)a[3]);
}
void __wrap_glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei * length, GLchar * infoLog)
{
    if (glt_direct()) { __real_glGetProgramInfoLog(program, maxLength, length, infoLog); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)program;
        w[1] = (uint32_t)maxLength;
        w[2] = (uint32_t)(uintptr_t)length;
        w[3] = (uint32_t)(uintptr_t)infoLog;
        uint32_t r = glt_sync(glt_t_glGetProgramInfoLog, w, 4, "glGetProgramInfoLog");
        (void)r;
    }
}
static void glt_t_glGetProgramiv(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGetProgramiv((GLuint)a[0], (GLenum)a[1], (GLint *)(uintptr_t)a[2]);
}
void __wrap_glGetProgramiv(GLuint program, GLenum pname, GLint * params)
{
    if (glt_direct()) { __real_glGetProgramiv(program, pname, params); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)program;
        w[1] = (uint32_t)pname;
        w[2] = (uint32_t)(uintptr_t)params;
        uint32_t r = glt_sync(glt_t_glGetProgramiv, w, 3, "glGetProgramiv");
        (void)r;
    }
}
static void glt_t_glGetShaderInfoLog(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGetShaderInfoLog((GLuint)a[0], (GLsizei)a[1], (GLsizei *)(uintptr_t)a[2], (GLchar *)(uintptr_t)a[3]);
}
void __wrap_glGetShaderInfoLog(GLuint handle, GLsizei maxLength, GLsizei * length, GLchar * infoLog)
{
    if (glt_direct()) { __real_glGetShaderInfoLog(handle, maxLength, length, infoLog); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)handle;
        w[1] = (uint32_t)maxLength;
        w[2] = (uint32_t)(uintptr_t)length;
        w[3] = (uint32_t)(uintptr_t)infoLog;
        uint32_t r = glt_sync(glt_t_glGetShaderInfoLog, w, 4, "glGetShaderInfoLog");
        (void)r;
    }
}
static void glt_t_glGetShaderiv(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glGetShaderiv((GLuint)a[0], (GLenum)a[1], (GLint *)(uintptr_t)a[2]);
}
void __wrap_glGetShaderiv(GLuint handle, GLenum pname, GLint * params)
{
    if (glt_direct()) { __real_glGetShaderiv(handle, pname, params); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)handle;
        w[1] = (uint32_t)pname;
        w[2] = (uint32_t)(uintptr_t)params;
        uint32_t r = glt_sync(glt_t_glGetShaderiv, w, 3, "glGetShaderiv");
        (void)r;
    }
}
static void glt_t_glGetString(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glGetString((GLenum)a[0]);
}
const GLubyte * __wrap_glGetString(GLenum name)
{
    if (glt_direct()) return __real_glGetString(name);
    {
        uint32_t w[1];
        w[0] = (uint32_t)name;
        uint32_t r = glt_sync(glt_t_glGetString, w, 1, "glGetString");
        return (const GLubyte *)(uintptr_t)r;
    }
}
static void glt_t_glGetUniformBlockIndex(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glGetUniformBlockIndex((GLuint)a[0], (const GLchar *)(uintptr_t)a[1]);
}
GLuint __wrap_glGetUniformBlockIndex(GLuint prog, const GLchar * uniformBlockName)
{
    if (glt_direct()) return __real_glGetUniformBlockIndex(prog, uniformBlockName);
    {
        uint32_t w[2];
        w[0] = (uint32_t)prog;
        w[1] = (uint32_t)(uintptr_t)uniformBlockName;
        uint32_t r = glt_sync(glt_t_glGetUniformBlockIndex, w, 2, "glGetUniformBlockIndex");
        return (GLuint)(uintptr_t)r;
    }
}
static void glt_t_glGetUniformLocation(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glGetUniformLocation((GLuint)a[0], (const GLchar *)(uintptr_t)a[1]);
}
GLint __wrap_glGetUniformLocation(GLuint prog, const GLchar * name)
{
    if (glt_direct()) return __real_glGetUniformLocation(prog, name);
    {
        uint32_t w[2];
        w[0] = (uint32_t)prog;
        w[1] = (uint32_t)(uintptr_t)name;
        uint32_t r = glt_sync(glt_t_glGetUniformLocation, w, 2, "glGetUniformLocation");
        return (GLint)(uintptr_t)r;
    }
}
static void glt_t_glLinkProgram(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glLinkProgram((GLuint)a[0]);
}
void __wrap_glLinkProgram(GLuint progr)
{
    if (glt_direct()) { __real_glLinkProgram(progr); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)progr;
        uint32_t r = glt_sync(glt_t_glLinkProgram, w, 1, "glLinkProgram");
        (void)r;
    }
}
static void glt_t_glMapBuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glMapBuffer((GLenum)a[0], (GLbitfield)a[1]);
}
void * __wrap_glMapBuffer(GLenum target, GLbitfield access)
{
    if (glt_direct()) return __real_glMapBuffer(target, access);
    {
        uint32_t w[2];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)access;
        uint32_t r = glt_sync(glt_t_glMapBuffer, w, 2, "glMapBuffer");
        return (void *)(uintptr_t)r;
    }
}
static void glt_t_glPixelStorei(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glPixelStorei((GLenum)a[0], (GLint)a[1]);
}
void __wrap_glPixelStorei(GLenum pname, GLint param)
{
    if (glt_direct()) { __real_glPixelStorei(pname, param); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)pname;
        w[1] = (uint32_t)param;
        glt_post(glt_t_glPixelStorei, w, 2, NULL, 0, "glPixelStorei");
    }
}
static void glt_t_glReadBuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glReadBuffer((GLenum)a[0]);
}
void __wrap_glReadBuffer(GLenum mode)
{
    if (glt_direct()) { __real_glReadBuffer(mode); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)mode;
        glt_post(glt_t_glReadBuffer, w, 1, NULL, 0, "glReadBuffer");
    }
}
static void glt_t_glReadPixels(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glReadPixels((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3], (GLenum)a[4], (GLenum)a[5], (GLvoid *)(uintptr_t)a[6]);
}
void __wrap_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid * data)
{
    if (glt_direct()) { __real_glReadPixels(x, y, width, height, format, type, data); return; }
    {
        uint32_t w[7];
        w[0] = (uint32_t)x;
        w[1] = (uint32_t)y;
        w[2] = (uint32_t)width;
        w[3] = (uint32_t)height;
        w[4] = (uint32_t)format;
        w[5] = (uint32_t)type;
        w[6] = (uint32_t)(uintptr_t)data;
        uint32_t r = glt_sync(glt_t_glReadPixels, w, 7, "glReadPixels");
        (void)r;
    }
}
static void glt_t_glRenderbufferStorage(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glRenderbufferStorage((GLenum)a[0], (GLenum)a[1], (GLsizei)a[2], (GLsizei)a[3]);
}
void __wrap_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    if (glt_direct()) { __real_glRenderbufferStorage(target, internalformat, width, height); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)internalformat;
        w[2] = (uint32_t)width;
        w[3] = (uint32_t)height;
        glt_post(glt_t_glRenderbufferStorage, w, 4, NULL, 0, "glRenderbufferStorage");
    }
}
static void glt_t_glSamplerParameterf(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glSamplerParameterf((GLuint)a[0], (GLenum)a[1], glt_u2f(a[2]));
}
void __wrap_glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param)
{
    if (glt_direct()) { __real_glSamplerParameterf(sampler, pname, param); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)sampler;
        w[1] = (uint32_t)pname;
        w[2] = glt_f2u(param);
        glt_post(glt_t_glSamplerParameterf, w, 3, NULL, 0, "glSamplerParameterf");
    }
}
static void glt_t_glSamplerParameteri(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glSamplerParameteri((GLuint)a[0], (GLenum)a[1], (GLint)a[2]);
}
void __wrap_glSamplerParameteri(GLuint target, GLenum pname, GLint param)
{
    if (glt_direct()) { __real_glSamplerParameteri(target, pname, param); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)pname;
        w[2] = (uint32_t)param;
        glt_post(glt_t_glSamplerParameteri, w, 3, NULL, 0, "glSamplerParameteri");
    }
}
static void glt_t_glScissor(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glScissor((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]);
}
void __wrap_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (glt_direct()) { __real_glScissor(x, y, width, height); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)x;
        w[1] = (uint32_t)y;
        w[2] = (uint32_t)width;
        w[3] = (uint32_t)height;
        glt_post(glt_t_glScissor, w, 4, NULL, 0, "glScissor");
    }
}
static void glt_t_glShaderBinary(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glShaderBinary((GLsizei)a[0], (const GLuint *)(uintptr_t)a[1], (GLenum)a[2], (const void *)(uintptr_t)a[3], (GLsizei)a[4]);
}
void __wrap_glShaderBinary(GLsizei count, const GLuint * handles, GLenum binaryFormat, const void * binary, GLsizei length)
{
    if (glt_direct()) { __real_glShaderBinary(count, handles, binaryFormat, binary, length); return; }
    {
        uint32_t w[5];
        w[0] = (uint32_t)count;
        w[1] = (uint32_t)(uintptr_t)handles;
        w[2] = (uint32_t)binaryFormat;
        w[3] = (uint32_t)(uintptr_t)binary;
        w[4] = (uint32_t)length;
        uint32_t r = glt_sync(glt_t_glShaderBinary, w, 5, "glShaderBinary");
        (void)r;
    }
}
static void glt_t_glShaderSource(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glShaderSource((GLuint)a[0], (GLsizei)a[1], (const GLchar *const *)(uintptr_t)a[2], (const GLint *)(uintptr_t)a[3]);
}
void __wrap_glShaderSource(GLuint handle, GLsizei count, const GLchar *const * string, const GLint * length)
{
    if (glt_direct()) { __real_glShaderSource(handle, count, string, length); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)handle;
        w[1] = (uint32_t)count;
        w[2] = (uint32_t)(uintptr_t)string;
        w[3] = (uint32_t)(uintptr_t)length;
        uint32_t r = glt_sync(glt_t_glShaderSource, w, 4, "glShaderSource");
        (void)r;
    }
}
static void glt_t_glStencilFuncSeparate(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glStencilFuncSeparate((GLenum)a[0], (GLenum)a[1], (GLint)a[2], (GLuint)a[3]);
}
void __wrap_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    if (glt_direct()) { __real_glStencilFuncSeparate(face, func, ref, mask); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)face;
        w[1] = (uint32_t)func;
        w[2] = (uint32_t)ref;
        w[3] = (uint32_t)mask;
        glt_post(glt_t_glStencilFuncSeparate, w, 4, NULL, 0, "glStencilFuncSeparate");
    }
}
static void glt_t_glStencilMask(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glStencilMask((GLuint)a[0]);
}
void __wrap_glStencilMask(GLuint mask)
{
    if (glt_direct()) { __real_glStencilMask(mask); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)mask;
        glt_post(glt_t_glStencilMask, w, 1, NULL, 0, "glStencilMask");
    }
}
static void glt_t_glStencilOpSeparate(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glStencilOpSeparate((GLenum)a[0], (GLenum)a[1], (GLenum)a[2], (GLenum)a[3]);
}
void __wrap_glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)
{
    if (glt_direct()) { __real_glStencilOpSeparate(face, sfail, dpfail, dppass); return; }
    {
        uint32_t w[4];
        w[0] = (uint32_t)face;
        w[1] = (uint32_t)sfail;
        w[2] = (uint32_t)dpfail;
        w[3] = (uint32_t)dppass;
        glt_post(glt_t_glStencilOpSeparate, w, 4, NULL, 0, "glStencilOpSeparate");
    }
}
static void glt_t_glTexImage2D(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glTexImage2D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLsizei)a[4], (GLint)a[5], (GLenum)a[6], (GLenum)a[7], (const GLvoid *)(uintptr_t)a[8]);
}
void __wrap_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * data)
{
    if (glt_direct()) { __real_glTexImage2D(target, level, internalFormat, width, height, border, format, type, data); return; }
    {
        uint32_t w[9];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)level;
        w[2] = (uint32_t)internalFormat;
        w[3] = (uint32_t)width;
        w[4] = (uint32_t)height;
        w[5] = (uint32_t)border;
        w[6] = (uint32_t)format;
        w[7] = (uint32_t)type;
        w[8] = (uint32_t)(uintptr_t)data;
        uint32_t r = glt_sync(glt_t_glTexImage2D, w, 9, "glTexImage2D");
        (void)r;
    }
}
static void glt_t_glTexParameteri(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glTexParameteri((GLenum)a[0], (GLenum)a[1], (GLint)a[2]);
}
void __wrap_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    if (glt_direct()) { __real_glTexParameteri(target, pname, param); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)pname;
        w[2] = (uint32_t)param;
        glt_post(glt_t_glTexParameteri, w, 3, NULL, 0, "glTexParameteri");
    }
}
static void glt_t_glTexSubImage2D(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glTexSubImage2D((GLenum)a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLsizei)a[4], (GLsizei)a[5], (GLenum)a[6], (GLenum)a[7], (const GLvoid *)(uintptr_t)a[8]);
}
void __wrap_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid * pixels)
{
    if (glt_direct()) { __real_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); return; }
    {
        uint32_t w[9];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)level;
        w[2] = (uint32_t)xoffset;
        w[3] = (uint32_t)yoffset;
        w[4] = (uint32_t)width;
        w[5] = (uint32_t)height;
        w[6] = (uint32_t)format;
        w[7] = (uint32_t)type;
        w[8] = (uint32_t)(uintptr_t)pixels;
        uint32_t r = glt_sync(glt_t_glTexSubImage2D, w, 9, "glTexSubImage2D");
        (void)r;
    }
}
static void glt_t_glUniform1f(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glUniform1f((GLint)a[0], glt_u2f(a[1]));
}
void __wrap_glUniform1f(GLint location, GLfloat v0)
{
    if (glt_direct()) { __real_glUniform1f(location, v0); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)location;
        w[1] = glt_f2u(v0);
        glt_post(glt_t_glUniform1f, w, 2, NULL, 0, "glUniform1f");
    }
}
static void glt_t_glUniform1i(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glUniform1i((GLint)a[0], (GLint)a[1]);
}
void __wrap_glUniform1i(GLint location, GLint v0)
{
    if (glt_direct()) { __real_glUniform1i(location, v0); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)location;
        w[1] = (uint32_t)v0;
        glt_post(glt_t_glUniform1i, w, 2, NULL, 0, "glUniform1i");
    }
}
static void glt_t_glUniform4f(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glUniform4f((GLint)a[0], glt_u2f(a[1]), glt_u2f(a[2]), glt_u2f(a[3]), glt_u2f(a[4]));
}
void __wrap_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    if (glt_direct()) { __real_glUniform4f(location, v0, v1, v2, v3); return; }
    {
        uint32_t w[5];
        w[0] = (uint32_t)location;
        w[1] = glt_f2u(v0);
        w[2] = glt_f2u(v1);
        w[3] = glt_f2u(v2);
        w[4] = glt_f2u(v3);
        glt_post(glt_t_glUniform4f, w, 5, NULL, 0, "glUniform4f");
    }
}
static void glt_t_glUniform4fv(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glUniform4fv((GLint)a[0], (GLsizei)a[1], (const GLfloat *)(void *)pl);
}
void __wrap_glUniform4fv(GLint location, GLsizei count, const GLfloat * value)
{
    if (glt_direct()) { __real_glUniform4fv(location, count, value); return; }
    {
        uint32_t w[3];
        w[0] = (uint32_t)location;
        w[1] = (uint32_t)count;
        w[2] = (uint32_t)(uintptr_t)value;
        glt_post(glt_t_glUniform4fv, w, 3, value, (uint32_t)count * 16u, "glUniform4fv");
    }
}
static void glt_t_glUnmapBuffer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_glUnmapBuffer((GLenum)a[0]);
}
GLboolean __wrap_glUnmapBuffer(GLenum target)
{
    if (glt_direct()) return __real_glUnmapBuffer(target);
    {
        uint32_t w[1];
        w[0] = (uint32_t)target;
        uint32_t r = glt_sync(glt_t_glUnmapBuffer, w, 1, "glUnmapBuffer");
        return (GLboolean)(uintptr_t)r;
    }
}
static void glt_t_glUseProgram(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glUseProgram((GLuint)a[0]);
}
void __wrap_glUseProgram(GLuint program)
{
    if (glt_direct()) { __real_glUseProgram(program); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)program;
        glt_post(glt_t_glUseProgram, w, 1, NULL, 0, "glUseProgram");
    }
}
static void glt_t_glVertexAttrib4f(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_glVertexAttrib4f((GLuint)a[0], glt_u2f(a[1]), glt_u2f(a[2]), glt_u2f(a[3]), glt_u2f(a[4]));
}
void __wrap_glVertexAttrib4f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    if (glt_direct()) { __real_glVertexAttrib4f(index, v0, v1, v2, v3); return; }
    {
        uint32_t w[5];
        w[0] = (uint32_t)index;
        w[1] = glt_f2u(v0);
        w[2] = glt_f2u(v1);
        w[3] = glt_f2u(v2);
        w[4] = glt_f2u(v3);
        glt_post(glt_t_glVertexAttrib4f, w, 5, NULL, 0, "glVertexAttrib4f");
    }
}
static void glt_t_vglAlloc(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_vglAlloc((uint32_t)a[0], (vglMemType)a[1]);
}
void * __wrap_vglAlloc(uint32_t size, vglMemType type)
{
    if (glt_direct()) return __real_vglAlloc(size, type);
    {
        uint32_t w[2];
        w[0] = (uint32_t)size;
        w[1] = (uint32_t)type;
        uint32_t r = glt_sync(glt_t_vglAlloc, w, 2, "vglAlloc");
        return (void *)(uintptr_t)r;
    }
}
static void glt_t_vglFree(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_vglFree((void *)(uintptr_t)a[0]);
}
void __wrap_vglFree(void * addr)
{
    if (glt_direct()) { __real_vglFree(addr); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)(uintptr_t)addr;
        uint32_t r = glt_sync(glt_t_vglFree, w, 1, "vglFree");
        (void)r;
    }
}
static void glt_t_vglGetGxmTexture(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_vglGetGxmTexture((GLenum)a[0]);
}
SceGxmTexture * __wrap_vglGetGxmTexture(GLenum target)
{
    if (glt_direct()) return __real_vglGetGxmTexture(target);
    {
        uint32_t w[1];
        w[0] = (uint32_t)target;
        uint32_t r = glt_sync(glt_t_vglGetGxmTexture, w, 1, "vglGetGxmTexture");
        return (SceGxmTexture *)(uintptr_t)r;
    }
}
static void glt_t_vglGetTexDataPointer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_vglGetTexDataPointer((GLenum)a[0]);
}
void * __wrap_vglGetTexDataPointer(GLenum target)
{
    if (glt_direct()) return __real_vglGetTexDataPointer(target);
    {
        uint32_t w[1];
        w[0] = (uint32_t)target;
        uint32_t r = glt_sync(glt_t_vglGetTexDataPointer, w, 1, "vglGetTexDataPointer");
        return (void *)(uintptr_t)r;
    }
}
static void glt_t_vglMalloc(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_vglMalloc((uint32_t)a[0]);
}
void * __wrap_vglMalloc(uint32_t size)
{
    if (glt_direct()) return __real_vglMalloc(size);
    {
        uint32_t w[1];
        w[0] = (uint32_t)size;
        uint32_t r = glt_sync(glt_t_vglMalloc, w, 1, "vglMalloc");
        return (void *)(uintptr_t)r;
    }
}
static void glt_t_vglMemFree(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    *res = (uint32_t)(uintptr_t)__real_vglMemFree((vglMemType)a[0]);
}
size_t __wrap_vglMemFree(vglMemType type)
{
    if (glt_direct()) return __real_vglMemFree(type);
    {
        uint32_t w[1];
        w[0] = (uint32_t)type;
        uint32_t r = glt_sync(glt_t_vglMemFree, w, 1, "vglMemFree");
        return (size_t)(uintptr_t)r;
    }
}
static void glt_t_vglOverloadTexDataPointer(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_vglOverloadTexDataPointer((GLenum)a[0], (void *)(uintptr_t)a[1]);
}
void __wrap_vglOverloadTexDataPointer(GLenum target, void * data)
{
    if (glt_direct()) { __real_vglOverloadTexDataPointer(target, data); return; }
    {
        uint32_t w[2];
        w[0] = (uint32_t)target;
        w[1] = (uint32_t)(uintptr_t)data;
        uint32_t r = glt_sync(glt_t_vglOverloadTexDataPointer, w, 2, "vglOverloadTexDataPointer");
        (void)r;
    }
}
static void glt_t_vglWaitVblankStart(const uint32_t *a, uint8_t *pl, uint32_t *res)
{
    (void)a; (void)pl; (void)res;
    __real_vglWaitVblankStart((GLboolean)a[0]);
}
void __wrap_vglWaitVblankStart(GLboolean enable)
{
    if (glt_direct()) { __real_vglWaitVblankStart(enable); return; }
    {
        uint32_t w[1];
        w[0] = (uint32_t)enable;
        glt_post(glt_t_vglWaitVblankStart, w, 1, NULL, 0, "vglWaitVblankStart");
    }
}
