#version 310 es
precision mediump float;

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 0, binding = 1) uniform sampler2D uLOD;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    vec2 offset;
    vec2 range;
    vec2 uv_min;
    vec2 uv_max;
   float max_bias;
} registers;

void main()
{
   vec2 lod_uv = clamp(vUV, registers.uv_min, registers.uv_max);
   float b = textureLod(uLOD, lod_uv, 0.0).x;
   FragColor = textureLod(uTexture, lod_uv, registers.max_bias * b);
}

