[![Build Status](https://travis-ci.org/libretro/beetle-psx-libretro.svg?branch=master)](https://travis-ci.org/libretro/beetle-psx-libretro)
[![Build status](https://ci.appveyor.com/api/projects/status/qd1ew088woadbqhc/branch/master?svg=true)](https://ci.appveyor.com/project/bparker06/beetle-psx-libretro/branch/master)

# Beetle PSX libretro

Beetle PSX is a port/fork of Mednafen's PSX module to the libretro API. It can be compiled in C++98 mode, excluding the Vulkan renderer, which is written in C++11 for the time being. Beetle PSX currently runs on Linux, OSX and Windows.

Notable additions in this fork are:
* PBP and CHD file format support, developed by Zapeth;
* Software renderer internal resolution upscaling, implemented by simias;
* An OpenGL 3.3 renderer, developed by simias;
* A Vulkan renderer, developed by TinyTiger;
* PGXP perspectve correct texturing and subpixel precision, developed by iCatButler;

## Documentation

https://docs.libretro.com/library/beetle_psx_hw/
