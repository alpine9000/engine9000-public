/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "shader_base.h"

static const char *
shader_base_crtFragmentSourceInternal(void)
{
    return
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_texSize;\n"
        "uniform float u_geom;\n"
        "uniform float u_scan;\n"
        "uniform float u_beam;\n"
        "uniform float u_border;\n"
        "uniform float u_overscan;\n"
        "void main() {\n"
        "  vec2 uv = gl_TexCoord[0].st;\n"
        "  vec2 p = uv * 2.0 - 1.0;\n"
        "  float r2 = dot(p, p);\n"
        "  float k = 0.04;\n"
        "  float scale = 1.0 - k * r2;\n"
        "  scale = max(scale, 0.5);\n"
        "  float overscan = u_overscan;\n"
        "  float geo = step(0.5, u_geom);\n"
        "  float useScale = mix(1.0, scale, geo);\n"
        "  float useOverscan = mix(1.0, overscan, geo);\n"
        "  vec2 p2 = (p / useScale) / useOverscan;\n"
        "  vec2 uv2 = (p2 + 1.0) * 0.5;\n"
        "  if (uv2.x < 0.0 || uv2.x > 1.0 || uv2.y < 0.0 || uv2.y > 1.0) {\n"
        "    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "    return;\n"
        "  }\n"
        "  vec4 c = texture2D(u_tex, uv2);\n"
        "  float linePos = uv2.y * u_texSize.y;\n"
        "  float frac = fract(linePos);\n"
        "  float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
        "  float beamFrac = frac - 0.5;\n"
        "  float beam = 0.50 + 0.50 * exp(-(beamFrac * beamFrac) / (0.18 * 0.18));\n"
        "  float scanDark = mix(0.80, 0.94, lum);\n"
        "  float line = floor(linePos);\n"
        "  float alt = (mod(line, 2.0) > 0.5) ? scanDark : 1.0;\n"
        "  float scanEnable = step(0.5, u_scan);\n"
        "  float scan = mix(1.0, alt, scanEnable);\n"
        "  float beamEnable = step(0.5, u_beam);\n"
        "  float beamOut = mix(1.0, beam, beamEnable);\n"
        "  float vignette = 1.0 - 0.25 * r2;\n"
        "  vignette = clamp(vignette, 0.6, 1.0);\n"
        "  float vignetteOut = mix(1.0, vignette, geo);\n"
        "  c.rgb *= scan * beamOut * vignetteOut;\n"
        "  gl_FragColor = vec4(c.rgb, 1.0);\n"
        "}\n";
}

const char *
shader_base_crtFragmentSource(void)
{
    return shader_base_crtFragmentSourceInternal();
}

