#version 450
precision highp float;
precision highp int;

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
   vec2 offset;
   vec2 range;
   vec2 inv_resolution;
} registers;

vec4 get_bias(vec3 c00, vec3 c01, vec3 c10, vec3 c11)
{
   // Measure the "energy" (variance) in the pixels.
   // If the pixels are all the same (2D content), use maximum bias, otherwise, taper off quickly back to 0 (edges)
   vec3 avg = 0.25 * (c00 + c01 + c10 + c11);
   float s00 = dot(c00 - avg, c00 - avg);
   float s01 = dot(c01 - avg, c01 - avg);
   float s10 = dot(c10 - avg, c10 - avg);
   float s11 = dot(c11 - avg, c11 - avg);
   return vec4(avg, 1.0 - log2(1000.0 * (s00 + s01 + s10 + s11) + 1.0));
}

vec4 get_bias(vec4 c00, vec4 c01, vec4 c10, vec4 c11)
{
   // Measure the "energy" (variance) in the pixels.
   // If the pixels are all the same (2D content), use maximum bias, otherwise, taper off quickly back to 0 (edges)
   float avg = 0.25 * (c00.a + c01.a + c10.a + c11.a);
   vec4 bias = get_bias(c00.rgb, c01.rgb, c10.rgb, c11.rgb);
   bias.a *= avg;
   return bias;
}

void main()
{
   vec2 uv = vUV - 0.25 * registers.inv_resolution;
#ifdef FIRST_PASS
   vec3 c00 = textureLodOffset(uTexture, uv, 0.0, ivec2(0, 0)).rgb;
   vec3 c01 = textureLodOffset(uTexture, uv, 0.0, ivec2(0, 1)).rgb;
   vec3 c10 = textureLodOffset(uTexture, uv, 0.0, ivec2(1, 0)).rgb;
   vec3 c11 = textureLodOffset(uTexture, uv, 0.0, ivec2(1, 1)).rgb;
   FragColor = get_bias(c00, c01, c10, c11);
#else
   vec4 c00 = textureLodOffset(uTexture, uv, 0.0, ivec2(0, 0));
   vec4 c01 = textureLodOffset(uTexture, uv, 0.0, ivec2(0, 1));
   vec4 c10 = textureLodOffset(uTexture, uv, 0.0, ivec2(1, 0));
   vec4 c11 = textureLodOffset(uTexture, uv, 0.0, ivec2(1, 1));
   FragColor = get_bias(c00, c01, c10, c11);
#endif
}
