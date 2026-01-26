/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_composite.h"
#include "crt.h"
#include "shader_advanced.h"
#include "shader_base.h"
#include "shader_bloom.h"

typedef GLuint (APIENTRYP glc_create_shader_fn)(GLenum);
typedef void (APIENTRYP glc_shader_source_fn)(GLuint, GLsizei, const GLchar *const*, const GLint*);
typedef void (APIENTRYP glc_compile_shader_fn)(GLuint);
typedef void (APIENTRYP glc_get_shader_iv_fn)(GLuint, GLenum, GLint*);
typedef void (APIENTRYP glc_get_shader_info_log_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRYP glc_delete_shader_fn)(GLuint);
typedef GLuint (APIENTRYP glc_create_program_fn)(void);
typedef void (APIENTRYP glc_attach_shader_fn)(GLuint, GLuint);
typedef void (APIENTRYP glc_link_program_fn)(GLuint);
typedef void (APIENTRYP glc_get_program_iv_fn)(GLuint, GLenum, GLint*);
typedef void (APIENTRYP glc_get_program_info_log_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRYP glc_delete_program_fn)(GLuint);
typedef GLint (APIENTRYP glc_get_uniform_location_fn)(GLuint, const GLchar*);
typedef void (APIENTRYP glc_use_program_fn)(GLuint);
typedef void (APIENTRYP glc_uniform_1f_fn)(GLint, GLfloat);
typedef void (APIENTRYP glc_uniform_1i_fn)(GLint, GLint);
typedef void (APIENTRYP glc_uniform_2f_fn)(GLint, GLfloat, GLfloat);
typedef void (APIENTRYP glc_active_texture_fn)(GLenum);
typedef void (APIENTRYP glc_gen_framebuffers_fn)(GLsizei, GLuint*);
typedef void (APIENTRYP glc_delete_framebuffers_fn)(GLsizei, const GLuint*);
typedef void (APIENTRYP glc_bind_framebuffer_fn)(GLenum, GLuint);
typedef void (APIENTRYP glc_framebuffer_texture_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRYP glc_check_framebuffer_fn)(GLenum);
typedef void (APIENTRYP glc_draw_buffer_fn)(GLenum);
typedef void (APIENTRYP glc_read_buffer_fn)(GLenum);

static glc_create_shader_fn glc_glCreateShader = NULL;
static glc_shader_source_fn glc_glShaderSource = NULL;
static glc_compile_shader_fn glc_glCompileShader = NULL;
static glc_get_shader_iv_fn glc_glGetShaderiv = NULL;
static glc_get_shader_info_log_fn glc_glGetShaderInfoLog = NULL;
static glc_delete_shader_fn glc_glDeleteShader = NULL;
static glc_create_program_fn glc_glCreateProgram = NULL;
static glc_attach_shader_fn glc_glAttachShader = NULL;
static glc_link_program_fn glc_glLinkProgram = NULL;
static glc_get_program_iv_fn glc_glGetProgramiv = NULL;
static glc_get_program_info_log_fn glc_glGetProgramInfoLog = NULL;
static glc_delete_program_fn glc_glDeleteProgram = NULL;
static glc_get_uniform_location_fn glc_glGetUniformLocation = NULL;
static glc_use_program_fn glc_glUseProgram = NULL;
static glc_uniform_1f_fn glc_glUniform1f = NULL;
static glc_uniform_1i_fn glc_glUniform1i = NULL;
static glc_uniform_2f_fn glc_glUniform2f = NULL;
static glc_active_texture_fn glc_glActiveTexture = NULL;
static glc_gen_framebuffers_fn glc_glGenFramebuffers = NULL;
static glc_delete_framebuffers_fn glc_glDeleteFramebuffers = NULL;
static glc_bind_framebuffer_fn glc_glBindFramebuffer = NULL;
static glc_framebuffer_texture_fn glc_glFramebufferTexture2D = NULL;
static glc_check_framebuffer_fn glc_glCheckFramebufferStatus = NULL;
static glc_draw_buffer_fn glc_glDrawBuffer = NULL;
static glc_read_buffer_fn glc_glReadBuffer = NULL;

static int glc_active = 0;
static SDL_GLContext glc_context = NULL;
static int glc_contextOwned = 0;
static GLuint glc_program_plain = 0;
static GLuint glc_program_crt = 0;
static GLuint glc_program_crt_adv = 0;
static GLuint glc_program_bloom_downsample = 0;
static GLuint glc_program_bloom_blur = 0;
static GLuint glc_program_bloom_composite = 0;
static GLuint glc_tex = 0;
static int glc_texW = 0;
static int glc_texH = 0;
static uint8_t *glc_upload = NULL;
static size_t glc_uploadSize = 0;
static GLint glc_plain_texLoc = -1;
static GLint glc_crt_texLoc = -1;
static GLint glc_crt_texSizeLoc = -1;
static GLint glc_crt_geomLoc = -1;
static GLint glc_crt_scanLoc = -1;
static GLint glc_crt_beamLoc = -1;
static GLint glc_crt_borderLoc = -1;
static GLint glc_crt_overscanLoc = -1;
static GLint glc_crt_adv_texLoc = -1;
static GLint glc_crt_adv_texSizeLoc = -1;
static GLint glc_crt_adv_geomLoc = -1;
static GLint glc_crt_adv_scanLoc = -1;
static GLint glc_crt_adv_beamLoc = -1;
static GLint glc_crt_adv_borderLoc = -1;
static GLint glc_crt_adv_overscanLoc = -1;
static GLint glc_crt_adv_dstSizeLoc = -1;
static GLint glc_crt_adv_dstOffsetLoc = -1;
static GLint glc_crt_adv_gammaLoc = -1;
static GLint glc_crt_adv_chromaLoc = -1;
static GLint glc_crt_adv_scanStrengthLoc = -1;
static GLint glc_crt_adv_maskStrengthLoc = -1;
static GLint glc_crt_adv_maskScaleLoc = -1;
static GLint glc_crt_adv_maskTypeLoc = -1;
static GLint glc_crt_adv_grilleLoc = -1;
static GLint glc_crt_adv_grilleStrengthLoc = -1;
static GLint glc_crt_adv_beamStrengthLoc = -1;
static GLint glc_crt_adv_beamWidthLoc = -1;
static GLint glc_crt_adv_curvatureKLoc = -1;
static GLint glc_bloom_down_texLoc = -1;
static GLint glc_bloom_down_invSrcSizeLoc = -1;
static GLint glc_bloom_down_thresholdLoc = -1;
static GLint glc_bloom_down_kneeLoc = -1;
static GLint glc_bloom_blur_texLoc = -1;
static GLint glc_bloom_blur_stepUvLoc = -1;
static GLint glc_bloom_comp_baseLoc = -1;
static GLint glc_bloom_comp_bloomLoc = -1;
static GLint glc_bloom_comp_strengthLoc = -1;

static GLuint glc_bloomFbo = 0;
static GLuint glc_bloomSceneTex = 0;
static int glc_bloomSceneW = 0;
static int glc_bloomSceneH = 0;
static GLuint glc_bloomTex0 = 0;
static GLuint glc_bloomTex1 = 0;
static int glc_bloomW = 0;
static int glc_bloomH = 0;
static GLuint glc_fbo = 0;
static GLuint glc_fboTex = 0;
static int glc_fboW = 0;
static int glc_fboH = 0;
static SDL_Texture *glc_captureTex = NULL;
static int glc_captureW = 0;
static int glc_captureH = 0;
static uint8_t *glc_capturePixels = NULL;
static uint8_t *glc_captureUpload = NULL;
static size_t glc_captureSize = 0;
static GLenum glc_framebufferTarget = GL_FRAMEBUFFER;
static GLenum glc_framebufferComplete = GL_FRAMEBUFFER_COMPLETE;
static GLenum glc_framebufferColorAttachment = GL_COLOR_ATTACHMENT0;
static int glc_crtShaderAdvanced = 1;

static int
glc_loadGl(void)
{
    glc_glCreateShader = (glc_create_shader_fn)SDL_GL_GetProcAddress("glCreateShader");
    glc_glShaderSource = (glc_shader_source_fn)SDL_GL_GetProcAddress("glShaderSource");
    glc_glCompileShader = (glc_compile_shader_fn)SDL_GL_GetProcAddress("glCompileShader");
    glc_glGetShaderiv = (glc_get_shader_iv_fn)SDL_GL_GetProcAddress("glGetShaderiv");
    glc_glGetShaderInfoLog = (glc_get_shader_info_log_fn)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    glc_glDeleteShader = (glc_delete_shader_fn)SDL_GL_GetProcAddress("glDeleteShader");
    glc_glCreateProgram = (glc_create_program_fn)SDL_GL_GetProcAddress("glCreateProgram");
    glc_glAttachShader = (glc_attach_shader_fn)SDL_GL_GetProcAddress("glAttachShader");
    glc_glLinkProgram = (glc_link_program_fn)SDL_GL_GetProcAddress("glLinkProgram");
    glc_glGetProgramiv = (glc_get_program_iv_fn)SDL_GL_GetProcAddress("glGetProgramiv");
    glc_glGetProgramInfoLog = (glc_get_program_info_log_fn)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    glc_glDeleteProgram = (glc_delete_program_fn)SDL_GL_GetProcAddress("glDeleteProgram");
    glc_glGetUniformLocation = (glc_get_uniform_location_fn)SDL_GL_GetProcAddress("glGetUniformLocation");
    glc_glUseProgram = (glc_use_program_fn)SDL_GL_GetProcAddress("glUseProgram");
    glc_glUniform1f = (glc_uniform_1f_fn)SDL_GL_GetProcAddress("glUniform1f");
    glc_glUniform1i = (glc_uniform_1i_fn)SDL_GL_GetProcAddress("glUniform1i");
    glc_glUniform2f = (glc_uniform_2f_fn)SDL_GL_GetProcAddress("glUniform2f");
    glc_glActiveTexture = (glc_active_texture_fn)SDL_GL_GetProcAddress("glActiveTexture");
    glc_glDrawBuffer = (glc_draw_buffer_fn)SDL_GL_GetProcAddress("glDrawBuffer");
    glc_glReadBuffer = (glc_read_buffer_fn)SDL_GL_GetProcAddress("glReadBuffer");
    glc_glGenFramebuffers = (glc_gen_framebuffers_fn)SDL_GL_GetProcAddress("glGenFramebuffers");
    glc_glDeleteFramebuffers = (glc_delete_framebuffers_fn)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    glc_glBindFramebuffer = (glc_bind_framebuffer_fn)SDL_GL_GetProcAddress("glBindFramebuffer");
    glc_glFramebufferTexture2D = (glc_framebuffer_texture_fn)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glc_glCheckFramebufferStatus = (glc_check_framebuffer_fn)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    if (!glc_glGenFramebuffers) {
        glc_glGenFramebuffers = (glc_gen_framebuffers_fn)SDL_GL_GetProcAddress("glGenFramebuffersEXT");
        glc_glDeleteFramebuffers = (glc_delete_framebuffers_fn)SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
        glc_glBindFramebuffer = (glc_bind_framebuffer_fn)SDL_GL_GetProcAddress("glBindFramebufferEXT");
        glc_glFramebufferTexture2D = (glc_framebuffer_texture_fn)SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
        glc_glCheckFramebufferStatus = (glc_check_framebuffer_fn)SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
        glc_framebufferTarget = GL_FRAMEBUFFER_EXT;
        glc_framebufferComplete = GL_FRAMEBUFFER_COMPLETE_EXT;
        glc_framebufferColorAttachment = GL_COLOR_ATTACHMENT0_EXT;
    }

    if (!glc_glCreateShader || !glc_glShaderSource || !glc_glCompileShader ||
        !glc_glGetShaderiv || !glc_glGetShaderInfoLog || !glc_glDeleteShader ||
        !glc_glCreateProgram || !glc_glAttachShader || !glc_glLinkProgram ||
        !glc_glGetProgramiv || !glc_glGetProgramInfoLog || !glc_glDeleteProgram ||
        !glc_glGetUniformLocation || !glc_glUseProgram || !glc_glUniform1f ||
        !glc_glUniform1i || !glc_glUniform2f || !glc_glActiveTexture) {
        fprintf(stderr, "gl-composite: missing GL entry points\n");
        return 0;
    }
    return 1;
}

static GLuint
glc_compile(GLenum type, const char *src)
{
    GLuint shader = glc_glCreateShader(type);
    if (!shader) {
        return 0;
    }
    glc_glShaderSource(shader, 1, &src, NULL);
    glc_glCompileShader(shader);
    GLint ok = GL_FALSE;
    glc_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glc_glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        fprintf(stderr, "gl-composite: compile failed: %s\n", log);
        glc_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint
glc_link(GLuint vs, GLuint fs)
{
    GLuint prog = glc_glCreateProgram();
    if (!prog) {
        return 0;
    }
    glc_glAttachShader(prog, vs);
    glc_glAttachShader(prog, fs);
    glc_glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glc_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glc_glGetProgramInfoLog(prog, (GLsizei)sizeof(log), &len, log);
        fprintf(stderr, "gl-composite: link failed: %s\n", log);
        glc_glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void
glc_drawQuadFlipY(float x0, float y0, float x1, float y1)
{
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x0, y1);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x1, y0);
    glEnd();
}

static void
glc_drawQuadNormal(float x0, float y0, float x1, float y1)
{
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x0, y1);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x1, y0);
    glEnd();
}

static void
glc_setCrtUniforms(int useAdv, int texW, int texH, int dstW, int dstH, float dstOffsetX, float dstOffsetY)
{
    if (texW <= 0 || texH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    GLint texSizeLoc = useAdv ? glc_crt_adv_texSizeLoc : glc_crt_texSizeLoc;
    if (texSizeLoc >= 0) {
        glc_glUniform2f(texSizeLoc, (float)texW, (float)texH);
    }

    GLint geomLoc = useAdv ? glc_crt_adv_geomLoc : glc_crt_geomLoc;
    GLint scanLoc = useAdv ? glc_crt_adv_scanLoc : glc_crt_scanLoc;
    GLint beamLoc = useAdv ? glc_crt_adv_beamLoc : glc_crt_beamLoc;
    GLint borderLoc = useAdv ? glc_crt_adv_borderLoc : glc_crt_borderLoc;
    GLint overscanLoc = useAdv ? glc_crt_adv_overscanLoc : glc_crt_overscanLoc;

    if (geomLoc >= 0) {
        glc_glUniform1f(geomLoc, crt_isGeometryEnabled() ? 1.0f : 0.0f);
    }
    if (scanLoc >= 0) {
        glc_glUniform1f(scanLoc, crt_isMaskEnabled() ? 1.0f : 0.0f);
    }
    if (beamLoc >= 0) {
        glc_glUniform1f(beamLoc, crt_isBloomEnabled() ? 1.0f : 0.0f);
    }
    if (borderLoc >= 0) {
        glc_glUniform1f(borderLoc, crt_getScanlineBorder());
    }
    if (overscanLoc >= 0) {
        glc_glUniform1f(overscanLoc, crt_getOverscan());
    }

    if (!useAdv) {
        return;
    }

    if (glc_crt_adv_dstSizeLoc >= 0) {
        glc_glUniform2f(glc_crt_adv_dstSizeLoc, (float)dstW, (float)dstH);
    }
    if (glc_crt_adv_dstOffsetLoc >= 0) {
        glc_glUniform2f(glc_crt_adv_dstOffsetLoc, dstOffsetX, dstOffsetY);
    }
    if (glc_crt_adv_gammaLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_gammaLoc, crt_isGammaEnabled() ? 1.0f : 0.0f);
    }
    if (glc_crt_adv_chromaLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_chromaLoc, crt_isChromaEnabled() ? 1.0f : 0.0f);
    }
    if (glc_crt_adv_scanStrengthLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_scanStrengthLoc, crt_getScanStrength());
    }
    if (glc_crt_adv_maskStrengthLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_maskStrengthLoc, crt_getMaskStrength());
    }
    if (glc_crt_adv_maskScaleLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_maskScaleLoc, crt_getMaskScale());
    }
    if (glc_crt_adv_maskTypeLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_maskTypeLoc, (float)crt_getMaskType());
    }
    if (glc_crt_adv_grilleLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_grilleLoc, crt_isGrilleEnabled() ? 1.0f : 0.0f);
    }
    if (glc_crt_adv_grilleStrengthLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_grilleStrengthLoc, crt_getGrilleStrength());
    }
    if (glc_crt_adv_beamStrengthLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_beamStrengthLoc, crt_getBeamStrength());
    }
    if (glc_crt_adv_beamWidthLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_beamWidthLoc, crt_getBeamWidth());
    }
    if (glc_crt_adv_curvatureKLoc >= 0) {
        glc_glUniform1f(glc_crt_adv_curvatureKLoc, crt_getCurvatureK());
    }
}

static int
glc_bloomSupported(void)
{
    if (!glc_program_bloom_downsample || !glc_program_bloom_blur || !glc_program_bloom_composite) {
        return 0;
    }
    if (!glc_glGenFramebuffers || !glc_glBindFramebuffer || !glc_glFramebufferTexture2D ||
        !glc_glCheckFramebufferStatus || !glc_glDeleteFramebuffers) {
        return 0;
    }
    return 1;
}

static int
glc_bloomEnsureTargets(int sceneW, int sceneH)
{
    if (!glc_bloomSupported() || sceneW <= 0 || sceneH <= 0) {
        return 0;
    }

    int bloomW = (sceneW + 3) / 4;
    int bloomH = (sceneH + 3) / 4;
    if (bloomW < 1) {
        bloomW = 1;
    }
    if (bloomH < 1) {
        bloomH = 1;
    }

    if (!glc_bloomFbo) {
        glc_glGenFramebuffers(1, &glc_bloomFbo);
    }
    if (!glc_bloomSceneTex) {
        glGenTextures(1, &glc_bloomSceneTex);
    }
    if (!glc_bloomTex0) {
        glGenTextures(1, &glc_bloomTex0);
    }
    if (!glc_bloomTex1) {
        glGenTextures(1, &glc_bloomTex1);
    }
    if (!glc_bloomFbo || !glc_bloomSceneTex || !glc_bloomTex0 || !glc_bloomTex1) {
        return 0;
    }

    if (sceneW != glc_bloomSceneW || sceneH != glc_bloomSceneH) {
        glBindTexture(GL_TEXTURE_2D, glc_bloomSceneTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sceneW, sceneH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glc_bloomSceneW = sceneW;
        glc_bloomSceneH = sceneH;
    }

    if (bloomW != glc_bloomW || bloomH != glc_bloomH) {
        glBindTexture(GL_TEXTURE_2D, glc_bloomTex0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bloomW, bloomH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, glc_bloomTex1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bloomW, bloomH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glc_bloomW = bloomW;
        glc_bloomH = bloomH;
    }

    glc_glBindFramebuffer(glc_framebufferTarget, glc_bloomFbo);
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_bloomSceneTex, 0);
    GLenum status = glc_glCheckFramebufferStatus(glc_framebufferTarget);
    glc_glBindFramebuffer(glc_framebufferTarget, 0);
    if (status != glc_framebufferComplete) {
        return 0;
    }
    return 1;
}

static int
glc_renderHalationPasses(int useAdv, int sceneW, int sceneH, GLuint targetFbo, int targetW, int targetH,
                         float outX0, float outY0, float outX1, float outY1)
{
    if (!glc_bloomEnsureTargets(sceneW, sceneH)) {
        return 0;
    }
    if (!crt_isHalationEnabled() || crt_getHalationStrength() <= 0.0f) {
        return 0;
    }

    float threshold = crt_getHalationThreshold();
    float knee = 0.10f;
    float strength = crt_getHalationStrength();
    float radiusFull = crt_getHalationRadius();
    float downsampleScaleX = (float)glc_bloomW / (float)glc_bloomSceneW;
    float downsampleScaleY = (float)glc_bloomH / (float)glc_bloomSceneH;
    float radiusX = radiusFull * downsampleScaleX;
    float radiusY = radiusFull * downsampleScaleY;

    glDisable(GL_BLEND);

    // Pass 1: CRT -> scene texture (source upload uses top-left origin, so flip here).
    glc_glBindFramebuffer(glc_framebufferTarget, glc_bloomFbo);
    if (glc_glDrawBuffer) {
        glc_glDrawBuffer(glc_framebufferColorAttachment);
    }
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_bloomSceneTex, 0);
    glViewport(0, 0, glc_bloomSceneW, glc_bloomSceneH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint progCrt = useAdv ? glc_program_crt_adv : glc_program_crt;
    GLint texLocCrt = useAdv ? glc_crt_adv_texLoc : glc_crt_texLoc;
    glc_glUseProgram(progCrt);
    if (texLocCrt >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_tex);
        glc_glUniform1i(texLocCrt, 0);
    }
    glc_setCrtUniforms(useAdv, glc_texW, glc_texH, glc_bloomSceneW, glc_bloomSceneH, 0.0f, 0.0f);
    glc_drawQuadFlipY(-1.0f, 1.0f, 1.0f, -1.0f);
    glc_glUseProgram(0);

    // Pass 2: extract + downsample -> bloomTex0 (scene texture has GL origin, so normal mapping).
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_bloomTex0, 0);
    glViewport(0, 0, glc_bloomW, glc_bloomH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glc_glUseProgram(glc_program_bloom_downsample);
    if (glc_bloom_down_texLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_bloomSceneTex);
        glc_glUniform1i(glc_bloom_down_texLoc, 0);
    }
    if (glc_bloom_down_invSrcSizeLoc >= 0) {
        glc_glUniform2f(glc_bloom_down_invSrcSizeLoc, 1.0f / (float)glc_bloomSceneW, 1.0f / (float)glc_bloomSceneH);
    }
    if (glc_bloom_down_thresholdLoc >= 0) {
        glc_glUniform1f(glc_bloom_down_thresholdLoc, threshold);
    }
    if (glc_bloom_down_kneeLoc >= 0) {
        glc_glUniform1f(glc_bloom_down_kneeLoc, knee);
    }
    glc_drawQuadNormal(-1.0f, 1.0f, 1.0f, -1.0f);
    glc_glUseProgram(0);

    // Pass 3: blur X -> bloomTex1.
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_bloomTex1, 0);
    glc_glUseProgram(glc_program_bloom_blur);
    if (glc_bloom_blur_texLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_bloomTex0);
        glc_glUniform1i(glc_bloom_blur_texLoc, 0);
    }
    if (glc_bloom_blur_stepUvLoc >= 0) {
        float stepUvX = (glc_bloomW > 0) ? (radiusX / (float)glc_bloomW) : 0.0f;
        glc_glUniform2f(glc_bloom_blur_stepUvLoc, stepUvX, 0.0f);
    }
    glc_drawQuadNormal(-1.0f, 1.0f, 1.0f, -1.0f);
    glc_glUseProgram(0);

    // Pass 4: blur Y -> bloomTex0.
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_bloomTex0, 0);
    glc_glUseProgram(glc_program_bloom_blur);
    if (glc_bloom_blur_texLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_bloomTex1);
        glc_glUniform1i(glc_bloom_blur_texLoc, 0);
    }
    if (glc_bloom_blur_stepUvLoc >= 0) {
        float stepUvY = (glc_bloomH > 0) ? (radiusY / (float)glc_bloomH) : 0.0f;
        glc_glUniform2f(glc_bloom_blur_stepUvLoc, 0.0f, stepUvY);
    }
    glc_drawQuadNormal(-1.0f, 1.0f, 1.0f, -1.0f);
    glc_glUseProgram(0);

    // Pass 5: composite -> target (add in linear, then encode).
    glc_glBindFramebuffer(glc_framebufferTarget, targetFbo);
    if (glc_glDrawBuffer) {
        glc_glDrawBuffer(targetFbo ? glc_framebufferColorAttachment : GL_BACK);
    }
    glViewport(0, 0, targetW, targetH);
    glc_glUseProgram(glc_program_bloom_composite);
    if (glc_bloom_comp_baseLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_bloomSceneTex);
        glc_glUniform1i(glc_bloom_comp_baseLoc, 0);
    }
    if (glc_bloom_comp_bloomLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, glc_bloomTex0);
        glc_glUniform1i(glc_bloom_comp_bloomLoc, 1);
    }
    if (glc_bloom_comp_strengthLoc >= 0) {
        glc_glUniform1f(glc_bloom_comp_strengthLoc, strength);
    }
    glc_drawQuadNormal(outX0, outY0, outX1, outY1);
    glc_glUseProgram(0);

    return 1;
}

int
gl_composite_init(SDL_Window *window, SDL_Renderer *renderer)
{
    if (!window || !renderer) {
        return 0;
    }
    (void)window;
    glc_context = SDL_GL_GetCurrentContext();
    if (!glc_context) {
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        glc_context = SDL_GL_GetCurrentContext();
    }
    if (!glc_context) {
        return 0;
    }
    if (!glc_loadGl()) {
        return 0;
    }
    if (glc_crtShaderAdvanced && !crt_hasPersistedConfig()) {
        crt_setAdvancedDefaults();
    }
    const char *vsSrc =
        "#version 120\n"
        "void main() {\n"
        "  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "}\n";
    const char *fsPlain =
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(u_tex, gl_TexCoord[0].st);\n"
        "}\n";
    const char *fsCrt = shader_base_crtFragmentSource();
    const char *fsCrtAdv = shader_advanced_crtFragmentSource();
    GLuint vs = glc_compile(GL_VERTEX_SHADER, vsSrc);
    GLuint fsPlainShader = glc_compile(GL_FRAGMENT_SHADER, fsPlain);
    if (!vs || !fsPlainShader) {
        if (vs) glc_glDeleteShader(vs);
        if (fsPlainShader) glc_glDeleteShader(fsPlainShader);
        return 0;
    }
    glc_program_plain = glc_link(vs, fsPlainShader);
    glc_glDeleteShader(fsPlainShader);
    if (!glc_program_plain) {
        glc_glDeleteShader(vs);
        return 0;
    }
    glc_plain_texLoc = glc_glGetUniformLocation(glc_program_plain, "u_tex");

    GLuint fsCrtShader = glc_compile(GL_FRAGMENT_SHADER, fsCrt);
    if (!fsCrtShader) {
        glc_glDeleteShader(vs);
        return 0;
    }
    glc_program_crt = glc_link(vs, fsCrtShader);
    glc_glDeleteShader(fsCrtShader);
    if (!glc_program_crt) {
        glc_glDeleteShader(vs);
        return 0;
    }
    glc_crt_texLoc = glc_glGetUniformLocation(glc_program_crt, "u_tex");
    glc_crt_texSizeLoc = glc_glGetUniformLocation(glc_program_crt, "u_texSize");
    glc_crt_geomLoc = glc_glGetUniformLocation(glc_program_crt, "u_geom");
    glc_crt_scanLoc = glc_glGetUniformLocation(glc_program_crt, "u_scan");
    glc_crt_beamLoc = glc_glGetUniformLocation(glc_program_crt, "u_beam");
    glc_crt_borderLoc = glc_glGetUniformLocation(glc_program_crt, "u_border");
    glc_crt_overscanLoc = glc_glGetUniformLocation(glc_program_crt, "u_overscan");

    GLuint fsCrtAdvShader = glc_compile(GL_FRAGMENT_SHADER, fsCrtAdv);
    if (!fsCrtAdvShader) {
        glc_glDeleteShader(vs);
        return 0;
    }
    glc_program_crt_adv = glc_link(vs, fsCrtAdvShader);
    glc_glDeleteShader(fsCrtAdvShader);
    if (!glc_program_crt_adv) {
        glc_glDeleteShader(vs);
        return 0;
    }
    glc_crt_adv_texLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_tex");
    glc_crt_adv_texSizeLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_texSize");
    glc_crt_adv_geomLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_geom");
    glc_crt_adv_scanLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_scan");
    glc_crt_adv_beamLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_beam");
    glc_crt_adv_borderLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_border");
    glc_crt_adv_scanStrengthLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_scanStrength");
    glc_crt_adv_maskStrengthLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_maskStrength");
    glc_crt_adv_maskScaleLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_maskScale");
    glc_crt_adv_maskTypeLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_maskType");
    glc_crt_adv_grilleLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_grille");
    glc_crt_adv_grilleStrengthLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_grilleStrength");
    glc_crt_adv_beamStrengthLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_beamStrength");
    glc_crt_adv_beamWidthLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_beamWidth");
    glc_crt_adv_curvatureKLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_curvatureK");
    glc_crt_adv_overscanLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_overscan");
    glc_crt_adv_dstSizeLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_dstSize");
    glc_crt_adv_dstOffsetLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_dstOffset");
    glc_crt_adv_gammaLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_gamma");
    glc_crt_adv_chromaLoc = glc_glGetUniformLocation(glc_program_crt_adv, "u_chroma");

    const char *fsBloomDown = shader_bloom_downsampleFragmentSource();
    const char *fsBloomBlur = shader_bloom_blurFragmentSource();
    const char *fsBloomComp = shader_bloom_compositeFragmentSource();

    GLuint fsBloomDownShader = glc_compile(GL_FRAGMENT_SHADER, fsBloomDown);
    if (fsBloomDownShader) {
        glc_program_bloom_downsample = glc_link(vs, fsBloomDownShader);
        glc_glDeleteShader(fsBloomDownShader);
        if (glc_program_bloom_downsample) {
            glc_bloom_down_texLoc = glc_glGetUniformLocation(glc_program_bloom_downsample, "u_tex");
            glc_bloom_down_invSrcSizeLoc = glc_glGetUniformLocation(glc_program_bloom_downsample, "u_invSrcSize");
            glc_bloom_down_thresholdLoc = glc_glGetUniformLocation(glc_program_bloom_downsample, "u_threshold");
            glc_bloom_down_kneeLoc = glc_glGetUniformLocation(glc_program_bloom_downsample, "u_knee");
        }
    }

    GLuint fsBloomBlurShader = glc_compile(GL_FRAGMENT_SHADER, fsBloomBlur);
    if (fsBloomBlurShader) {
        glc_program_bloom_blur = glc_link(vs, fsBloomBlurShader);
        glc_glDeleteShader(fsBloomBlurShader);
        if (glc_program_bloom_blur) {
            glc_bloom_blur_texLoc = glc_glGetUniformLocation(glc_program_bloom_blur, "u_tex");
            glc_bloom_blur_stepUvLoc = glc_glGetUniformLocation(glc_program_bloom_blur, "u_stepUv");
        }
    }

    GLuint fsBloomCompShader = glc_compile(GL_FRAGMENT_SHADER, fsBloomComp);
    if (fsBloomCompShader) {
        glc_program_bloom_composite = glc_link(vs, fsBloomCompShader);
        glc_glDeleteShader(fsBloomCompShader);
        if (glc_program_bloom_composite) {
            glc_bloom_comp_baseLoc = glc_glGetUniformLocation(glc_program_bloom_composite, "u_base");
            glc_bloom_comp_bloomLoc = glc_glGetUniformLocation(glc_program_bloom_composite, "u_bloom");
            glc_bloom_comp_strengthLoc = glc_glGetUniformLocation(glc_program_bloom_composite, "u_strength");
        }
    }

    glc_glDeleteShader(vs);
    glGenTextures(1, &glc_tex);
    glBindTexture(GL_TEXTURE_2D, glc_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glc_active = 1;
    (void)renderer;
    return 1;
}

void
gl_composite_shutdown(void)
{
    glc_active = 0;
    if (glc_program_plain) {
        glc_glDeleteProgram(glc_program_plain);
        glc_program_plain = 0;
    }
    if (glc_program_crt) {
        glc_glDeleteProgram(glc_program_crt);
        glc_program_crt = 0;
    }
    if (glc_program_crt_adv) {
        glc_glDeleteProgram(glc_program_crt_adv);
        glc_program_crt_adv = 0;
    }
    if (glc_program_bloom_downsample) {
        glc_glDeleteProgram(glc_program_bloom_downsample);
        glc_program_bloom_downsample = 0;
    }
    if (glc_program_bloom_blur) {
        glc_glDeleteProgram(glc_program_bloom_blur);
        glc_program_bloom_blur = 0;
    }
    if (glc_program_bloom_composite) {
        glc_glDeleteProgram(glc_program_bloom_composite);
        glc_program_bloom_composite = 0;
    }
    if (glc_tex) {
        glDeleteTextures(1, &glc_tex);
        glc_tex = 0;
    }
    glc_texW = 0;
    glc_texH = 0;
    if (glc_bloomSceneTex) {
        glDeleteTextures(1, &glc_bloomSceneTex);
        glc_bloomSceneTex = 0;
    }
    glc_bloomSceneW = 0;
    glc_bloomSceneH = 0;
    if (glc_bloomTex0) {
        glDeleteTextures(1, &glc_bloomTex0);
        glc_bloomTex0 = 0;
    }
    if (glc_bloomTex1) {
        glDeleteTextures(1, &glc_bloomTex1);
        glc_bloomTex1 = 0;
    }
    glc_bloomW = 0;
    glc_bloomH = 0;
    if (glc_bloomFbo) {
        glc_glDeleteFramebuffers(1, &glc_bloomFbo);
        glc_bloomFbo = 0;
    }
    if (glc_fboTex) {
        glDeleteTextures(1, &glc_fboTex);
        glc_fboTex = 0;
    }
    if (glc_fbo) {
        glc_glDeleteFramebuffers(1, &glc_fbo);
        glc_fbo = 0;
    }
    glc_fboW = 0;
    glc_fboH = 0;
    if (glc_captureTex) {
        SDL_DestroyTexture(glc_captureTex);
        glc_captureTex = NULL;
    }
    glc_captureW = 0;
    glc_captureH = 0;
    if (glc_capturePixels) {
        free(glc_capturePixels);
        glc_capturePixels = NULL;
    }
    if (glc_captureUpload) {
        free(glc_captureUpload);
        glc_captureUpload = NULL;
    }
    glc_captureSize = 0;
    if (glc_upload) {
        free(glc_upload);
        glc_upload = NULL;
        glc_uploadSize = 0;
    }
    if (glc_context && glc_contextOwned) {
        SDL_GL_DeleteContext(glc_context);
    }
    glc_context = NULL;
    glc_contextOwned = 0;
}

int
gl_composite_isActive(void)
{
    return glc_active ? 1 : 0;
}

void
gl_composite_renderFrame(SDL_Renderer *renderer, const uint8_t *data, int width, int height,
                         size_t pitch, const SDL_Rect *dst)
{
    if (!glc_active || !renderer || !data || !dst || width <= 0 || height <= 0) {
        return;
    }
    SDL_RenderFlush(renderer);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    const uint8_t *uploadSrc = data;
    if (pitch != (size_t)width * 4) {
        size_t need = (size_t)width * (size_t)height * 4;
        if (need > glc_uploadSize) {
            uint8_t *next = (uint8_t *)realloc(glc_upload, need);
            if (!next) {
                return;
            }
            glc_upload = next;
            glc_uploadSize = need;
        }
        for (int y = 0; y < height; ++y) {
            memcpy(glc_upload + (size_t)y * (size_t)width * 4, data + (size_t)y * pitch, (size_t)width * 4);
        }
        uploadSrc = glc_upload;
    }
    if (width != glc_texW || height != glc_texH) {
        glc_texW = width;
        glc_texH = height;
        glBindTexture(GL_TEXTURE_2D, glc_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glc_texW, glc_texH, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, glc_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glc_texW, glc_texH, GL_BGRA, GL_UNSIGNED_BYTE, uploadSrc);

    int ww = 0;
    int hh = 0;
    SDL_GetRendererOutputSize(renderer, &ww, &hh);
    if (ww <= 0 || hh <= 0) {
        return;
    }
    float x0 = (2.0f * (float)dst->x / (float)ww) - 1.0f;
    float x1 = (2.0f * (float)(dst->x + dst->w) / (float)ww) - 1.0f;
    float y0 = 1.0f - (2.0f * (float)dst->y / (float)hh);
    float y1 = 1.0f - (2.0f * (float)(dst->y + dst->h) / (float)hh);

    int useCrt = crt_isEnabled();
    int useAdv = (useCrt && glc_crtShaderAdvanced) ? 1 : 0;
    if (useCrt && glc_bloomSupported() && crt_isHalationEnabled() && crt_getHalationStrength() > 0.0f) {
        if (dst->w > 0 && dst->h > 0) {
            if (glc_renderHalationPasses(useAdv, dst->w, dst->h, 0, ww, hh, x0, y0, x1, y1)) {
                glMatrixMode(GL_TEXTURE);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);
                glPopMatrix();
                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glPopAttrib();
                return;
            }
        }
    }

    glViewport(0, 0, ww, hh);
    GLuint prog = useCrt ? (useAdv ? glc_program_crt_adv : glc_program_crt) : glc_program_plain;
    GLint texLoc = useCrt ? (useAdv ? glc_crt_adv_texLoc : glc_crt_texLoc) : glc_plain_texLoc;
    glc_glUseProgram(prog);
    if (texLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_tex);
        glc_glUniform1i(texLoc, 0);
    }
    if (useCrt) {
        GLint texSizeLoc = useAdv ? glc_crt_adv_texSizeLoc : glc_crt_texSizeLoc;
        if (texSizeLoc >= 0) {
            glc_glUniform2f(texSizeLoc, (float)glc_texW, (float)glc_texH);
        }
    }
    if (useCrt) {
        GLint geomLoc = useAdv ? glc_crt_adv_geomLoc : glc_crt_geomLoc;
        GLint scanLoc = useAdv ? glc_crt_adv_scanLoc : glc_crt_scanLoc;
        GLint beamLoc = useAdv ? glc_crt_adv_beamLoc : glc_crt_beamLoc;
        GLint borderLoc = useAdv ? glc_crt_adv_borderLoc : glc_crt_borderLoc;
        GLint overscanLoc = useAdv ? glc_crt_adv_overscanLoc : glc_crt_overscanLoc;
        if (geomLoc >= 0) {
            glc_glUniform1f(geomLoc, crt_isGeometryEnabled() ? 1.0f : 0.0f);
        }
        if (scanLoc >= 0) {
            glc_glUniform1f(scanLoc, crt_isMaskEnabled() ? 1.0f : 0.0f);
        }
        if (beamLoc >= 0) {
            glc_glUniform1f(beamLoc, crt_isBloomEnabled() ? 1.0f : 0.0f);
        }
        if (borderLoc >= 0) {
            glc_glUniform1f(borderLoc, crt_getScanlineBorder());
        }
        if (overscanLoc >= 0) {
            glc_glUniform1f(overscanLoc, crt_getOverscan());
        }
        if (useAdv && glc_crt_adv_dstSizeLoc >= 0) {
            glc_glUniform2f(glc_crt_adv_dstSizeLoc, (float)dst->w, (float)dst->h);
        }
        if (useAdv && glc_crt_adv_dstOffsetLoc >= 0) {
            float ox = (float)dst->x;
            float oy = (float)hh - (float)(dst->y + dst->h);
            glc_glUniform2f(glc_crt_adv_dstOffsetLoc, ox, oy);
        }
        if (useAdv && glc_crt_adv_gammaLoc >= 0) {
            glc_glUniform1f(glc_crt_adv_gammaLoc, crt_isGammaEnabled() ? 1.0f : 0.0f);
        }
        if (useAdv && glc_crt_adv_chromaLoc >= 0) {
            glc_glUniform1f(glc_crt_adv_chromaLoc, crt_isChromaEnabled() ? 1.0f : 0.0f);
        }
        if (useAdv) {
            if (glc_crt_adv_scanStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_scanStrengthLoc, crt_getScanStrength());
            }
            if (glc_crt_adv_maskStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskStrengthLoc, crt_getMaskStrength());
            }
            if (glc_crt_adv_maskScaleLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskScaleLoc, crt_getMaskScale());
            }
            if (glc_crt_adv_maskTypeLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskTypeLoc, (float)crt_getMaskType());
            }
            if (glc_crt_adv_grilleLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_grilleLoc, crt_isGrilleEnabled() ? 1.0f : 0.0f);
            }
            if (glc_crt_adv_grilleStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_grilleStrengthLoc, crt_getGrilleStrength());
            }
            if (glc_crt_adv_beamStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_beamStrengthLoc, crt_getBeamStrength());
            }
            if (glc_crt_adv_beamWidthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_beamWidthLoc, crt_getBeamWidth());
            }
            if (glc_crt_adv_curvatureKLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_curvatureKLoc, crt_getCurvatureK());
            }
        }
    }
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x0, y1);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x1, y0);
    glEnd();
    glc_glUseProgram(0);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

static int
glc_captureEnsureTargets(SDL_Renderer *renderer, int w, int h)
{
    if (!renderer || w <= 0 || h <= 0) {
        return 0;
    }
    if (!glc_fbo) {
        glc_glGenFramebuffers(1, &glc_fbo);
    }
    if (!glc_fboTex) {
        glGenTextures(1, &glc_fboTex);
    }
    if (!glc_fbo || !glc_fboTex) {
        return 0;
    }
    if (w != glc_fboW || h != glc_fboH) {
        glBindTexture(GL_TEXTURE_2D, glc_fboTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glc_fboW = w;
        glc_fboH = h;
    }
    glc_glBindFramebuffer(glc_framebufferTarget, glc_fbo);
    glc_glFramebufferTexture2D(glc_framebufferTarget, glc_framebufferColorAttachment, GL_TEXTURE_2D, glc_fboTex, 0);
    GLenum status = glc_glCheckFramebufferStatus(glc_framebufferTarget);
    glc_glBindFramebuffer(glc_framebufferTarget, 0);
    if (status != glc_framebufferComplete) {
        return 0;
    }
    size_t needed = (size_t)w * (size_t)h * 4;
    if (needed > glc_captureSize) {
        uint8_t *pix = (uint8_t *)realloc(glc_capturePixels, needed);
        uint8_t *up = (uint8_t *)realloc(glc_captureUpload, needed);
        if (!pix || !up) {
            if (pix) glc_capturePixels = pix;
            if (up) glc_captureUpload = up;
            return 0;
        }
        glc_capturePixels = pix;
        glc_captureUpload = up;
        glc_captureSize = needed;
    }
    if (!glc_captureTex || w != glc_captureW || h != glc_captureH) {
        if (glc_captureTex) {
            SDL_DestroyTexture(glc_captureTex);
        }
        glc_captureTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!glc_captureTex) {
            return 0;
        }
        glc_captureW = w;
        glc_captureH = h;
    }
    return 1;
}

int
gl_composite_captureToRenderer(SDL_Renderer *renderer, const uint8_t *data, int width, int height,
                               size_t pitch, const SDL_Rect *dst)
{
    if (!glc_active || !renderer || !data || !dst || width <= 0 || height <= 0) {
        return 0;
    }
    if (!glc_glGenFramebuffers || !glc_glBindFramebuffer || !glc_glFramebufferTexture2D ||
        !glc_glCheckFramebufferStatus || !glc_glDeleteFramebuffers) {
        return 0;
    }
    int capW = dst->w;
    int capH = dst->h;
    if (capW <= 0 || capH <= 0) {
        return 0;
    }
    if (!glc_captureEnsureTargets(renderer, capW, capH)) {
        return 0;
    }
    SDL_Texture *prevTarget = SDL_GetRenderTarget(renderer);
    SDL_Rect prevViewport;
    SDL_RenderGetViewport(renderer, &prevViewport);
    SDL_Rect prevClip;
    SDL_bool prevClipEnabled = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer, &prevClip);
    float prevScaleX = 1.0f;
    float prevScaleY = 1.0f;
    SDL_RenderGetScale(renderer, &prevScaleX, &prevScaleY);
    if (prevTarget) {
        SDL_SetRenderTarget(renderer, NULL);
    }
    SDL_RenderFlush(renderer);
    GLint prevGlViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prevGlViewport);

    const uint8_t *src = data;
    if (pitch != (size_t)width * 4) {
        size_t need = (size_t)width * (size_t)height * 4;
        if (need > glc_uploadSize) {
            uint8_t *next = (uint8_t *)realloc(glc_upload, need);
            if (!next) {
                return 0;
            }
            glc_upload = next;
            glc_uploadSize = need;
        }
        for (int y = 0; y < height; ++y) {
            memcpy(glc_upload + (size_t)y * (size_t)width * 4, data + (size_t)y * pitch, (size_t)width * 4);
        }
        src = glc_upload;
    }
    if (width != glc_texW || height != glc_texH) {
        glc_texW = width;
        glc_texH = height;
        glBindTexture(GL_TEXTURE_2D, glc_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glc_texW, glc_texH, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, glc_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glc_texW, glc_texH, GL_BGRA, GL_UNSIGNED_BYTE, src);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    glc_glBindFramebuffer(glc_framebufferTarget, glc_fbo);
    if (glc_glDrawBuffer) {
        glc_glDrawBuffer(glc_framebufferColorAttachment);
    }
    glViewport(0, 0, capW, capH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    int useCrt = crt_isEnabled();
    int useAdv = (useCrt && glc_crtShaderAdvanced) ? 1 : 0;
    if (useCrt && glc_bloomSupported() && crt_isHalationEnabled() && crt_getHalationStrength() > 0.0f) {
        if (glc_renderHalationPasses(useAdv, capW, capH, glc_fbo, capW, capH, -1.0f, 1.0f, 1.0f, -1.0f)) {
            // glc_fbo stays bound for readback.
            goto glc_capture_readback;
        }
    }
    GLuint prog = useCrt ? (useAdv ? glc_program_crt_adv : glc_program_crt) : glc_program_plain;
    GLint texLoc = useCrt ? (useAdv ? glc_crt_adv_texLoc : glc_crt_texLoc) : glc_plain_texLoc;
    glc_glUseProgram(prog);
    if (texLoc >= 0) {
        glc_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glc_tex);
        glc_glUniform1i(texLoc, 0);
    }
    if (useCrt) {
        GLint texSizeLoc = useAdv ? glc_crt_adv_texSizeLoc : glc_crt_texSizeLoc;
        if (texSizeLoc >= 0) {
            glc_glUniform2f(texSizeLoc, (float)glc_texW, (float)glc_texH);
        }
    }
    if (useCrt) {
        GLint geomLoc = useAdv ? glc_crt_adv_geomLoc : glc_crt_geomLoc;
        GLint scanLoc = useAdv ? glc_crt_adv_scanLoc : glc_crt_scanLoc;
        GLint beamLoc = useAdv ? glc_crt_adv_beamLoc : glc_crt_beamLoc;
        GLint borderLoc = useAdv ? glc_crt_adv_borderLoc : glc_crt_borderLoc;
        GLint overscanLoc = useAdv ? glc_crt_adv_overscanLoc : glc_crt_overscanLoc;
        if (geomLoc >= 0) {
            glc_glUniform1f(geomLoc, crt_isGeometryEnabled() ? 1.0f : 0.0f);
        }
        if (scanLoc >= 0) {
            glc_glUniform1f(scanLoc, crt_isMaskEnabled() ? 1.0f : 0.0f);
        }
        if (beamLoc >= 0) {
            glc_glUniform1f(beamLoc, crt_isBloomEnabled() ? 1.0f : 0.0f);
        }
        if (borderLoc >= 0) {
            glc_glUniform1f(borderLoc, crt_getScanlineBorder());
        }
        if (overscanLoc >= 0) {
            glc_glUniform1f(overscanLoc, crt_getOverscan());
        }
        if (useAdv && glc_crt_adv_dstSizeLoc >= 0) {
            glc_glUniform2f(glc_crt_adv_dstSizeLoc, (float)capW, (float)capH);
        }
        if (useAdv && glc_crt_adv_dstOffsetLoc >= 0) {
            glc_glUniform2f(glc_crt_adv_dstOffsetLoc, 0.0f, 0.0f);
        }
        if (useAdv && glc_crt_adv_gammaLoc >= 0) {
            glc_glUniform1f(glc_crt_adv_gammaLoc, crt_isGammaEnabled() ? 1.0f : 0.0f);
        }
        if (useAdv && glc_crt_adv_chromaLoc >= 0) {
            glc_glUniform1f(glc_crt_adv_chromaLoc, crt_isChromaEnabled() ? 1.0f : 0.0f);
        }
        if (useAdv) {
            if (glc_crt_adv_scanStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_scanStrengthLoc, crt_getScanStrength());
            }
            if (glc_crt_adv_maskStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskStrengthLoc, crt_getMaskStrength());
            }
            if (glc_crt_adv_maskScaleLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskScaleLoc, crt_getMaskScale());
            }
            if (glc_crt_adv_maskTypeLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_maskTypeLoc, (float)crt_getMaskType());
            }
            if (glc_crt_adv_grilleLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_grilleLoc, crt_isGrilleEnabled() ? 1.0f : 0.0f);
            }
            if (glc_crt_adv_grilleStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_grilleStrengthLoc, crt_getGrilleStrength());
            }
            if (glc_crt_adv_beamStrengthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_beamStrengthLoc, crt_getBeamStrength());
            }
            if (glc_crt_adv_beamWidthLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_beamWidthLoc, crt_getBeamWidth());
            }
            if (glc_crt_adv_curvatureKLoc >= 0) {
                glc_glUniform1f(glc_crt_adv_curvatureKLoc, crt_getCurvatureK());
            }
        }
    }
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, -1.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, 1.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glEnd();
    glc_glUseProgram(0);

glc_capture_readback:
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (glc_glReadBuffer) {
        glc_glReadBuffer(glc_framebufferColorAttachment);
    }
    glReadPixels(0, 0, capW, capH, GL_BGRA, GL_UNSIGNED_BYTE, glc_capturePixels);
    glc_glBindFramebuffer(glc_framebufferTarget, 0);

    size_t row = (size_t)capW * 4;
    for (int y = 0; y < capH; ++y) {
        memcpy(glc_captureUpload + (size_t)y * row,
               glc_capturePixels + (size_t)(capH - 1 - y) * row,
               row);
    }
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glViewport(prevGlViewport[0], prevGlViewport[1], prevGlViewport[2], prevGlViewport[3]);
    if (prevTarget) {
        SDL_SetRenderTarget(renderer, prevTarget);
    }
    SDL_RenderSetViewport(renderer, &prevViewport);
    if (prevClipEnabled) {
        SDL_RenderSetClipRect(renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(renderer, NULL);
    }
    SDL_RenderSetScale(renderer, prevScaleX, prevScaleY);
    SDL_UpdateTexture(glc_captureTex, NULL, glc_captureUpload, (int)row);
    SDL_RenderCopy(renderer, glc_captureTex, NULL, dst);
    return 1;
}

int
gl_composite_isCrtShaderAdvanced(void)
{
    return glc_crtShaderAdvanced ? 1 : 0;
}

int
gl_composite_toggleCrtShaderAdvanced(void)
{
    glc_crtShaderAdvanced = glc_crtShaderAdvanced ? 0 : 1;
    if (glc_crtShaderAdvanced) {
        crt_setAdvancedDefaults();
    }
    return glc_crtShaderAdvanced ? 1 : 0;
}
