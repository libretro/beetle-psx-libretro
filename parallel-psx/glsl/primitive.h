#ifndef PRIMITIVE_H
#define PRIMITIVE_H

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
     #include "vram.h"
#endif
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D uDitherLUT;

#if 0
vec3 quantize_bgr555(vec3 color)
{
	return round(color * 31.0) / 31.0;
}
#endif
#endif
