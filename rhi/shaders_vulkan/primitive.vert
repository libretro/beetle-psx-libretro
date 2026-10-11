#version 450

layout(location = 0) in highp uvec4 Position;
layout(location = 1) in highp uvec4 Color;
layout(location = 3) in mediump ivec3 Param;
#ifdef TEXTURED
layout(location = 2) in mediump uvec4 Window;
layout(location = 4) in ivec4 UV;
layout(location = 5) in highp uvec4 UVRange;
layout(location = 7) in highp uint UVPlane;
layout(location = 10) flat out highp uvec2 vUVOrigin;
layout(location = 11) flat out highp uvec2 vUVDX;
layout(location = 12) flat out highp uvec2 vUVDY;

layout(location = 1) out highp vec2 vUV;
layout(location = 3) flat out mediump ivec2 vBaseUV;
layout(location = 4) flat out mediump ivec4 vWindow;
/* highp: see vram.h. UINT16_MAX limits do not survive a 16-bit int. */
layout(location = 5) flat out highp ivec4 vTexLimits;
#endif
/* Matches primitive.h: native RGB5 truncation requires full precision at
 * the fragment input on implementations where mediump maps to FP16. */
layout(location = 0) out highp vec4 vColor;
layout(location = 2) flat out mediump ivec3 vParam;
/* Depth-cue sidecar: far colour + blend factor. t == 0 (the no-cue
 * encoding) makes the fragment mix the identity. */
layout(location = 6) in highp uvec4 Fog;
layout(location = 6) out highp vec4 vFog;
layout(location = 7) flat out highp uvec3 vColorOrigin;
layout(location = 8) flat out highp uvec3 vColorDX;
layout(location = 9) flat out highp uvec3 vColorDY;

const vec2 FB_SIZE = vec2(1024.0, 512.0);
//const vec4 texture_limits = vec4(0.0, 0.0, 1024.0, 1024.0);

layout(constant_id = 5) const int OFFSET_UV = 0;

void main()
{
   vec4 position = uintBitsToFloat(Position);
#ifdef TEXTURED
   if ((uint(Param.z) & 0x0008u) != 0u)
   {
      /* Native positions are signed-16 integral XY, with unchanged depth.
       * The other two position lanes carry the first wrapped UV plane. */
      ivec2 xy = ivec2(int(Position.x << 16u) >> 16, int(Position.x) >> 16);
      position = vec4(vec2(xy), uintBitsToFloat(Position.z), 1.0);
   }
#endif
   vec2 off = vec2(0.5, 0.5);
#ifdef UNSCALED
   gl_Position = vec4((position.xy + off) / FB_SIZE * 2.0 - 1.0, position.z, 1.0) * position.w;
#else
   gl_Position = vec4(position.xy / FB_SIZE * 2.0 - 1.0, position.z, 1.0) * position.w;
#endif
   if ((uint(Param.z) & 0x0004u) != 0u)
   {
      /* Each pair holds an 8.12 origin and two wrapped 8.12 slopes.
       * Decode once per vertex; fragments then use integer pixel positions. */
      uvec3 low = uvec3(Color.x, Color.z, Fog.x);
      uvec3 high = uvec3(Color.y, Color.w, Fog.y);
      vColorOrigin = low & 0xfffffu;
      vColorDX = (low >> 20u) | ((high & 0xffu) << 12u);
      vColorDY = (high >> 8u) & 0xfffffu;
      vColor = vec4((uvec4(Fog.z) >> uvec4(0u, 8u, 16u, 24u)) & 255u) / 255.0;
      vFog = vec4(0.0);
   }
   else
   {
      vColor = uintBitsToFloat(Color);
      vFog = uintBitsToFloat(Fog);
      vColorOrigin = vColorDX = vColorDY = uvec3(0u);
   }
   vParam = Param;
#ifdef TEXTURED
   // iCB: Offset UVs by half a pixel to account for rounding errors in projection
#ifdef UNSCALED
   vUV = vec2(UV.xy) + off;
#else
   if (OFFSET_UV > 0)
      vUV = vec2(UV.xy) + off;
   else
      vUV = vec2(UV.xy);
#endif
   uvec2 low = uvec2(Position.y, UVPlane);
   uvec2 high = uvec2(Position.w, Fog.w);
   /* The GP0 native path has no PGXP cue. Restore the zero factor while
    * retaining the far colour and all enhanced shading inputs. */
   if ((uint(Param.z) & 0x0008u) != 0u)
      vFog.a = 0.0;
   vUVOrigin = low & 0xfffffu;
   vUVDX = (low >> 20u) | ((high & 0xffu) << 12u);
   vUVDY = (high >> 8u) & 0xfffffu;
   vBaseUV = UV.zw;
   vWindow = ivec4(Window);
   vTexLimits = ivec4(UVRange);
#endif
}
