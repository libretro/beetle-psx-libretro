#version 450

layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
#ifdef TEXTURED
layout(location = 2) in mediump uvec4 Window;
layout(location = 3) in mediump ivec3 Param;
layout(location = 4) in ivec4 UV;
layout(location = 5) in mediump uvec4 UVRange;

layout(location = 1) out mediump vec2 vUV;
layout(location = 2) flat out mediump ivec3 vParam;
layout(location = 3) flat out mediump ivec2 vBaseUV;
layout(location = 4) flat out mediump ivec4 vWindow;
layout(location = 5) flat out mediump ivec4 vTexLimits;
#endif
layout(location = 0) out mediump vec4 vColor;

const vec2 FB_SIZE = vec2(1024.0, 512.0);
//const vec4 texture_limits = vec4(0.0, 0.0, 1024.0, 1024.0);

void main()
{
   gl_Position = vec4(Position.xy / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
   vColor = Color;
#ifdef TEXTURED
   // iCB: Offset UVs by half a pixel to account for rounding errors in projection
   vUV = vec2(UV.xy) + vec2(0.5 / 1024.0, 0.5 / 512.0);
   vParam = Param;
   vBaseUV = UV.zw;
   vWindow = ivec4(Window);
   vTexLimits = ivec4(UVRange);
#endif
}
