# RHI API and OpenGL 3.3 Renderer

## RHI API

The RHI API is the means by which components of the Beetle PSX libretro core interface with the hardware renderers. The RHI API offers two general classes of functions for the core. The first class consists of various functions used to perform libretro-specific actions such as reading core options or preparing/finalizing the current emulation loop frame. The second class of functions consists of RHI interface functions that the emulated PSX GPU uses to issue commands to the hardware renderer. Not every emulated PSX GPU command has a corresponding RHI interface function, but the set of available functions can be extended or modified as necessary when bugs are discovered and higher accuracy is required.

Each unique hardware renderer will implement RHI interface functions as another layer of function calls, typically but not necessarily one per RHI interface function. The RHI interface should then select the correct function to call based on the currently running hardware renderer.

The RHI API also includes support for dumping RHI API calls to file, which can be utilized for debugging purposes by any renderers that implement RHI playback.

## OpenGL 3.3 Renderer

The OpenGL renderer is currently implemented in `rhi_lib_gl.c` and can be called via the functions exposed in `rhi_lib_gl.h`.

## Building

The RHI API and OpenGL renderer are components of the Beetle PSX libretro core. To build with OpenGL support, run `make HAVE_OPENGL=1` in the repository's top level directory. To build with all possible hardware renderers, instead run `make HAVE_HW=1`. To build with dump support, additionally pass `RHI_DUMP=1`.

## Coding Style

The preferred coding style for the rhi subdirectory is the libretro coding style. See: https://docs.libretro.com/development/coding-standards/

## Credits

The OpenGL renderer was originally authored by simias as a port/plugin of Rustation's GL renderer to Beetle PSX.
