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

## vcd_state.c

`VCD_StateAction` round-trip through the real SFORMAT machinery. Drives the
transport into a non-default state, saves, perturbs mode/transport/pad, loads,
and checks the transport came back -- and, the part worth testing, that the
decoders were *dropped* rather than carried across, then re-prime from the
restored position.

The decoders' internal state is deliberately not serialised: reference
pictures, the MP2 bit reservoir and the demuxer window are large, belong to
modules with no state-export API, and are entirely recoverable because a Video
CD is losslessly re-readable. Dropping and re-priming costs one GOP of latency
after a load. It also has to happen unconditionally -- a stale reference
picture surviving a load would be predicted from.

## vcd_pipeline.c

Wraps a real MPEG-1 program stream in Mode 2 Form 2 sectors the way a Video
CD carries it and pushes them through `VCD_FeedSector`, covering the sector
tap, subheader filtering, packet routing to the two decoders, and the YCbCr
to RGB565 conversion.

All three share a build line; `state_stub.c` satisfies `mednafen/state.c`'s
reference to the core's whole-machine `StateAction`, which these harnesses
never reach:

```sh
gcc -O1 -g -std=gnu99 -fsanitize=address,undefined \
    -DMEDNAFEN_VERSION_NUMERIC=9386 \
    -o vcdprobe tools/vcd/vcd_probe.c \
    mednafen/psx/vcd.c mednafen/state.c tools/vcd/state_stub.c \
    libretro-common/compat/compat_strl.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c \
    libretro-common/formats/mpeg1/rmpeg1_video.c \
    libretro-common/formats/mp3/rmp3.c \
    -Ilibretro-common/include -Imednafen -I. -lm
```

Substitute `vcd_state.c` or `vcd_pipeline.c` for the harness.

Both are clean under ASan and UBSan.
