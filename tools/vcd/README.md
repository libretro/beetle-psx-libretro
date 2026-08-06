# Video CD test harnesses

Not built by the core.

## vcd_probe.c

`VCD_ProbeDisc` decides whether a disc is a Video CD at all and what its
chapter list is, so everything downstream is gated on it -- and it is the one
part of the path a decode test cannot reach. This builds the control sectors
to the letter of the spec (PVD at LBA 16 with the CD-BRIDGE system
identifier, INFO.VCD at 150, ENTRIES.VCD at 151, big-endian fields, BCD MSF)
and serves them through a stub `CDIF_ReadSector`.

Covers detection of VCD 1.1, VCD 2.0, SVCD and HQ-VCD; the PAL flag and PBC
flag; BCD MSF to LBA conversion; rejection of an ordinary PSX disc, a
non-ISO9660 disc and an unreadable INFO; and malformed input -- an
out-of-range MSF entry, and an entry count larger than the array.

A disc whose INFO identifies it as a Video CD but whose ENTRIES is corrupt
keeps its type and returns an empty chapter list. The type comes from INFO,
not from ENTRIES, and the caller has to handle a short list regardless.

## vcd_pipeline.c

Wraps a real MPEG-1 program stream in Mode 2 Form 2 sectors the way a Video
CD carries it and pushes them through `VCD_FeedSector`, covering the sector
tap, subheader filtering, packet routing to the two decoders, and the YCbCr
to RGB565 conversion.

```sh
gcc -O1 -g -std=gnu99 -fsanitize=address,undefined -o vcdprobe \
    tools/vcd/vcd_probe.c mednafen/psx/vcd.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c \
    libretro-common/formats/mpeg1/rmpeg1_video.c \
    libretro-common/formats/mp3/rmp3.c \
    -Ilibretro-common/include -Imednafen -I. -lm
```

Both are clean under ASan and UBSan.
