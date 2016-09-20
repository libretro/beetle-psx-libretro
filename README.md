# Beetle PSX libretro

This is a fork of Mednafen PSX. It has been ported to the libretro API.
It currently runs on Linux, OSX and Windows.

## Running

To run this core, the "system directory" must be defined if running in RetroArch.
The PSX BIOS must be placed there, $sysdir/SCPH550{0,1,2} for Japanese, NA and EU regions respectively.

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

* Renderer (restart) - 'software' or 'opengl'. 'opengl' uses the OpenGL API to accelerate tasks like upscaling.
* Software framebuffer - If off, the software renderer will skip some steps. Potential speedup. Causes bad graphics when doing framebuffer readbacks.
* CD Image Cache - Loads the complete image in memory at startup.
* CPU Overclock - Gets rid of memory access latency and makes all GTE instructions have 1 cycle latency.
* Skip BIOS - Self-explanatory. Some games have issues when enabled.
* Widescreen mode hack - If on, renders in 16:9. Works best on 3D games.
* Internal GPU resolution - Graphics upscaling.
* Texture filtering - Self-explanatory.
* Internal color depth - PSX had 16bpp depth, beetle-psx can go up to 32bpp.
* Scale dithering pattern with internal resolution - Self-explanatory
* Wireframe mode - For debug use. Shows only the outlines of polygons. 
* Display full VRAM - Everything in VRAM is drawn on screen.
* Dithering pattern - If off, disables the dithering pattern the PSX applies to combat color banding.
* GTE pixel accuracy - If on, uses floating point coordinates instead of integer ones for vertex positions, to avoid the PSX poly jitter. This option does nothing at the moment.
* Memcard 0 method - Picks the format (libretro or mednafen) used for storing memcard 0 save data.
* Enable memory card 1 - Specifically enables memcard slot 1. Needed for game "Codename Tenka".
* Shared memory cards (restart) - Stores everything in the same savefile. 'Memcard 0 method' needs to be set to 'libretro'.
* Initial Scanline - Sets the first scanline to be drawn on screen.
* Initial Scanline PAL - Sets the first scanline to be drawn on screen for PAL systems.
* Last Scanline - Sets the last scanline to be drawn on screen.
* Last Scanline PAL - Sets the last scanline to be drawn on screen for PAL systems.
* DualShock Analog button toggle - Toggles the Analog button from DualShock controllers, if disabled analogs are always on, if enabled you can toggle their state by pressing and holding START+SELECT+L1+L2+R1+R2.
* Port 1 PSX Enable Multitap - Enables/Disables multitap functionality on port 1.
* Port 2 PSX Enable Multitap - Enables/Disables multitap functionality on port 2.
* Frame duping (speedup) - Redraws/reuses the last frame if there was no new data.
* Display internal FPS - Shows the frame rate at which the emulated PSX is drawing at.
* Crop Overscan - Self-explanatory.
* Offset cropped image - Self-explanatory.
* Additional cropping - Self-explanatory.
