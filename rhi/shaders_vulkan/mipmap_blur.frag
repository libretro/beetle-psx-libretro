#version 450
precision highp float;
precision highp int;

layout(location = 0) out float FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
   vec2 offset;
   vec2 range;
   vec2 inv_resolution;
   vec2 uv_min;
   vec2 uv_max;
} registers;

void main()
{
   float bias = 0.0;
   const float w0 = 0.25;
   const float w1 = 0.125;
   const float w2 = 0.0625;
#define UV(x, y) clamp((vUV + vec2(x, y) / vec2(1024.0, 512.0)), registers.uv_min, registers.uv_max)
   bias += w2 * textureLod(uTexture, UV(-1.0, -1.0), 0.0).a;
   bias += w2 * textureLod(uTexture, UV(+1.0, -1.0), 0.0).a;
   bias += w2 * textureLod(uTexture, UV(-1.0, +1.0), 0.0).a;
   bias += w2 * textureLod(uTexture, UV(+1.0, +1.0), 0.0).a;
   bias += w1 * textureLod(uTexture, UV( 0.0, -1.0), 0.0).a;
   bias += w1 * textureLod(uTexture, UV(-1.0,  0.0), 0.0).a;
   bias += w1 * textureLod(uTexture, UV(+1.0,  0.0), 0.0).a;
   bias += w1 * textureLod(uTexture, UV( 0.0, +1.0), 0.0).a;
   bias += w0 * textureLod(uTexture, UV( 0.0,  0.0), 0.0).a;
   FragColor = bias;
}
