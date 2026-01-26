/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "shader_bloom.h"

static const char *
shader_bloom_downsampleFragmentSourceInternal(void)
{
    return
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_invSrcSize;\n"
        "uniform float u_threshold;\n"
        "uniform float u_knee;\n"
        "vec3 srgbToLinear(vec3 c) {\n"
        "  return pow(max(c, vec3(0.0)), vec3(2.2));\n"
        "}\n"
        "void main() {\n"
        "  vec2 uv = gl_TexCoord[0].st;\n"
        "  vec2 o = u_invSrcSize;\n"
        "  vec3 c0 = texture2D(u_tex, uv + vec2(-0.5, -0.5) * o).rgb;\n"
        "  vec3 c1 = texture2D(u_tex, uv + vec2( 0.5, -0.5) * o).rgb;\n"
        "  vec3 c2 = texture2D(u_tex, uv + vec2(-0.5,  0.5) * o).rgb;\n"
        "  vec3 c3 = texture2D(u_tex, uv + vec2( 0.5,  0.5) * o).rgb;\n"
        "  vec3 c = (c0 + c1 + c2 + c3) * 0.25;\n"
        "  vec3 lin = srgbToLinear(c);\n"
        "  float thr = clamp(u_threshold, 0.0, 1.0);\n"
        "  float knee = max(u_knee, 0.00001);\n"
        "  float br = max(lin.r, max(lin.g, lin.b));\n"
        "  float w = smoothstep(thr - knee, thr + knee, br);\n"
        "  float m = max(br - thr, 0.0);\n"
        "  vec3 outC = lin;\n"
        "  if (br > 0.00001) {\n"
        "    outC *= (m / br);\n"
        "  } else {\n"
        "    outC = vec3(0.0);\n"
        "  }\n"
        "  outC *= w;\n"
        "  gl_FragColor = vec4(outC, 1.0);\n"
        "}\n";
}

static const char *
shader_bloom_blurFragmentSourceInternal(void)
{
    return
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_stepUv;\n"
        "void main() {\n"
        "  vec2 uv = gl_TexCoord[0].st;\n"
        "  vec2 o = u_stepUv;\n"
        "  vec3 sum = texture2D(u_tex, uv).rgb * 0.2270270270;\n"
        "  sum += texture2D(u_tex, uv + o * 1.3846153846).rgb * 0.3162162162;\n"
        "  sum += texture2D(u_tex, uv - o * 1.3846153846).rgb * 0.3162162162;\n"
        "  sum += texture2D(u_tex, uv + o * 3.2307692308).rgb * 0.0702702703;\n"
        "  sum += texture2D(u_tex, uv - o * 3.2307692308).rgb * 0.0702702703;\n"
        "  gl_FragColor = vec4(sum, 1.0);\n"
        "}\n";
}

static const char *
shader_bloom_compositeFragmentSourceInternal(void)
{
    return
        "#version 120\n"
        "uniform sampler2D u_base;\n"
        "uniform sampler2D u_bloom;\n"
        "uniform float u_strength;\n"
        "vec3 srgbToLinear(vec3 c) {\n"
        "  return pow(max(c, vec3(0.0)), vec3(2.2));\n"
        "}\n"
        "vec3 linearToSrgb(vec3 c) {\n"
        "  return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));\n"
        "}\n"
        "void main() {\n"
        "  vec2 uv = gl_TexCoord[0].st;\n"
        "  vec3 base = texture2D(u_base, uv).rgb;\n"
        "  vec3 bloom = texture2D(u_bloom, uv).rgb;\n"
        "  vec3 lin = srgbToLinear(base);\n"
        "  lin += bloom * max(u_strength, 0.0);\n"
        "  gl_FragColor = vec4(linearToSrgb(lin), 1.0);\n"
        "}\n";
}

const char *
shader_bloom_downsampleFragmentSource(void)
{
    return shader_bloom_downsampleFragmentSourceInternal();
}

const char *
shader_bloom_blurFragmentSource(void)
{
    return shader_bloom_blurFragmentSourceInternal();
}

const char *
shader_bloom_compositeFragmentSource(void)
{
    return shader_bloom_compositeFragmentSourceInternal();
}

