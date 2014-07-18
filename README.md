# Beetle PSX libretro

This is fork of Mednafen PSX. It has been ported to the libretro API.
It currently runs on Linux, OSX and possibly Windows.

## Running

To run this core, the "system directory" must be defined if running in RetroArch.
The PSX BIOS must be placed there, $sysdir/SCPH550{0,1,2} for Japanese, NA and EU regions respectively.

Memory cards will be saved to "save directory"

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

## Suggested Firmware

- scph5500.bin (8dd7d5296a650fac7319bce665a6a53c)
- scph5501.bin (490f666e1afb15b7362b406ed1cea246)
- scph5502.bin (32736f17079d0b2b7024407c39bd3050)

## Options

* CD Image Cache - Loads the complete image in memory at startup
* PSX Dithering - Enables Dithering
* PSX Initial Scanline - Sets the first scanline to be drawn on screen
* PSX Initial Scanline PAL - Sets the first scanline to be drawn on screen for PAL systems
* PSX Last Scanline - Sets the last scanline to be drawn on screen
* PSX Last Scanline PAL - Sets the last scanline to be drawn on screen for PAL systems
* Dualshock analog toggle - Enables/Disables the analog button from Dualshock controllers, if disabled analogs are always on, if enabled you can toggle it's state with START+SELECT+L1+L2+R1+R2
* Port 1 PSX Enable Multitap - Enables/Disables multitap functionality on port 1
* Port 2 PSX Enable Multitap - Enables/Disables multitap functionality on port 2
