# Mednafen PSX libretro

This is port of Mednafen PSX core to the libretro API.
It currently runs on Linux and possibly OSX.

## Running

To run this core, a proper configuration for Mednafen must be set up in
<tt>$HOME/.mednafen</tt> as Mednafen itself expects.
Here, at least the BIOS must be set up as it is not distributed.
You should attempt to at least get an ISO image running in the real Mednafen before attempting
to use this port.


## Loading ISOs

Mednafen differs from other PS1 emulators in that it reads a .cue sheet that points to an .iso/.bin whatever.
If you have e.g. <tt>foo.iso</tt>, you should create a foo.cue, and fill this in:

    FILE "foo.iso" BINARY
       TRACK 01 MODE1/2352
          INDEX 01 00:00:00

After that, you can load the <tt>foo.cue</tt> file as a ROM.
Note that this is a dirty hack and will not work on all games.
Ideally, make sure to use rips that have cue-sheets.

