#version 450

/* Fullscreen pass-through for the analog chain. Deliberately declares no push
 * constants: the three analog fragment stages each have their own layout, and
 * quad.vert's offset/range block would collide with all of them. UV is the
 * plain normalised position - the fragment stages do their own addressing off
 * gl_FragCoord for the base-clock sample index. */

layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vUV;

void main()
{
	gl_Position = vec4(Position, 0.0, 1.0);
	vUV = 0.5 * Position + 0.5;
}
