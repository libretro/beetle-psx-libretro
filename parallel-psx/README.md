# Parallel PSX

This is a Vulkan implementation of the PlayStation 1 (PSX) graphics chip.
The aim is to be best-in-class for visual quality as well as accuracy for a HW rendered plugin.

Main features:

 - Internal upscaling
 - Multisample anti-aliasing
 - Adaptive smoothing, which aims to make 2D elements smooth, and 3D elements crisp and sharp
 - Full mask bit emulation
 - Full framebuffer emulation
 - Standalone API for easy integration in other emulators
 - RSX dump playback support (can be dumped from Beetle PSX)
 - PGXP integration (sub-pixel precision and perspective correctness)

## Hardware and drivers tested on

 - nVidia Linux (375.xx)
 - AMDGPU-PRO 16.30
 - Mesa Intel, Broadwell GPU (seems to fully work if you build very latest driver from source!)
 - Mesa Radeon (RADV, missing some features for full mask bit emulation)

## Overview

This project consists of three modules.

### Vulkan backend

This is an implementation of a higher-level API which retains most of Vulkan's ways to slash out driver overhead while
being convenient to use. This API was designed to be reusable and might be hoisted out to its own project eventually.

### Framebuffer Atlas

A key component is having the ability to track various access to the PSX framebuffer (1024x512).
The atlas tracks hazards on 8x8 blocks and when hazards are found, Vulkan pipeline barriers and batch flushes occur.
This allows us to render while doing blits to different regions of VRAM without having to fully stall the GPU.

### Renderer

The renderer implements the PSX GPU commands, using both the atlas and the Vulkan backend.

## Building dump player

The dump player is used to play-back and trace dumps which are used to debug games.
Dumps are generated from Beetle-PSX or other programs.

```
git submodule init
git submodule update
mkdir build
cd build
cmake .. -DWSI=GLFW
make -j8
./rsx-player dump.rsx --dump-vram dump/game
```

## Credits

This renderer would not have existed without the excellent Mednafen PSX emulator as well as Rustation PSX renderer.
Otherwise, all code sans submodules in this repository is by Tiny Tiger.
