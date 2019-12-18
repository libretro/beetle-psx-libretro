#ifndef COMMON_H
#define COMMON_H

vec4 abgr1555(uint value)
{
   // This rounding is very deliberate, we want 0b11111 to become 0b11111000 in UNORM.
   // We keep the msb/lsb separate to support palettes correctly as the msbs should be reinterpreted as 4x4 or 2x8 bits.
   // Overall, we get 10 bits of precision for RGB, weee! :3
   uvec4 msb = (uvec4(value) >> uvec4(0u, 5u, 10u, 15u)) & uvec4(31u, 31u, 31u, 1u);
   uvec3 lsb = (uvec3(value) >> uvec3(16u, 21u, 26u)) & uvec3(31u);
   uvec4 rgba = uvec4((msb.rgb << 5u) | lsb, msb.a);
#define SCALING (1.0 / (4.0 * 255.0))
   return vec4(rgba) * vec4(SCALING, SCALING, SCALING, 1.0);
}

uvec2 unpack2x16(uint x)
{
   return uvec2(x & 0xffffu, x >> 16u);
}

uvec4 unpack4x8(uint x)
{
   return (uvec4(x) >> uvec4(0u, 8u, 16u, 24u)) & 0xffu;
}

uint pack_abgr1555(vec4 value)
{
   // This rounding is very deliberate, we want 0b11111 to become 0b11111000 in UNORM.
   // We keep the msb/lsb separate to support palettes correctly as the msbs should be reinterpreted as 4x4 or 2x8 bits.
   // Overall, we get 10 bits of precision for RGB, weee! :3
#define INV_SCALING (4.0 * 255.0)

   // Need to make sure that we round so we can get a stable roundtrip with abgr1555 -> pack_abgr1555.
   uvec4 rgba = uvec4(round(clamp(value, vec4(0.0), vec4(1.0)) * vec4(INV_SCALING, INV_SCALING, INV_SCALING, 1.0)));
   uvec3 msb = rgba.rgb >> 5u;
   uvec3 lsb = rgba.rgb & 31u;
   return (msb.r << 0u) | (msb.g << 5u) | (msb.b << 10u) | (rgba.a << 15u) | (lsb.r << 16u) | (lsb.g << 21u) | (lsb.b << 26u);
}

#endif
