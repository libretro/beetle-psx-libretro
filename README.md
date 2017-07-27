# Beetle PSX libretro

This is a fork of Mednafen PSX. It has been ported to the libretro API.
It currently runs on Linux, OSX and Windows.

## Running

To run this core, the "system directory" must be defined if running in RetroArch.
The PSX BIOS must be placed there, $sysdir/scph550{0,1,2} for Japanese, NA and EU regions respectively.

Memory cards will be saved to "save directory", memory card #1 is saved using libretro's standard interface. The rest of memory cards are saved using Mednafen's standard mechanism. You might have to rename your old 
memory cards to gamename.srm. Alternatively you may just rename it from gamename.gamenamehash.0.mcr to gamename.gamenamehash.1.mcr and load them off the corresponding slot.

Core now supports save states. Keep in mind states might result on loss your memorycards if you are careless.

## Loading ISOs

Beetle differs from other PS1 emulators in that it needs a cue-sheets that points to an image file, usually an .iso/.bin file.
If you have e.g. <tt>foo.iso</tt>, you should create a foo.cue, and fill this in:

    FILE "foo.iso" BINARY
       TRACK 01 MODE1/2352
          INDEX 01 00:00:00

After that, you can load the <tt>foo.cue</tt> file as a ROM.
Note that this is a dirty hack and will not work on all games.
Ideally, make sure to use rips that have cue-sheets.

If foo is a multiple-disk game, you should have .cue files for each one, e.g. <tt>foo (Disc 1).cue</tt>, <tt>foo (Disc 2).cue</tt>, <tt>foo (Disc 3).cue</tt>.To take advantage of Beetle's Disk Control feature for disk swapping, an index file should be made.

Open a text file and enter your game's .cue files on it, like this:

    foo (Disc 1).cue
    foo (Disc 2).cue
    foo (Disc 3).cue

Save as foo.m3u and use this file in place of each disk's individual cue sheet.

## Condensing Games

Alternatively to using cue sheets with .bin/.iso files, you can convert your games to .pbp (Playstation Portable update file) to reduce file sizes and neaten up your game folder. If converting a multiple-disk game, all disks should be added to the same .pbp file, rather than making a .m3u file for them.

Most conversion tools will want a single .bin/.iso file for each disk. If your game uses multiple .bin files (tracks) per disk, you will have to mount the cue sheet to a virtual drive and re-burn the images onto a single track before conversion.

Note that RetroArch does not currently have .pbp database due to variability in users' conversion methods. All .pbp games will have to be added to playlists manually.

## Suggested Firmware

- scph5500.bin (8dd7d5296a650fac7319bce665a6a53c)
- scph5501.bin (490f666e1afb15b7362b406ed1cea246)
- scph5502.bin (32736f17079d0b2b7024407c39bd3050)

## Options

* Renderer (restart) - 'software', 'vulkan' and 'opengl'. The last two options will enable and/or speedup enhancements like upscaling and texture filtering.
* Software framebuffer - If off, the software renderer will skip some steps. Potential speedup. Causes bad graphics when doing framebuffer readbacks.
* Adaptive smoothing - When upscaling, smooths out 2D elements while keeping 3D elements sharp. Vulkan renderer only at the moment.
* Internal GPU resolution - Graphics upscaling.
* Texture filtering - Per-texture filtering using e.g. xBR, SABR, bilinear, etc. OpenGL only at the moment.
* Internal color depth - PSX had 16bpp depth, beetle-psx can go up to 32bpp. OpenGL only at the moment. Vulkan always uses 32bpp.
* Wireframe mode - For debug use. Shows only the outlines of polygons. OpenGL only.
* Display full VRAM - Everything in VRAM is drawn on screen.
* PGXP operation mode - When not off, floating point coordinates will be used for vertex positions, to avoid the PSX polygon jitter. 'memory + cpu' mode can further reduce jitter at the cost of performance and geometry glitches.
* PGXP vertex cache - Maintains a cache for vertices. May result in better performance but can result in graphics glitches in most games.
* PGXP perspective correct texturing - Original PSX did affine texture mapping, resulting in e.g. crooked lines across walls. This fixes it.
* Dithering pattern - If off, disables the dithering pattern the PSX applies to combat color banding. OpenGL only. Vulkan always disables the pattern.
* Scale dithering pattern with internal resolution - Self-explanatory. OpenGL only.
* Initial Scanline - Sets the first scanline to be drawn on screen.
* Initial Scanline PAL - Sets the first scanline to be drawn on screen for PAL systems.
* Last Scanline - Sets the last scanline to be drawn on screen.
* Last Scanline PAL - Sets the last scanline to be drawn on screen for PAL systems.
* Frame duping (speedup) - Redraws/reuses the last frame if there was no new data.
* Widescreen mode hack - If on, renders in 16:9. Works best on 3D games.
* Crop Overscan - Self-explanatory.
* Additional cropping - Self-explanatory.
* Offset cropped image - Self-explanatory.
* Display internal FPS - Shows the frame rate at which the emulated PSX is drawing at.
* Analog self-calibration - Monitors the max values reached by the input, using it as a calibration heuristic which then scales the analog coordinates sent to the emulator accordingly.
For best results, rotate the sticks at max amplitude for the algorithm to get a good estimate of the scaling factor, otherwise it will adjust while playing content.
* DualShock Analog button toggle - Toggles the Analog button from DualShock controllers, if disabled analogs are always on, if enabled you can toggle their state by pressing and holding START+SELECT+L1+L2+R1+R2.
* Port 1: Multitap Enable - Enables/Disables multitap functionality on port 1.
* Port 2: Multitap Enable - Enables/Disables multitap functionality on port 2.
* CPU Overclock - Gets rid of memory access latency and makes all GTE instructions have 1 cycle latency.
* CD Image Cache - Loads the complete image in memory at startup.
* Skip BIOS - Self-explanatory. Some games have issues when enabled.
* Memcard 0 method - Picks the format (libretro or mednafen) used for storing memcard 0 save data.
* Enable memory card 1 - Specifically enables memcard slot 1. Needed for game "Codename Tenka".
* Shared memory cards (restart) - Stores everything in the same savefile. 'Memcard 0 method' needs to be set to 'libretro'.
