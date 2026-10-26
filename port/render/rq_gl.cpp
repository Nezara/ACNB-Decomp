// GL 4.5 backend (design sections 4.4, 11): the only file of the module with GL headers or GL names. Forked from the
// GL parts of runtime/gs_hw.c: shaders copied verbatim (0 px parity needs identical shader code), persistent vertex
// ring with fences, render targets (colour RGBA8 / depth 32F at the internal resolution), blend mapping, the alpha
// test passes, display merge (fs_disp), 1x download / upload staging for scaled targets. Own objects only: the
// reference (gs_hw.c) shares the context, so enter / leave save and restore what gs_hw.c caches or assumes.
#ifdef _WIN32
#define APIENTRY __stdcall   // keeps windows.h out of glcorearb.h
#endif
#include <GL/glcorearb.h>
#include "rq_internal.h"
#include <stddef.h>
#include <stdio.h>

#define RQ_GL_FUNCS(X) \
    X(PFNGLGENTEXTURESPROC, glGenTextures) X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) X(PFNGLTEXSTORAGE2DPROC, glTexStorage2D) \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri) X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLUNIFORM1UIPROC, glUniform1ui) \
    X(PFNGLUNIFORM2IPROC, glUniform2i) X(PFNGLUNIFORM3FPROC, glUniform3f) X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM4IPROC, glUniform4i) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERSTORAGEPROC, glBufferStorage) X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLCLIPCONTROLPROC, glClipControl) X(PFNGLPROVOKINGVERTEXPROC, glProvokingVertex) \
    X(PFNGLENABLEPROC, glEnable) X(PFNGLDISABLEPROC, glDisable) X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
    X(PFNGLVIEWPORTPROC, glViewport) X(PFNGLSCISSORPROC, glScissor) X(PFNGLCOLORMASKPROC, glColorMask) \
    X(PFNGLDEPTHMASKPROC, glDepthMask) X(PFNGLDEPTHFUNCPROC, glDepthFunc) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays) X(PFNGLREADPIXELSPROC, glReadPixels) X(PFNGLFLUSHPROC, glFlush) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) X(PFNGLCOPYIMAGESUBDATAPROC, glCopyImageSubData) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLFENCESYNCPROC, glFenceSync) \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync) X(PFNGLDELETESYNCPROC, glDeleteSync) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) X(PFNGLCLEARCOLORPROC, glClearColor) X(PFNGLCLEARPROC, glClear)

#define MAXT 64                 /* target slots (rq_tex keeps at most 48 surfaces) */
#define RING_SEG_V 131072       /* vertex streaming: one persistently mapped ring, fenced in 4 segments */
#define RING_SEGS 4

/* prog_draw uniforms: locations looked up once, values shadowed to skip redundant updates */
enum { UD_NDC, UD_ZMAX, UD_F0, UD_F1, UD_TSZ, UD_WRAP, UD_REG, UD_TEXA, UD_TEX, UD_FOGC, UD_BIAS, UD_BL, UD_AT, UD_RS, UD_SNAP, UD_N };
static const char *const ud_name[UD_N] = {"u_ndc", "u_zmax", "u_f0", "u_f1", "u_tsz", "u_wrap", "u_reg", "u_texa",
                                          "u_tex", "u_fogc", "u_bias", "u_bl", "u_at", "u_rs", "u_snap"};

struct Gl {
#define X(T, n) T n;
    RQ_GL_FUNCS(X)
#undef X
    int loaded, ready, rs, keep_fb;
    struct { GLuint p; const char *n; GLint loc; } uloc[48]; int nuloc;   /* U() cache (names are literals) */
    GLuint prog_draw, prog_zup, prog_zdown, prog_cdown, prog_disp, prog_c2d, prog_d2c;
    GLuint vao, vbo, draw_fbo, util_fbo;
    GLuint zup_tex, zdown_tex, zdown_fbo, tmp_tex, disp_tex, disp_fbo, circ_tex[2];
    int aa; GLuint aa_tex, aa_fbo, out_fbo; int aa_w, aa_h;   /* step 5 video.aa: 2 = disp box-resolved into aa_tex */
    GLuint down_tex, down_fbo, up_tex, up_fbo;   /* 1x colour staging for scaled targets (rs > 1) */
    int tmp_w, tmp_h, disp_w, disp_h;
    RqHwVert *ring;
    uint32_t ring_pos;   /* in vertices */
    int ring_seg;
    GLsync ring_fence[RING_SEGS];
    GLint ud[UD_N];
    uint32_t ud_val[UD_N][4];
    uint8_t ud_set[UD_N];
    GLuint cur_prog;
    GLuint att_color, att_depth;   /* draw_fbo attachments currently set (reset when a texture name may be recycled) */
    struct {
        GLuint tex, fbo; int depth, w, h;
        uint32_t gen;               /* bumped by every GPU write (draw, upload, copy) */
        uint32_t dl_gen; int sw, k, dl_y0, dl_y1, dl_w;   /* readback pattern (see gl_target_download) */
        struct { GLuint pbo; GLsync fence; uint8_t *map; uint32_t gen; int y0, y1, w, live; } pf;
    } tg[MAXT];
    uint32_t cur_c, cur_d;          /* targets of the last draw */
    uint64_t w_ring, w_sync, w_issue, w_pf;   /* CPU ns blocked: ring fences, synchronous readbacks, prefetch reads / fences */
        uint32_t n_sync, n_pf, n_hit, n_stale, n_cur;
    GLint sv_prog, sv_vao, sv_abuf, sv_dfb, sv_rfb, sv_active, sv_tex[2], sv_pack, sv_unpack;   /* host state (enter) */
    int warned_ad, warned_cd;
};

/* ---- shaders: verbatim copy of runtime/gs_hw.c (keep identical, design section 11) */
static const char *vs_draw =
    "#version 450\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in uint a_z; layout(location=2) in vec3 a_tex;\n"
    "layout(location=3) in vec4 a_col; layout(location=4) in float a_fog;\n"
    "uniform vec4 u_ndc; uniform uint u_zmax;\n"
    "noperspective out vec3 v_tex; noperspective out vec4 v_col; flat out vec4 v_fcol; noperspective out float v_fog;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos * u_ndc.xy + u_ndc.zw, float(min(a_z, u_zmax)) * exp2(-32.0), 1.0);\n"
    "  v_tex = a_tex; v_col = a_col; v_fcol = a_col; v_fog = a_fog;\n"
    "}\n";

static const char *fs_draw =
    "#version 450\n"
    "noperspective in vec3 v_tex; noperspective in vec4 v_col; flat in vec4 v_fcol; noperspective in float v_fog;\n"
    "layout(location=0, index=0) out vec4 o_col;\n"
    "layout(location=0, index=1) out vec4 o_fac;\n"
    "uniform sampler2D u_tex;\n"
    "uniform ivec4 u_f0;   // tme fst iip fge\n"
    "uniform ivec4 u_f1;   // tfx tcc amode linear\n"
    "uniform ivec4 u_tsz;  // 1<<tw 1<<th glw glh\n"
    "uniform ivec4 u_wrap; // wms wmt aniso (step 5: > 1 = max taps; not in gs_hw.c)\n"
    "uniform ivec4 u_reg;  // minu maxu minv maxv\n"
    "uniform ivec4 u_texa; // ta0 ta1 aem\n"
    "uniform ivec4 u_at;   // ate atst aref discard_when\n"
    "uniform ivec4 u_bl;   // csel kmode fba fix\n"
    "uniform vec3 u_fogc; uniform float u_bias;\n"
    "uniform int u_rs;     // texels per GS texel of u_tex (a scaled render target), else 1\n"
    "uniform int u_snap;   // step 5.1 (not in gs_hw.c): > 1 = interpolants at each GS pixel's first sub-pixel\n"
    "int wrapc(int c, int sz, int m, int mn, int mx) {\n"
    "  if (m == 0) return c & (sz - 1);\n"
    "  if (m == 1) return clamp(c, 0, sz - 1);\n"
    "  if (m == 2) return clamp(c, mn, mx);\n"
    "  return (c & mn) | mx;\n"
    "}\n"
    "ivec4 texel(ivec2 c, ivec2 sub) {   // GS texel c, sub-texel sub of a scaled texture\n"
    "  c.x = wrapc(c.x, u_tsz.x, u_wrap.x, u_reg.x, u_reg.y);\n"
    "  c.y = wrapc(c.y, u_tsz.y, u_wrap.y, u_reg.z, u_reg.w);\n"
    "  c = clamp(c, ivec2(0), u_tsz.zw - 1);\n"
    "  ivec4 t = ivec4(texelFetch(u_tex, c * u_rs + sub, 0) * 255.0 + 0.5);\n"
    "  bool z = u_texa.z != 0 && t.r + t.g + t.b == 0;\n"
    "  if (u_f1.z == 1) t.a = z ? 0 : u_texa.x;\n"
    "  else if (u_f1.z == 2) t.a = t.a >= 128 ? u_texa.y : (z ? 0 : u_texa.x);\n"
    "  return t;\n"
    "}\n"
    "ivec4 tap(ivec2 h) {   // texel h of the (possibly scaled) texture\n"
    "  ivec2 c = ivec2(floor(vec2(h) / float(u_rs)));\n"
    "  return texel(c, h - c * u_rs);\n"
    "}\n"
    "ivec4 sample_tex(vec2 uv) {\n"
    "  if (u_f1.w == 0) {\n"
    "    vec2 f = floor(uv + 1.0 / 1024.0);\n"
    "    return texel(ivec2(f), clamp(ivec2((uv + 1.0 / 1024.0 - f) * float(u_rs)), ivec2(0), ivec2(u_rs - 1)));\n"
    "  }\n"
    "  uv = uv * float(u_rs) - 0.5;\n"
    "  vec2 f0 = floor(uv + 1.0 / 1024.0); ivec2 i = ivec2(f0);\n"
    "  ivec2 fr = clamp(ivec2((uv - f0) * 256.0), ivec2(0), ivec2(256));\n"
    "  ivec4 c00 = tap(i), c10 = tap(i + ivec2(1, 0)), c01 = tap(i + ivec2(0, 1)), c11 = tap(i + ivec2(1, 1));\n"
    "  ivec4 a = (c00 * (256 - fr.x) + c10 * fr.x) >> 8, b = (c01 * (256 - fr.x) + c11 * fr.x) >> 8;\n"
    "  return (a * (256 - fr.y) + b * fr.y) >> 8;\n"
    "}\n"
    "ivec4 sample_aniso(vec2 uv, vec2 dx, vec2 dy) {   // step 5 video.aniso: taps spread over the major axis\n"
    "  float lx = length(dx), ly = length(dy), lmax = max(lx, ly), lmin = max(min(lx, ly), 1.0 / 64.0);\n"
    "  if (lmax <= 1.0) return sample_tex(uv);   // not minified along the major axis\n"
    "  int n = clamp(int(ceil(lmax / lmin)), 1, u_wrap.z);\n"
    "  if (n <= 1) return sample_tex(uv);\n"
    "  vec2 ax = lx >= ly ? dx : dy; ivec4 acc = ivec4(0);\n"
    "  for (int i = 0; i < n; i++) acc += sample_tex(uv + ax * ((float(i) + 0.5) / float(n) - 0.5));\n"
    "  return (acc + n / 2) / n;\n"
    "}\n"
    "void main() {\n"
    "  vec3 tx = v_tex; vec4 vc = v_col; float vf = v_fog;\n"
    "  if (u_snap > 1) {   // 2D layout: the 1x picture scaled by an integer (u_snap is uniform: derivatives valid)\n"
    "    vec2 p = gl_FragCoord.xy, d = floor(p / float(u_snap)) * float(u_snap) + 0.5 - p;\n"
    "    tx += dFdx(v_tex) * d.x + dFdy(v_tex) * d.y; vc += dFdx(v_col) * d.x + dFdy(v_col) * d.y;\n"
    "    vf += dFdx(v_fog) * d.x + dFdy(v_fog) * d.y;\n"
    "  }\n"
    "  ivec4 c = ivec4(u_f0.z != 0 ? vc : v_fcol);\n"
    "  if (u_f0.x != 0) {\n"
    "    vec2 uv = u_f0.y != 0 ? tx.xy : tx.xy / tx.z * vec2(u_tsz.xy);\n"
    "    vec2 ddx = dFdx(uv), ddy = dFdy(uv);   // uniform control flow (u_f0, u_wrap uniform)\n"
    "    ivec4 t = u_wrap.z > 1 ? sample_aniso(uv, ddx, ddy) : sample_tex(uv);\n"
    "    int tfx = u_f1.x; bool tcc = u_f1.y != 0;\n"
    "    if (tfx == 0) { c.rgb = (t.rgb * c.rgb) >> 7; if (tcc) c.a = (t.a * c.a) >> 7; }\n"
    "    else if (tfx == 1) { c.rgb = t.rgb; if (tcc) c.a = t.a; }\n"
    "    else if (tfx == 2) { c.rgb = ((t.rgb * c.rgb) >> 7) + c.a; if (tcc) c.a = t.a + c.a; }\n"
    "    else { c.rgb = ((t.rgb * c.rgb) >> 7) + c.a; if (tcc) c.a = t.a; }\n"
    "    c = clamp(c, 0, 255);\n"
    "  }\n"
    "  if (u_f0.w != 0) {\n"
    "    int f = clamp(int(vf), 0, 255);\n"
    "    c.rgb = (f * c.rgb + (255 - f) * ivec3(u_fogc)) >> 8;\n"
    "  }\n"
    "  if (u_at.x != 0) {\n"
    "    int a = c.a, r = u_at.z; bool pass;\n"
    "    switch (u_at.y) { case 0: pass = false; break; case 1: pass = true; break; case 2: pass = a < r; break;\n"
    "      case 3: pass = a <= r; break; case 4: pass = a == r; break; case 5: pass = a >= r; break;\n"
    "      case 6: pass = a > r; break; default: pass = a != r; break; }\n"
    "    if (pass == (u_at.w != 0)) discard;\n"
    "  }\n"
    "  float f = float(u_bl.x == 2 ? u_bl.w : c.a) / 128.0;\n"
    "  float k = 1.0;\n"
    "  switch (u_bl.y) { case 0: k = 0.0; break; case 2: k = f; break; case 3: k = 1.0 + f; break; case 4: k = max(1.0 - f, 0.0); break; }\n"
    "  int aw = u_bl.z != 0 ? (c.a | 0x80) : c.a;\n"
    "  o_col = vec4((vec3(c.rgb) * k + u_bias) / 255.0, float(aw) / 255.0);\n"
    "  o_fac = vec4(f);\n"
    "}\n";

static const char *vs_full =
    "#version 450\n"
    "void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

static const char *fs_zup =
    "#version 450\n"
    "uniform usampler2D u_src; uniform int u_n;\n"
    "void main() { gl_FragDepth = float(texelFetch(u_src, ivec2(gl_FragCoord.xy) / u_n, 0).r) * exp2(-32.0); }\n";

static const char *fs_zdown =
    "#version 450\n"
    "uniform sampler2D u_src; uniform int u_n; out uint o;\n"
    "void main() { float d = texelFetch(u_src, ivec2(gl_FragCoord.xy) * u_n, 0).r; o = d >= 1.0 ? 0xffffffffu : uint(d * 4294967296.0); }\n";

/* scaled colour -> 1x for downloads: sub-pixel 0 of each GS pixel. The draw offset (eps in draw_batch) puts that
 * sub-pixel's sample point on the GS pixel corner the 1x picture samples, so for 12.4 geometry the downloaded bytes
 * are the 1x render's (readbacks the game keeps in RAM must not depend on video.scale / resolution; the centre
 * sub-pixel the old blit took gave the scaled coverage at edges). zdown takes the same sub-pixel. */
static const char *fs_cdown =
    "#version 450\n"
    "uniform sampler2D u_src; uniform int u_n; out vec4 o;\n"
    "void main() { o = texelFetch(u_src, ivec2(gl_FragCoord.xy) * u_n, 0); }\n";

/* compatible-surface copies: colour bytes <-> depth value (same 32-bit word) */
static const char *fs_c2d =
    "#version 450\n"
    "uniform sampler2D u_src; uniform uint u_zmask;\n"
    "void main() {\n"
    "  uvec4 c = uvec4(texelFetch(u_src, ivec2(gl_FragCoord.xy), 0) * 255.0 + 0.5);\n"
    "  uint z = (c.r | (c.g << 8) | (c.b << 16) | (c.a << 24)) & u_zmask;\n"
    "  gl_FragDepth = float(z) * exp2(-32.0);\n"
    "}\n";

static const char *fs_d2c =
    "#version 450\n"
    "uniform sampler2D u_src; out vec4 o;\n"
    "void main() {\n"
    "  float d = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).r;\n"
    "  uint z = d >= 1.0 ? 0xffffffffu : uint(d * 4294967296.0);\n"
    "  o = vec4(uvec4(z, z >> 8, z >> 16, z >> 24) & 255u) / 255.0;\n"
    "}\n";

static const char *fs_disp =
    "#version 450\n"
    "uniform sampler2D u_t0, u_t1;\n"
    "uniform ivec4 u_c0, u_c1;   // output offset x,y\n"
    "uniform ivec4 u_s0, u_s1;   // source x,y, psm class (0 32, 1 24, 2 16), enabled\n"
    "uniform ivec2 u_sc;         // texels per GS pixel of circuit 0 / 1 (scaled render target or 1)\n"
    "uniform int u_n;            // output pixels per GS pixel\n"
    "uniform ivec4 u_m;          // mmod slbg alp\n"
    "uniform vec3 u_bg;\n"
    "out vec4 o;\n"
    "vec4 fetch(sampler2D t, ivec4 c, ivec4 s, int sc, out bool have) {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy) - c.xy * u_n; have = s.w != 0 && p.x >= 0 && p.y >= 0;\n"
    "  if (!have) return vec4(0.0);\n"
    "  vec4 v = texelFetch(t, clamp(p * sc / u_n + s.xy * sc, ivec2(0), textureSize(t, 0) - 1), 0);\n"
    "  if (s.z == 1) v.a = 128.0 / 255.0; else if (s.z == 2) v.a = v.a >= 0.5 ? 1.0 : 0.0;\n"
    "  return v;\n"
    "}\n"
    "void main() {\n"
    "  bool h0, h1; vec4 p0 = fetch(u_t0, u_c0, u_s0, u_sc.x, h0), p1 = fetch(u_t1, u_c1, u_s1, u_sc.y, h1);\n"
    "  bool en0 = u_s0.w != 0, en1 = u_s1.w != 0;\n"
    "  vec3 r;\n"
    "  if (en0 && h0) {\n"
    "    float a = u_m.x != 0 ? min(float(u_m.z), 255.0) / 255.0 : p0.a;\n"
    "    vec3 b = (en1 && h1 && u_m.y == 0) ? p1.rgb : ((u_m.y != 0 || !en1) ? u_bg : vec3(0.0));\n"
    "    r = (!en1 && u_m.y == 0) ? p0.rgb : p0.rgb * a + b * (1.0 - a);\n"
    "  } else if (h1) r = p1.rgb; else r = u_bg;\n"
    "  o = vec4(r, 1.0);\n"
    "}\n";

static void gl_fatal(const char *m, const char *log)
{
    rq_log("rq_gl: %s%s%s", m, log ? "\n" : "", log ? log : "");
    exit(1);
}

static GLuint compile(Gl *g, GLenum type, const char *src)
{
    GLuint s = g->glCreateShader(type);
    g->glShaderSource(s, 1, &src, 0);
    g->glCompileShader(s);
    GLint ok;
    g->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; g->glGetShaderInfoLog(s, sizeof log, 0, log); gl_fatal("shader compile failed:", log); }
    return s;
}

static GLuint program(Gl *g, const char *vs, const char *fs)
{
    GLuint p = g->glCreateProgram();
    g->glAttachShader(p, compile(g, GL_VERTEX_SHADER, vs));
    g->glAttachShader(p, compile(g, GL_FRAGMENT_SHADER, fs));
    g->glLinkProgram(p);
    GLint ok;
    g->glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; g->glGetProgramInfoLog(p, sizeof log, 0, log); gl_fatal("program link failed:", log); }
    return p;
}

/* uniform location, cached: glGetUniformLocation is a query, i.e. a sync with a threaded driver's worker thread */
static GLint U(Gl *g, GLuint p, const char *n)
{
    for (int i = 0; i < g->nuloc; i++) if (g->uloc[i].p == p && g->uloc[i].n == n) return g->uloc[i].loc;
    GLint loc = g->glGetUniformLocation(p, n);
    if (g->nuloc < 48) { g->uloc[g->nuloc].p = p; g->uloc[g->nuloc].n = n; g->uloc[g->nuloc].loc = loc; g->nuloc++; }
    return loc;
}

static int ud_same(Gl *g, int u, const void *v)
{
    if (g->ud_set[u] && !memcmp(g->ud_val[u], v, 16)) return 1;
    memcpy(g->ud_val[u], v, 16);
    g->ud_set[u] = 1;
    return 0;
}
static void ud_4i(Gl *g, int u, int a, int b, int c, int d) { int v[4] = {a, b, c, d}; if (!ud_same(g, u, v)) g->glUniform4i(g->ud[u], a, b, c, d); }
static void ud_4f(Gl *g, int u, float a, float b, float c, float d) { float v[4] = {a, b, c, d}; if (!ud_same(g, u, v)) g->glUniform4f(g->ud[u], a, b, c, d); }
static void ud_1ui(Gl *g, int u, uint32_t a) { uint32_t v[4] = {a}; if (!ud_same(g, u, v)) g->glUniform1ui(g->ud[u], a); }

static void use_prog(Gl *g, GLuint p) { if (p != g->cur_prog) { g->glUseProgram(p); g->cur_prog = p; } }

static GLuint new_tex(Gl *g, GLenum fmt, int w, int h)
{
    GLuint t;
    g->glGenTextures(1, &t);
    g->glBindTexture(GL_TEXTURE_2D, t);
    g->glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint fbo_for(Gl *g, GLuint color, GLuint depth)
{
    GLuint f;
    g->glGenFramebuffers(1, &f);
    g->glBindFramebuffer(GL_FRAMEBUFFER, f);
    if (color) g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    GLenum db = color ? GL_COLOR_ATTACHMENT0 : GL_NONE;
    g->glDrawBuffers(1, &db);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) rq_log("rq_gl: incomplete fbo");
    return f;
}

static void gl_load(Gl *g)
{
    if (!rq_gl_current()) gl_fatal("no OpenGL 4.5 context", 0);
    int ok = 1;
#define X(T, n) g->n = (T)rq_gl_proc(#n); if (!g->n) { rq_log("rq_gl: missing %s", #n); ok = 0; }
    RQ_GL_FUNCS(X)
#undef X
    if (!ok) gl_fatal("GL entry points missing", 0);
    g->loaded = 1;
}

/* gs_hw.c init(): objects + the context-global state (set to the identical values gs_hw.c uses) */
static void gl_init(Gl *g)
{
    g->ready = 1;
    g->prog_draw = program(g, vs_draw, fs_draw);
    g->prog_zup = program(g, vs_full, fs_zup);
    g->prog_zdown = program(g, vs_full, fs_zdown);
    g->prog_cdown = program(g, vs_full, fs_cdown);
    g->prog_disp = program(g, vs_full, fs_disp);
    g->prog_c2d = program(g, vs_full, fs_c2d);
    g->prog_d2c = program(g, vs_full, fs_d2c);
    for (int i = 0; i < UD_N; i++) g->ud[i] = g->glGetUniformLocation(g->prog_draw, ud_name[i]);
    use_prog(g, g->prog_draw);
    g->glUniform1i(g->ud[UD_TEX], 0);
    if (g->rs > 1) rq_log("rq_gl: internal resolution x%d", g->rs);
    g->glGenVertexArrays(1, &g->vao);
    g->glBindVertexArray(g->vao);
    g->glGenBuffers(1, &g->vbo);
    g->glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    {
        GLbitfield fl = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        GLsizeiptr bytes = (GLsizeiptr)RING_SEG_V * RING_SEGS * sizeof(RqHwVert);
        g->glBufferStorage(GL_ARRAY_BUFFER, bytes, 0, fl);
        g->ring = (RqHwVert *)g->glMapBufferRange(GL_ARRAY_BUFFER, 0, bytes, fl);
        if (!g->ring) gl_fatal("persistent vertex buffer mapping failed", 0);
    }
    g->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RqHwVert), (void *)offsetof(RqHwVert, x));
    g->glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(RqHwVert), (void *)offsetof(RqHwVert, z));
    g->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(RqHwVert), (void *)offsetof(RqHwVert, s));
    g->glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(RqHwVert), (void *)offsetof(RqHwVert, r));
    g->glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RqHwVert), (void *)offsetof(RqHwVert, fog));
    for (int i = 0; i < 5; i++) g->glEnableVertexAttribArray(i);
    g->glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    g->glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
    g->glEnable(GL_DEPTH_CLAMP);
    g->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    g->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    g->glGenFramebuffers(1, &g->draw_fbo);
    g->glGenFramebuffers(1, &g->util_fbo);
    g->zup_tex = new_tex(g, GL_R32UI, 2048, 1024);
    g->zdown_tex = new_tex(g, GL_R32UI, 2048, 1024);
    g->zdown_fbo = fbo_for(g, g->zdown_tex, 0);
    if (g->rs > 1) {
        g->down_tex = new_tex(g, GL_RGBA8, 2048, 1024);
        g->down_fbo = fbo_for(g, g->down_tex, 0);
        g->up_tex = new_tex(g, GL_RGBA8, 2048, 1024);
        g->up_fbo = fbo_for(g, g->up_tex, 0);
    }
    g->circ_tex[0] = new_tex(g, GL_RGBA8, 2048, 1024);
    g->circ_tex[1] = new_tex(g, GL_RGBA8, 2048, 1024);
}

// ---------------------------------------------------------------- RqBackendOps

static int gl_be_init(void *be, const RqOptions *o)   // no GL calls here: the context may not exist yet
{
    Gl *g = (Gl *)be;
    g->rs = o->scale < 1 ? 1 : o->scale > 4 ? 4 : o->scale;
    g->aa = o->aa_resolve == 2 && g->rs % 2 == 0 ? 2 : 1;
    g->att_color = g->att_depth = ~0u;
    return 1;
}

static void gl_shutdown(void *be) { (void)be; }   // process exit; the context goes with it

static void gl_enter(void *be)
{
    Gl *g = (Gl *)be;
    if (!g->loaded) gl_load(g);
    g->glGetIntegerv(GL_CURRENT_PROGRAM, &g->sv_prog);
    g->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &g->sv_vao);
    g->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &g->sv_abuf);
    g->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &g->sv_dfb);
    g->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &g->sv_rfb);
    g->glGetIntegerv(GL_ACTIVE_TEXTURE, &g->sv_active);
    for (int u = 0; u < 2; u++) { g->glActiveTexture(GL_TEXTURE0 + u); g->glGetIntegerv(GL_TEXTURE_BINDING_2D, &g->sv_tex[u]); }
    g->glActiveTexture(GL_TEXTURE0);
    g->glGetIntegerv(GL_PACK_ROW_LENGTH, &g->sv_pack);
    g->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &g->sv_unpack);
    g->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    g->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g->cur_prog = ~0u;   // the host may have switched programs since the last leave
    if (!g->ready) gl_init(g);
    g->glBindVertexArray(g->vao);
    g->glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
}

static void gl_leave(void *be)
{
    Gl *g = (Gl *)be;
    g->glUseProgram((GLuint)g->sv_prog);
    g->glBindVertexArray((GLuint)g->sv_vao);
    g->glBindBuffer(GL_ARRAY_BUFFER, (GLuint)g->sv_abuf);
    if (!g->keep_fb) {
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)g->sv_dfb);
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)g->sv_rfb);
    }
    g->keep_fb = 0;
    for (int u = 0; u < 2; u++) { g->glActiveTexture(GL_TEXTURE0 + u); g->glBindTexture(GL_TEXTURE_2D, (GLuint)g->sv_tex[u]); }
    g->glActiveTexture((GLenum)g->sv_active);
    g->glPixelStorei(GL_PACK_ROW_LENGTH, g->sv_pack);
    g->glPixelStorei(GL_UNPACK_ROW_LENGTH, g->sv_unpack);
    g->glColorMask(1, 1, 1, 1);
    g->glDepthMask(1);
    g->glFlush();   // submit what the consumer queued: the GPU renders while the game builds the rest of the frame
}

static uint32_t gl_tex_create(void *be, int w, int h) { return new_tex((Gl *)be, GL_RGBA8, w, h); }

static void gl_tex_destroy(void *be, uint32_t t)
{
    Gl *g = (Gl *)be;
    GLuint n = t;
    if (n == g->att_color || n == g->att_depth) g->att_color = g->att_depth = ~0u;
    g->glDeleteTextures(1, &n);
}

static void gl_tex_update(void *be, uint32_t t, int x, int y, int w, int h, const uint32_t *rgba, int stride)
{
    Gl *g = (Gl *)be;
    g->glBindTexture(GL_TEXTURE_2D, t);
    if (stride != w) g->glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    g->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (stride != w) g->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void pf_free(Gl *g, uint32_t t);

static uint32_t gl_target_create(void *be, int depth, int w, int h)
{
    Gl *g = (Gl *)be;
    int i = 0;
    while (i < MAXT && g->tg[i].tex) i++;
    if (i == MAXT) gl_fatal("out of target slots", 0);
    g->tg[i].depth = depth; g->tg[i].w = w; g->tg[i].h = h;
    g->tg[i].tex = new_tex(g, depth ? GL_DEPTH_COMPONENT32F : GL_RGBA8, w * g->rs, h * g->rs);
    g->tg[i].fbo = depth ? fbo_for(g, 0, g->tg[i].tex) : fbo_for(g, g->tg[i].tex, 0);
    return (uint32_t)i + 1;
}

static void gl_target_destroy(void *be, uint32_t t)
{
    Gl *g = (Gl *)be;
    if (!t || t > MAXT) return;
    auto &s = g->tg[t - 1];
    if (s.tex == g->att_color || s.tex == g->att_depth) g->att_color = g->att_depth = ~0u;
    pf_free(g, t);
    if (g->cur_c == t) g->cur_c = 0;
    if (g->cur_d == t) g->cur_d = 0;
    g->glDeleteTextures(1, &s.tex);
    g->glDeleteFramebuffers(1, &s.fbo);
    memset(&s, 0, sizeof s);
}

/* VRAM -> GPU, columns [x, x + w) of rows [y0, y1) (gs_hw.c surf_upload; rq_tex upload_push: single pages) */
static void gl_target_upload(void *be, uint32_t t, int x, int y0, int y1, int w, const uint32_t *px)
{
    Gl *g = (Gl *)be;
    auto &s = g->tg[t - 1];
    s.gen++;
    int rs = g->rs;
    if (!s.depth) {
        if (rs == 1) {
            g->glBindTexture(GL_TEXTURE_2D, s.tex);
            g->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y0, w, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            return;
        }
        g->glBindTexture(GL_TEXTURE_2D, g->up_tex);
        g->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y0, w, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->up_fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbo);
        g->glDisable(GL_SCISSOR_TEST);
        g->glColorMask(1, 1, 1, 1);
        g->glBlitFramebuffer(x, y0, x + w, y1, x * rs, y0 * rs, (x + w) * rs, y1 * rs, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        return;
    }
    g->glBindTexture(GL_TEXTURE_2D, g->zup_tex);
    g->glTexSubImage2D(GL_TEXTURE_2D, 0, x, y0, w, y1 - y0, GL_RED_INTEGER, GL_UNSIGNED_INT, px);
    g->glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    g->glViewport(0, 0, s.w * rs, s.h * rs);
    g->glEnable(GL_SCISSOR_TEST);
    g->glScissor(x * rs, y0 * rs, w * rs, (y1 - y0) * rs);
    g->glDisable(GL_BLEND);
    g->glEnable(GL_DEPTH_TEST);
    g->glDepthFunc(GL_ALWAYS);
    g->glDepthMask(1);
    use_prog(g, g->prog_zup);
    g->glActiveTexture(GL_TEXTURE0);
    g->glBindTexture(GL_TEXTURE_2D, g->zup_tex);
    g->glUniform1i(U(g, g->prog_zup, "u_src"), 0);
    g->glUniform1i(U(g, g->prog_zup, "u_n"), rs);
    g->glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* GPU -> rows [y0, y1) of width w, 1x (gs_hw.c surf_download: depth through R32UI, scaled colour through a blit):
 * the resolve; leaves the 1x source bound as the read framebuffer */
static void dl_resolve(Gl *g, uint32_t t, int y0, int y1, int w)
{
    auto &s = g->tg[t - 1];
    int rs = g->rs;
    rq_gpu_seg(1);   // host GPU timing: the resolve / downscale; the readback itself counts as wait
    if (s.depth) {   // depth -> R32UI, then read the rows from there
        g->glBindFramebuffer(GL_FRAMEBUFFER, g->zdown_fbo);
        g->glViewport(0, 0, 2048, 1024);
        g->glDisable(GL_SCISSOR_TEST); g->glDisable(GL_DEPTH_TEST); g->glDisable(GL_BLEND);
        g->glColorMask(1, 1, 1, 1);
        use_prog(g, g->prog_zdown);
        g->glActiveTexture(GL_TEXTURE0);
        g->glBindTexture(GL_TEXTURE_2D, s.tex);
        g->glUniform1i(U(g, g->prog_zdown, "u_src"), 0);
        g->glUniform1i(U(g, g->prog_zdown, "u_n"), rs);
        g->glDrawArrays(GL_TRIANGLES, 0, 3);
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->zdown_fbo);
    } else if (rs > 1) {   // scaled colour -> 1x (sub-pixel 0 per GS pixel, fs_cdown)
        g->glBindFramebuffer(GL_FRAMEBUFFER, g->down_fbo);
        g->glViewport(0, 0, 2048, 1024);
        g->glEnable(GL_SCISSOR_TEST); g->glScissor(0, y0, w, y1 - y0);   // the rows read, as the old blit
        g->glDisable(GL_DEPTH_TEST); g->glDisable(GL_BLEND);
        g->glColorMask(1, 1, 1, 1);
        use_prog(g, g->prog_cdown);
        g->glActiveTexture(GL_TEXTURE0);
        g->glBindTexture(GL_TEXTURE_2D, s.tex);
        g->glUniform1i(U(g, g->prog_cdown, "u_src"), 0);
        g->glUniform1i(U(g, g->prog_cdown, "u_n"), rs);
        g->glDrawArrays(GL_TRIANGLES, 0, 3);
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->down_fbo);
    } else g->glBindFramebuffer(GL_READ_FRAMEBUFFER, s.fbo);
    rq_gpu_seg(0);
}

static void read_rows(Gl *g, uint32_t t, int y0, int y1, int w, void *dst)
{
    int d = g->tg[t - 1].depth;
    g->glReadPixels(0, y0, w, y1 - y0, d ? GL_RED_INTEGER : GL_RGBA, d ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE, dst);
}

/* Async readbacks. A target the CPU reads back (rq_tex surf_download) is usually drawn, left for other targets and
 * read some draw phases later, the same way every frame. Each read learns k = how often the target was left since
 * the previous read (and its rows); when it is left for the k-th time again, the same rows are resolved and read
 * into a persistently mapped PBO behind a fence, so the GPU does the copy while the consumer goes on. The next read
 * takes its rows from there if no GPU write reached the target in between (gen), else it reads synchronously as
 * before. Same resolve passes, same texels: bytes identical either way. Measured s78 (NVIDIA): glReadPixels into
 * the PBO still blocks (driver thread catch-up), so the win is only that the stall moves to the target switch;
 * reads of the target still being drawn (rq_tex format aliasing, ~60% in battle) cannot prefetch. */
static void pf_issue(Gl *g, uint32_t t)
{
    auto &s = g->tg[t - 1];
    auto &p = s.pf;
    int y0 = s.dl_y0, y1 = s.dl_y1, w = s.dl_w;
    if (p.fence) { g->glDeleteSync(p.fence); p.fence = 0; }   // an unread earlier one: superseded (GL orders the writes)
    if (!p.pbo) {   // the whole target at 1x, client-side storage: the CPU reads it
        GLbitfield fl = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        GLsizeiptr bytes = (GLsizeiptr)s.w * s.h * 4;
        g->glGenBuffers(1, &p.pbo);
        g->glBindBuffer(GL_PIXEL_PACK_BUFFER, p.pbo);
        g->glBufferStorage(GL_PIXEL_PACK_BUFFER, bytes, 0, fl | GL_CLIENT_STORAGE_BIT);
        p.map = (uint8_t *)g->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, fl);
        if (!p.map) gl_fatal("readback buffer mapping failed", 0);
    } else g->glBindBuffer(GL_PIXEL_PACK_BUFFER, p.pbo);
    dl_resolve(g, t, y0, y1, w);
    uint64_t t0 = rq_clock_ns();
    read_rows(g, t, y0, y1, w, (void *)0);   // blocks on this NVIDIA driver too (driver thread catch-up): counted as wait
    uint64_t dt = rq_clock_ns() - t0;
    rq_gpu_wait_ns(dt);
    g->w_issue += dt;
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    p.fence = g->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    p.gen = s.gen; p.y0 = y0; p.y1 = y1; p.w = w; p.live = 1;
    g->n_pf++;
}

static void pf_free(Gl *g, uint32_t t)
{
    auto &p = g->tg[t - 1].pf;
    if (p.fence) g->glDeleteSync(p.fence);
    if (p.pbo) g->glDeleteBuffers(1, &p.pbo);   // unmaps it
    memset(&p, 0, sizeof p);
}

/* target t is no longer drawn to (the draws moved to other targets) */
static void pf_away(Gl *g, uint32_t t)
{
    if (!t) return;
    auto &s = g->tg[t - 1];
    if (++s.sw == s.k) pf_issue(g, t);
}

static void gl_target_download(void *be, uint32_t t, int y0, int y1, int w, uint32_t *px)
{
    Gl *g = (Gl *)be;
    auto &s = g->tg[t - 1];
    auto &p = s.pf;
    if (p.live && p.gen == s.gen && w == p.w && y0 >= p.y0 && y1 <= p.y1) {
        if (p.fence) {
            uint64_t t0 = rq_clock_ns();
            g->glClientWaitSync(p.fence, GL_SYNC_FLUSH_COMMANDS_BIT, ~0ull);
            uint64_t dt = rq_clock_ns() - t0;
            rq_gpu_wait_ns(dt);
            g->w_pf += dt;
            g->glDeleteSync(p.fence);
            p.fence = 0;
        }
        memcpy(px, p.map + (size_t)(y0 - p.y0) * w * 4, (size_t)w * (y1 - y0) * 4);
        g->n_hit++;
    } else {
        if (p.live && p.gen != s.gen) g->n_stale++;
        if (t == g->cur_c || t == g->cur_d) g->n_cur++;
        dl_resolve(g, t, y0, y1, w);
        uint64_t t0 = rq_clock_ns();
        read_rows(g, t, y0, y1, w, px);
        uint64_t dt = rq_clock_ns() - t0;
        rq_gpu_wait_ns(dt);
        g->w_sync += dt;
        g->n_sync++;
    }
    // learn the pattern: rows read with no GPU write in between join one range
    if (s.dl_gen == s.gen && s.dl_w == w) {
        if (y0 < s.dl_y0) s.dl_y0 = y0;
        if (y1 > s.dl_y1) s.dl_y1 = y1;
    } else {
        s.dl_gen = s.gen; s.dl_y0 = y0; s.dl_y1 = y1; s.dl_w = w;
        s.k = (t == g->cur_c || t == g->cur_d) ? 0 : s.sw;   // read while still drawn to: no prefetch point
        s.sw = 0;
    }
}

static int gl_summary(void *be, char *buf, int n)
{
    Gl *g = (Gl *)be;
    return snprintf(buf, n, "rq_gl waits: readback sync %.0f ms (%u, %u of the target being drawn), prefetch issue %.0f ms + fence %.0f ms (%u issued, %u hit, %u stale), ring %.0f ms\n",
                    g->w_sync * 1e-6, g->n_sync, g->n_cur, g->w_issue * 1e-6, g->w_pf * 1e-6, g->n_pf, g->n_hit, g->n_stale, g->w_ring * 1e-6);
}

/* compatible targets: rows [y0, y1) of width w from src into dst (gs_hw.c gpu_copy, one run) */
static void gl_target_copy(void *be, uint32_t dt, uint32_t st, int y0, int y1, int w, uint32_t zmask, int keep_a)
{
    Gl *g = (Gl *)be;
    auto &dst = g->tg[dt - 1];
    auto &src = g->tg[st - 1];
    int rs = g->rs;
    dst.gen++;
    if (src.depth == dst.depth) {
        g->glCopyImageSubData(src.tex, GL_TEXTURE_2D, 0, 0, y0 * rs, 0, dst.tex, GL_TEXTURE_2D, 0, 0, y0 * rs, 0,
                              w * rs, (y1 - y0) * rs, 1);
        return;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    g->glViewport(0, 0, dst.w * rs, dst.h * rs);
    g->glEnable(GL_SCISSOR_TEST);
    g->glScissor(0, y0 * rs, w * rs, (y1 - y0) * rs);
    g->glDisable(GL_BLEND);
    g->glActiveTexture(GL_TEXTURE0);
    g->glBindTexture(GL_TEXTURE_2D, src.tex);
    if (dst.depth) {
        g->glEnable(GL_DEPTH_TEST); g->glDepthFunc(GL_ALWAYS); g->glDepthMask(1);
        use_prog(g, g->prog_c2d);
        g->glUniform1i(U(g, g->prog_c2d, "u_src"), 0);
        g->glUniform1ui(U(g, g->prog_c2d, "u_zmask"), zmask);
    } else {
        g->glDisable(GL_DEPTH_TEST);
        g->glColorMask(1, 1, 1, !keep_a);   // Z24: the top byte stays as uploaded
        use_prog(g, g->prog_d2c);
        g->glUniform1i(U(g, g->prog_d2c, "u_src"), 0);
    }
    g->glDrawArrays(GL_TRIANGLES, 0, 3);
    g->glColorMask(1, 1, 1, 1);
}

/* feedback: a snapshot of the target being drawn, returned as a texture id */
static uint32_t gl_target_snapshot(void *be, uint32_t t, int w, int vh)
{
    Gl *g = (Gl *)be;
    auto &s = g->tg[t - 1];
    int rs = g->rs;
    if (g->tmp_w < w * rs || g->tmp_h < vh * rs) {
        if (g->tmp_tex) gl_tex_destroy(g, g->tmp_tex);
        g->tmp_w = 2048 * rs; g->tmp_h = 1024 * rs;
        g->tmp_tex = new_tex(g, GL_RGBA8, g->tmp_w, g->tmp_h);
    }
    g->glCopyImageSubData(s.tex, GL_TEXTURE_2D, 0, 0, 0, 0, g->tmp_tex, GL_TEXTURE_2D, 0, 0, 0, 0, w * rs, vh * rs, 1);
    return g->tmp_tex;
}

static void ring_enter(Gl *g, int s)
{
    g->ring_fence[g->ring_seg] = g->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g->ring_seg = s;
    if (g->ring_fence[s]) {
        uint64_t t0 = rq_clock_ns();
        g->glClientWaitSync(g->ring_fence[s], GL_SYNC_FLUSH_COMMANDS_BIT, ~0ull);
        uint64_t dt = rq_clock_ns() - t0;
        rq_gpu_wait_ns(dt);
        g->w_ring += dt;
        g->glDeleteSync(g->ring_fence[s]);
        g->ring_fence[s] = 0;
    }
}

/* start a batch at ring_pos (the next segment if less than 4096 vertices are left in this one); a batch never crosses
 * a segment boundary, so a segment's fence, created when the next one is entered, covers every draw that read it */
static RqHwVert *gl_vtx_alloc(void *be, uint32_t max, uint32_t *first, uint32_t *cap)
{
    Gl *g = (Gl *)be;
    uint32_t room = (uint32_t)(g->ring_seg + 1) * RING_SEG_V - g->ring_pos;
    if (room < 4096) {
        int s = (g->ring_seg + 1) % RING_SEGS;
        ring_enter(g, s);
        g->ring_pos = (uint32_t)s * RING_SEG_V;
        room = RING_SEG_V;
    }
    *first = g->ring_pos;
    *cap = room < max ? room : max;
    return g->ring + g->ring_pos;
}

/* Blend equation (A-B)*C>>7 + D  ->  GL fixed function + shader premultiply of Cs (gs_hw.c blend_setup). */
static void blend_setup(Gl *g, const RqBatch *k, int *kmode, int *csel, float *bias)
{
    *bias = 0;
    int abe = k->abe;
    int A = k->ba, B = k->bb, C = k->bc, D = k->bd;
    *csel = C == 3 ? 2 : C;
    if (!abe || (A == B && D == 0)) { g->glDisable(GL_BLEND); *kmode = 1; return; }
    if (A == B && D == 2) { g->glDisable(GL_BLEND); *kmode = 0; return; }
    int cs_one = D == 0, cs_f = (A == 0) - (B == 0);
    int cd_one = D == 1, cd_f = (A == 1) - (B == 1);
    *kmode = !cs_one ? (cs_f ? 2 : 0) : cs_f > 0 ? 3 : cs_f < 0 ? 4 : 1;
    GLenum F = C == 1 ? GL_DST_ALPHA : GL_SRC1_ALPHA, F1 = C == 1 ? GL_ONE_MINUS_DST_ALPHA : GL_ONE_MINUS_SRC1_ALPHA;
    if (C == 1 && !g->warned_ad++) rq_log("rq_gl: blend with Ad as factor approximated (Ad/255 instead of Ad/128)");
    GLenum df = !cd_one ? (cd_f ? F : GL_ZERO) : cd_f < 0 ? F1 : GL_ONE;
    if (cd_one && cd_f > 0 && !g->warned_cd++) rq_log("rq_gl: blend Cd*(1+f) approximated");
    int neg_s = !cs_one && cs_f < 0, neg_d = !cd_one && cd_f < 0;
    GLenum eq = GL_FUNC_ADD;
    if (neg_s && neg_d) { *kmode = 0; df = GL_ZERO; }
    else if (neg_s) eq = GL_FUNC_REVERSE_SUBTRACT;
    else if (neg_d) eq = GL_FUNC_SUBTRACT;
    /* GS truncates the (A-B)*C>>7 term, GL rounds. When the product is all on the source side (Cs*f + Cd
     * and friends) a source bias turns the rounding into a floor. Mixes (Cd*(1-f) on the destination side)
     * come out closest without one (measured on the title feedback blur: mean error -0.4 vs +2.6/-2.2). */
    if (cs_f && !cd_f) *bias = eq == GL_FUNC_REVERSE_SUBTRACT ? 0.499f : -0.499f;
    g->glEnable(GL_BLEND);
    g->glBlendEquationSeparate(eq, GL_FUNC_ADD);
    g->glBlendFuncSeparate(GL_ONE, df, GL_ONE, GL_ZERO);
}

/* gs_hw.c gs_hw_flush, GL half */
static void gl_draw(void *be, const RqBatch *b)
{
    Gl *g = (Gl *)be;
    int rs = g->rs;
    if (b->color != g->cur_c || b->depth != g->cur_d) {   // draw phase change: the targets left may prefetch
        uint32_t oc = g->cur_c, od = g->cur_d;
        g->cur_c = b->color; g->cur_d = b->depth;
        if (oc != b->color && oc != b->depth) pf_away(g, oc);
        if (od != b->color && od != b->depth && od != oc) pf_away(g, od);
    }
    if (b->color) g->tg[b->color - 1].gen++;
    if (b->depth) g->tg[b->depth - 1].gen++;
    g->glBindFramebuffer(GL_FRAMEBUFFER, g->draw_fbo);
    {
        GLuint ct = b->color ? g->tg[b->color - 1].tex : 0, dt = b->depth ? g->tg[b->depth - 1].tex : 0;
        if (ct != g->att_color) {
            g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
            if (!ct != !g->att_color || g->att_color == ~0u) { GLenum db = ct ? GL_COLOR_ATTACHMENT0 : GL_NONE; g->glDrawBuffers(1, &db); }
            g->att_color = ct;
        }
        if (dt != g->att_depth) { g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dt, 0); g->att_depth = dt; }
    }
    int W = b->tw, H = b->th;
    g->glViewport(0, 0, W * rs, H * rs);
    g->glEnable(GL_SCISSOR_TEST);
    g->glScissor(b->sx0 * rs, b->sy0 * rs, (b->sx1 - b->sx0 + 1) * rs, (b->sy1 - b->sy0 + 1) * rs);

    use_prog(g, g->prog_draw);
    const float eps = (0.5f - 1.0f / 512) / rs;   // GS samples at pixel corners, GL at centres (of scaled pixels)
    ud_4f(g, UD_NDC, 2.0f / W, 2.0f / H, eps * 2.0f / W - 1.0f, eps * 2.0f / H - 1.0f);
    ud_1ui(g, UD_ZMAX, b->zmax);
    { int v[4] = {b->snap}; if (!ud_same(g, UD_SNAP, v)) g->glUniform1i(g->ud[UD_SNAP], v[0]); }
    ud_4i(g, UD_F0, b->tme, b->fst, b->iip, b->fge);
    if (b->tme) {
        ud_4i(g, UD_F1, b->tfx, b->tcc, b->amode, b->linear);
        ud_4i(g, UD_TSZ, b->tsw, b->tsh, b->glw, b->glh);
        ud_4i(g, UD_WRAP, b->wms, b->wmt, b->aniso, 0);
        ud_4i(g, UD_REG, b->minu, b->maxu, b->minv, b->maxv);
        ud_4i(g, UD_TEXA, b->ta0, b->ta1, b->aem, 0);
        { int v[4] = {b->tex_rs}; if (!ud_same(g, UD_RS, v)) g->glUniform1i(g->ud[UD_RS], v[0]); }
        g->glActiveTexture(GL_TEXTURE0);
        g->glBindTexture(GL_TEXTURE_2D, b->tex_is_target ? g->tg[b->tex - 1].tex : b->tex);
    }
    uint32_t fc = b->fogcol;
    {
        float v[4] = {(float)(fc & 255), (float)((fc >> 8) & 255), (float)((fc >> 16) & 255), 0};
        if (!ud_same(g, UD_FOGC, v)) g->glUniform3f(g->ud[UD_FOGC], v[0], v[1], v[2]);
    }
    int kmode, csel;
    float bias;
    blend_setup(g, b, &kmode, &csel, &bias);
    { float v[4] = {bias}; if (!ud_same(g, UD_BIAS, v)) g->glUniform1f(g->ud[UD_BIAS], bias); }
    ud_4i(g, UD_BL, csel, kmode, b->fba, b->fix);

    g->glBindVertexArray(g->vao);
    g->ring_pos = b->first + b->n;   // the batch was built in place
    GLenum mode = b->prim == 0 ? GL_POINTS : b->prim == 1 ? GL_LINES : GL_TRIANGLES;
    if (b->depth) { g->glEnable(GL_DEPTH_TEST); static const GLenum zf[4] = {GL_NEVER, GL_ALWAYS, GL_GEQUAL, GL_GREATER}; g->glDepthFunc(zf[b->ztst]); }
    else g->glDisable(GL_DEPTH_TEST);
    int zw = b->depth && !b->zmsk;

    // pass 1: fragments passing the alpha test (or all, without one)
    int passes[2], np = 0;
    if (!b->ate) passes[np++] = 0;
    else {
        if (b->atst != 0) passes[np++] = 1;    // passing
        if (b->afail != 0) passes[np++] = 2;   // failing, with the AFAIL write mask
    }
    for (int i = 0; i < np; i++) {
        int pass = passes[i];
        int cm[4] = {b->cmask[0], b->cmask[1], b->cmask[2], b->cmask[3]}, dw = zw;
        if (pass == 2) {
            if (b->afail == 1) dw = 0;                                 // FB_ONLY
            else if (b->afail == 2) cm[0] = cm[1] = cm[2] = cm[3] = 0; // ZB_ONLY
            else { cm[3] = 0; dw = 0; }                                // RGB_ONLY
        }
        g->glColorMask(cm[0], cm[1], cm[2], cm[3]);
        g->glDepthMask(dw);
        ud_4i(g, UD_AT, pass != 0, b->atst, b->aref, pass == 1 ? 0 : 1);
        g->glDrawArrays(mode, (GLint)b->first, (GLsizei)b->n);
    }
    g->glColorMask(1, 1, 1, 1);
    g->glDepthMask(1);
}

/* gs_hw.c gs_hw_display, GL half: composite both read circuits + merge into disp_tex. The call order is kept as is
 * (a decoded circuit 1 is uploaded through the active unit 0 before unit 1 is selected). */
static int gl_display(void *be, const RqDisplayIn *d, int *pw, int *ph)
{
    Gl *g = (Gl *)be;
    int rs = g->rs, w = d->w, h = d->h;
    GLuint p = g->prog_disp;
    use_prog(g, p);
    int sc[2] = {1, 1};
    if (g->disp_w < w * rs || g->disp_h < h * rs) {
        if (g->disp_tex) { gl_tex_destroy(g, g->disp_tex); g->glDeleteFramebuffers(1, &g->disp_fbo); }
        g->disp_w = 1024 * rs; g->disp_h = 1024 * rs;
        g->disp_tex = new_tex(g, GL_RGBA8, g->disp_w, g->disp_h);
        g->disp_fbo = fbo_for(g, g->disp_tex, 0);
        use_prog(g, p);
    }
    for (int k = 0; k < 2; k++) {
        GLuint tex = g->circ_tex[k];
        int sx = 0, sy = 0, cls = 0;
        if (d->en[k]) {
            cls = d->c[k].cls;
            if (d->c[k].target) {
                tex = g->tg[d->c[k].target - 1].tex; sx = d->c[k].sx; sy = d->c[k].sy; sc[k] = rs;
            } else {
                g->glBindTexture(GL_TEXTURE_2D, tex);
                g->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, d->c[k].rw, d->c[k].rh, GL_RGBA, GL_UNSIGNED_BYTE, d->c[k].rgba);
            }
        }
        g->glActiveTexture(GL_TEXTURE0 + k);
        g->glBindTexture(GL_TEXTURE_2D, tex);
        g->glUniform1i(U(g, p, k ? "u_t1" : "u_t0"), k);
        g->glUniform4i(U(g, p, k ? "u_c1" : "u_c0"), d->c[k].ox, d->c[k].oy, 0, 0);
        g->glUniform4i(U(g, p, k ? "u_s1" : "u_s0"), sx, sy, cls, d->en[k]);
    }
    g->glActiveTexture(GL_TEXTURE0);
    g->glUniform2i(U(g, p, "u_sc"), sc[0], sc[1]);
    g->glUniform1i(U(g, p, "u_n"), rs);
    uint32_t bg = d->bg;
    g->glUniform3f(U(g, p, "u_bg"), (bg & 0xff) / 255.0f, ((bg >> 8) & 0xff) / 255.0f, ((bg >> 16) & 0xff) / 255.0f);
    g->glUniform4i(U(g, p, "u_m"), d->mmod, d->slbg, d->alp, 0);
    g->glBindFramebuffer(GL_FRAMEBUFFER, g->disp_fbo);
    g->glViewport(0, 0, w * rs, h * rs);
    g->glDisable(GL_SCISSOR_TEST); g->glDisable(GL_DEPTH_TEST); g->glDisable(GL_BLEND);
    g->glColorMask(1, 1, 1, 1);
    g->glDrawArrays(GL_TRIANGLES, 0, 3);
    *pw = w * rs; *ph = h * rs;   // disp_tex holds the picture at the internal resolution
    g->out_fbo = g->disp_fbo;
    if (g->aa == 2) {   // step 5 video.aa = 2x: a linear blit to exactly half size = the 2x2 box average
        int ow = w * rs / 2, oh = h * rs / 2;
        if (g->aa_w < ow || g->aa_h < oh) {
            if (g->aa_tex) { gl_tex_destroy(g, g->aa_tex); g->glDeleteFramebuffers(1, &g->aa_fbo); }
            g->aa_w = g->disp_w / 2; g->aa_h = g->disp_h / 2;
            g->aa_tex = new_tex(g, GL_RGBA8, g->aa_w, g->aa_h);
            g->aa_fbo = fbo_for(g, g->aa_tex, 0);
        }
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->disp_fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->aa_fbo);
        g->glBlitFramebuffer(0, 0, ow * 2, oh * 2, 0, 0, ow, oh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        g->glBindFramebuffer(GL_FRAMEBUFFER, g->disp_fbo);
        g->out_fbo = g->aa_fbo;
        *pw = ow; *ph = oh;
    }
    return 1;
}

/* disp_tex -> BGRA buffer (rows bottom first, as gs_hw_read_display) */
static void gl_read_output(void *be, uint32_t *bgra, int w, int h, int stride)
{
    Gl *g = (Gl *)be;
    if (!g->out_fbo) return;
    g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->out_fbo);
    g->glPixelStorei(GL_PACK_ROW_LENGTH, stride);
    uint64_t t0 = rq_clock_ns();
    g->glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    rq_gpu_wait_ns(rq_clock_ns() - t0);
    g->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

/* Stretch disp_tex (w x h) into (dx, dy, dw, dh) (top-left origin) of the default framebuffer (cw x ch), flipping
 * to top-down; the rest is cleared to black. Leaves the default framebuffer bound (the host draws its HUD next). */
static void gl_blit_output(void *be, int w, int h, int dx, int dy, int dw, int dh, int cw, int ch, int nearest)
{
    Gl *g = (Gl *)be;
    if (!g->out_fbo) return;
    g->glBindFramebuffer(GL_READ_FRAMEBUFFER, g->out_fbo);
    g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g->glDisable(GL_SCISSOR_TEST);
    g->glColorMask(1, 1, 1, 1);
    if (dw != cw || dh != ch) {
        g->glClearColor(0, 0, 0, 1);
        g->glClear(GL_COLOR_BUFFER_BIT);
    }
    int y0 = ch - dy - dh;   // GL rows count from the bottom
    g->glBlitFramebuffer(0, 0, w, h, dx, y0 + dh, dx + dw, y0, GL_COLOR_BUFFER_BIT, nearest ? GL_NEAREST : GL_LINEAR);
    g->keep_fb = 1;
}

// host GPU timing (rq_host.h rq_gpu_seg): timestamp segments around the ops that are GPU work with little CPU work
// inside (uploads are left out: their CPU copy would count as GPU time)
static void ts_target_copy(void *be, uint32_t dt, uint32_t st, int y0, int y1, int w, uint32_t zmask, int keep_a)
{ rq_gpu_seg(1); gl_target_copy(be, dt, st, y0, y1, w, zmask, keep_a); rq_gpu_seg(0); }
static uint32_t ts_target_snapshot(void *be, uint32_t t, int w, int vh)
{ rq_gpu_seg(1); uint32_t r = gl_target_snapshot(be, t, w, vh); rq_gpu_seg(0); return r; }
static void ts_draw(void *be, const RqBatch *b) { rq_gpu_seg(1); gl_draw(be, b); rq_gpu_seg(0); }
static int ts_display(void *be, const RqDisplayIn *d, int *pw, int *ph)
{ rq_gpu_seg(1); int r = gl_display(be, d, pw, ph); rq_gpu_seg(0); return r; }
static void ts_blit_output(void *be, int w, int h, int dx, int dy, int dw, int dh, int cw, int ch, int nearest)
{ rq_gpu_seg(1); gl_blit_output(be, w, h, dx, dy, dw, dh, cw, ch, nearest); rq_gpu_seg(0); }

extern "C" const RqBackendOps rq_backend_gl = {
    gl_be_init, gl_shutdown, gl_enter, gl_leave,
    gl_tex_create, gl_tex_destroy, gl_tex_update,
    gl_target_create, gl_target_destroy, gl_target_upload, gl_target_download, ts_target_copy, ts_target_snapshot,
    gl_vtx_alloc, ts_draw, ts_display, gl_read_output, ts_blit_output, gl_summary,
    (uint32_t)sizeof(Gl),
};
