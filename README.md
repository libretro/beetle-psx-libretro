# Mednafen PSX libretro

This is port of Mednafen PSX core to the libretro API.
It currently runs on Linux and possibly OSX.

## Running

To run this core, a proper configuration for Mednafen must be set up in
$HOME/.mednafen as Mednafen itself expects.
Here, at least the BIOS must be set up as it is not distributed.
You should attempt to at least get an ISO image running in the real mednafen before attempting
to use this port.


## Loading ISOs

Mednafen differs from other PS1 games in that it reads a .cue sheet that points to an .iso.
If you have e.g. <tt>foo.iso</tt>, you should create a foo.cue, and fill this in:

    FILE "foo.iso" BINARY
       TRACK 01 MODE1/2352
          INDEX 01 00:00:00

After that, you can load the <tt>foo.cue</tt> file as a ROM.

