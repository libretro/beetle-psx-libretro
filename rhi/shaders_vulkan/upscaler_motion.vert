#version 450
/* The temporal upscaler's motion vector pass (rhi_upscaler.c): a
 * primitive's triangles where it was drawn, in render pixels, each
 * vertex with the order it was drawn in (tested), its depth for the
 * upscaler and how far it moved from the frame before. */
layout(location = 0) in vec4 Position;   /* x, y, order, depth */
layout(location = 1) in vec2 Motion;
layout(location = 0) out vec2 vMotion;
layout(location = 1) out float vDepth;

layout(push_constant, std430) uniform Registers
{
   vec2 inv_resolution;
   /* as primitive.vert rasterizes at this scale */
   layout(offset = 16) vec2 offset;
} registers;

void main()
{
   gl_Position = vec4((Position.xy + registers.offset) * registers.inv_resolution * 2.0 - 1.0, Position.z, 1.0);
   vMotion     = Motion;
   vDepth      = Position.w;
}
