#ifndef VRAM_H
#define VRAM_H

layout(location = 1) in mediump vec2 vUV;
layout(location = 2) flat in mediump ivec3 vParam;
layout(location = 3) flat in mediump ivec2 vBaseUV;
layout(location = 4) flat in mediump ivec4 vWindow;
layout(set = 0, binding = 0) uniform mediump usampler2D uFramebuffer;

vec4 sample_vram_atlas(ivec2 uv)
{
    ivec3 params = vParam;
    int shift = params.z & 3;

    uv = (uv & vWindow.xy) | vWindow.zw;

    ivec2 coord;
    if (shift != 0)
    {
        int bpp = 16 >> shift;
        coord = ivec2(uv);
        int phase = coord.x & ((1 << shift) - 1);
        int align = bpp * phase;
        coord.x >>= shift;
        int value = int(texelFetch(uFramebuffer, (vBaseUV + coord) & ivec2(1023, 511), 0).x);
        int mask = (1 << bpp) - 1;
        value = (value >> align) & mask;

        params.x += value;
        coord = params.xy;
    }
    else
        coord = vBaseUV + uv;

    return abgr1555(texelFetch(uFramebuffer, coord & ivec2(1023, 511), 0).x);
}

vec4 sample_vram_bilinear(vec4 NNColor)
{
    vec2 base = vUV;
    ivec2 ibase = ivec2(base);
    vec4 c01 = sample_vram_atlas(ibase + ivec2(0, 1));
    vec4 c10 = sample_vram_atlas(ibase + ivec2(1, 0));
    vec4 c11 = sample_vram_atlas(ibase + ivec2(1, 1));
    float u = fract(base.x);
    float v = fract(base.y);
    vec4 x0 = mix(NNColor, c10, u);
    vec4 x1 = mix(c01, c11, u);
    return mix(x0, x1, v);
}

#endif
