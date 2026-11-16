#!/usr/bin/env python3
"""(v231) Writes src/video/vgl_glt_gen.inc: the --wrap'd vitaGL entry points of
the GL worker thread (src/video/vgl_glt.c). Each function is one of:
  A  asynchronous: its arguments (and, for a pointer argument, a copy of what
     it points to: PAYLOAD) queued, the caller goes on;
  S  synchronous: queued with a slot for the answer, the caller waits until
     the worker has run it (every pointer it passes stays valid meanwhile);
  C  custom: written by hand in vgl_glt.c (the vertex attributes, the draws,
     the swap, the frame number).
Every argument travels as one 32-bit word (GLdouble as two)."""
import re, sys

SPEC = r"""
A void glActiveTexture(GLenum texture);
S void glAttachShader(GLuint prog, GLuint shad);
S void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
C void glBindBuffer(GLenum target, GLuint buffer);
A void glBindFramebuffer(GLenum target, GLuint framebuffer);
A void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
A void glBindSampler(GLuint unit, GLuint smp);
A void glBindTexture(GLenum target, GLuint texture);
C void glBindVertexArray(GLuint array);
A void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
A void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
A void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
S void glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage);
S GLenum glCheckFramebufferStatus(GLenum target);
A void glClear(GLbitfield mask);
A void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
A void glClearDepth(GLdouble depth);
A void glClearStencil(GLint s);
A void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
S void glCompileShader(GLuint shader);
S GLuint glCreateProgram(void);
S GLuint glCreateShader(GLenum shaderType);
A void glCullFace(GLenum mode);
A void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);   PAYLOAD framebuffers (uint32_t)n * 4u
A void glDeleteProgram(GLuint prog);
A void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);   PAYLOAD renderbuffers (uint32_t)n * 4u
A void glDeleteShader(GLuint shad);
A void glDeleteTextures(GLsizei n, const GLuint *textures);   PAYLOAD textures (uint32_t)n * 4u
A void glDepthFunc(GLenum func);
A void glDepthMask(GLboolean flag);
A void glDisable(GLenum cap);
C void glDisableVertexAttribArray(GLuint index);
C void glDrawArrays(GLenum mode, GLint first, GLsizei count);
C void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
C void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices);
A void glEnable(GLenum cap);
C void glEnableVertexAttribArray(GLuint index);
S void glFinish(void);
A void glFlush(void);
A void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
A void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
A void glFrontFace(GLenum mode);
S void glGenBuffers(GLsizei n, GLuint *buffers);
S void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
S void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
S void glGenSamplers(GLsizei n, GLuint *smps);
S void glGenTextures(GLsizei n, GLuint *textures);
S void glGenVertexArrays(GLsizei n, GLuint *res);
A void glGenerateMipmap(GLenum target);
S GLenum glGetError(void);
C void glGetIntegerv(GLenum pname, GLint *data);
S void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary);
S void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
S void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
S void glGetShaderInfoLog(GLuint handle, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
S void glGetShaderiv(GLuint handle, GLenum pname, GLint *params);
S const GLubyte *glGetString(GLenum name);
S GLuint glGetUniformBlockIndex(GLuint prog, const GLchar *uniformBlockName);
S GLint glGetUniformLocation(GLuint prog, const GLchar *name);
S void glLinkProgram(GLuint progr);
S void *glMapBuffer(GLenum target, GLbitfield access);
A void glPixelStorei(GLenum pname, GLint param);
A void glReadBuffer(GLenum mode);
S void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data);
A void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
A void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
A void glSamplerParameteri(GLuint target, GLenum pname, GLint param);
A void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
S void glShaderBinary(GLsizei count, const GLuint *handles, GLenum binaryFormat, const void *binary, GLsizei length);
S void glShaderSource(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length);
A void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
A void glStencilMask(GLuint mask);
A void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
S void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data);
A void glTexParameteri(GLenum target, GLenum pname, GLint param);
S void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
A void glUniform1f(GLint location, GLfloat v0);
A void glUniform1i(GLint location, GLint v0);
A void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
A void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);   PAYLOAD value (uint32_t)count * 16u
S GLboolean glUnmapBuffer(GLenum target);
A void glUseProgram(GLuint program);
A void glVertexAttrib4f(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
C void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
C void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
S void *vglAlloc(uint32_t size, vglMemType type);
S void vglFree(void *addr);
S SceGxmTexture *vglGetGxmTexture(GLenum target);
S void *vglGetTexDataPointer(GLenum target);
S void *vglMalloc(uint32_t size);
S size_t vglMemFree(vglMemType type);
S void vglOverloadTexDataPointer(GLenum target, void *data);
C void vglSwapBuffers(GLboolean has_commondialog);
A void vglWaitVblankStart(GLboolean enable);
C uint32_t vglGetFrameNumber(void);
"""

FLOATS = {'GLfloat'}
def parse(line):
    m = re.match(r'([ASC])\s+(.+?)\b(\w+)\((.*?)\);\s*(?:PAYLOAD\s+(\w+)\s+(.+))?$', line.strip())
    if not m: raise SystemExit('bad: ' + line)
    mode, ret, name, args, pl_arg, pl_len = m.groups()
    ret = ret.strip()
    al = []
    if args.strip() not in ('', 'void'):
        for a in args.split(','):
            a = a.strip()
            mm = re.match(r'(.*?)(\w+)$', a)
            t, n = mm.group(1).strip(), mm.group(2)
            al.append((t, n))
    return mode, ret, name, al, pl_arg, pl_len

out = []
w = out.append
w('/* GENERATED by tools/glt/gen_glt.py -- do not edit: vgl_glt.c includes it */')
protos = []
for line in SPEC.strip().split('\n'):
    if not line.strip(): continue
    mode, ret, name, al, pl_arg, pl_len = parse(line)
    params = ', '.join('%s %s' % (t, n) for t, n in al) or 'void'
    protos.append('%s __real_%s(%s);' % (ret, name, params))
    if mode == 'C': continue
    # words: one per argument (a GLdouble two)
    enc, dec, nw = [], [], 0
    for t, n in al:
        if t == 'GLdouble':
            enc.append('{ union { double d; uint32_t u[2]; } c; c.d = %s; w[%d] = c.u[0]; w[%d] = c.u[1]; }' % (n, nw, nw + 1))
            dec.append('({ union { double d; uint32_t u[2]; } c; c.u[0] = a[%d]; c.u[1] = a[%d]; c.d; })' % (nw, nw + 1))
            nw += 2
        elif t in FLOATS:
            enc.append('w[%d] = glt_f2u(%s);' % (nw, n)); dec.append('glt_u2f(a[%d])' % nw); nw += 1
        elif '*' in t:
            enc.append('w[%d] = (uint32_t)(uintptr_t)%s;' % (nw, n))
            if pl_arg == n: dec.append('(%s)(void *)pl' % t)
            else: dec.append('(%s)(uintptr_t)a[%d]' % (t, nw))
            nw += 1
        else:
            enc.append('w[%d] = (uint32_t)%s;' % (nw, n)); dec.append('(%s)a[%d]' % (t, nw)); nw += 1
    call = '__real_%s(%s)' % (name, ', '.join(dec))
    w('static void glt_t_%s(const uint32_t *a, uint8_t *pl, uint32_t *res)' % name)
    w('{')
    w('    (void)a; (void)pl; (void)res;')
    if ret == 'void': w('    %s;' % call)
    elif ret in ('GLfloat',): w('    *res = glt_f2u(%s);' % call)
    else: w('    *res = (uint32_t)(uintptr_t)%s;' % call)
    w('}')
    w('%s __wrap_%s(%s)' % (ret, name, params))
    w('{')
    direct = '__real_%s(%s)' % (name, ', '.join(n for t, n in al))
    if ret == 'void': w('    if (glt_direct()) { %s; return; }' % direct)
    else: w('    if (glt_direct()) return %s;' % direct)
    w('    {')
    w('        uint32_t w[%d];' % max(nw, 1))
    for e in enc: w('        ' + e)
    if nw == 0: w('        w[0] = 0;')
    if mode == 'A':
        if pl_arg: w('        glt_post(glt_t_%s, w, %d, %s, %s, "%s");' % (name, nw, pl_arg, pl_len, name))
        else: w('        glt_post(glt_t_%s, w, %d, NULL, 0, "%s");' % (name, nw, name))
        if ret != 'void': raise SystemExit('async with a result: ' + name)
    else:
        w('        uint32_t r = glt_sync(glt_t_%s, w, %d, "%s");' % (name, nw, name))
        if ret == 'void': w('        (void)r;')
        else: w('        return (%s)(uintptr_t)r;' % ret)
    w('    }')
    w('}')
open(sys.argv[1], 'w').write('\n'.join(protos) + '\n\n' + '\n'.join(out) + '\n')
names = [parse(l)[2] for l in SPEC.strip().split('\n') if l.strip()]
open(sys.argv[2], 'w').write(' '.join('-Wl,--wrap=' + n for n in names) + '\n')
print(len(names), 'functions')
