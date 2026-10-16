#version 450
precision highp float;
precision highp int;

#include "primitive_shading.h"

void main()
{
	bool enhanced_texel;
	float blend_amt;
	primitive_source(FragColor, enhanced_texel, blend_amt);
}
