#ifndef _SHADERS_COMMON
#define _SHADERS_COMMON

#ifdef HAVE_OPENGLES3
#define GLSL_VERTEX(src) "#version 300 es\n" #src
#define GLSL_FRAGMENT(src) "#version 300 es\n#ifdef GL_ES\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n#endif\n" #src
#else
#define GLSL_VERTEX(src) "#version 330 core\n" #src
#define GLSL_FRAGMENT(src) "#version 330 core\n" #src
#endif
#define STRINGIZE(src) #src

#endif
