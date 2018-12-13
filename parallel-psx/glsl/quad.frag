#version 450
precision highp float;
precision highp int;

#include "common.h"

layout(location = 0) in highp vec2 vUV;

#if defined(SCALED)
layout(set = 0, binding = 0) uniform sampler2D uTexture;
#elif defined(UNSCALED)
layout(set = 0, binding = 0) uniform mediump usampler2D uTexture;
#endif
layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec2 offset;
    vec2 range;
} registers;

void main()
{
#if defined(SCALED)
    FragColor = vec4(textureLod(uTexture, vUV, 0.0).rgb, 1.0);
#elif defined(UNSCALED)
#if defined(BPP24)
    ivec2 coord = ivec2((vUV - registers.offset) * vec2(1024.0, 512.0));
    int base_x = (coord.x * 3) >> 1;
    int shift = 8 * (coord.x & 1);
    coord.x = base_x;
    vec2 uv = (vec2(coord) + 0.5) / vec2(1024.0, 512.0) + registers.offset;
    uint value = (textureLod(uTexture, uv, 0.0).x & 0xffffu) | (textureLodOffset(uTexture, uv, 0.0, ivec2(1, 0)).x << 16u);
    value >>= uint(shift);

    vec3 rgb = vec3((uvec3(value) >> uvec3(0u, 8u, 16u)) & 0xffu) / 255.0;
    FragColor = vec4(rgb, 1.0);
#else
    uint value = textureLod(uTexture, vUV, 0.0).x;
    FragColor = vec4(abgr1555(value).rgb, 1.0);
#endif
#endif
}
