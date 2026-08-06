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

## cdstream_map_test.c

Ownership check for `mednafen/cdstream`: open, close, heap-allocated variant,
and the memcache conversion, followed by a fresh allocation to prove the heap
survived.

**Must be built with `-DHAVE_MMAP`** or it proves nothing. `cdstream_open`
asks the VFS for a file mapping, and without that define no mapping comes
back, `buf` stays NULL, and the branch under test is unreachable. Every other
harness here was built without it, which is exactly how a heap-corrupting
free reached a user.

`cdstream::buf` is always borrowed -- from a VFS mapping whose lifetime
belongs to the RFILE, or from a `data_transfer`'s buffer owned by the
transfer. Nothing ever allocates it, so it must never be freed. The
corruption does not surface at the free either; it surfaces in a later,
unrelated allocation, which is why the reported crash was inside
`RtlFreeHeap` under `CDAccess_Image_ImageOpen`.

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

## make_vcd.py and vcd_disc.c

`make_vcd.py` builds a synthetic but spec-conformant Video CD (CUE + BIN);
`vcd_disc.c` opens it with `CDIF_Open`, reads the TOC, runs `VCD_ProbeDisc`
against the image, and walks track 2 with `CDIF_ReadRawSector` -- the same
call cdc.c's sector tap uses.

```sh
python3 tools/vcd/make_vcd.py stream.mpg out          # add --pal for PAL
```

This is the only test that exercises the real disc path: the CUE parse, the
TOC, Mode 2 sector framing as CDIF hands it over, and whether the probe finds
the control sectors at their mandated LBAs on an actual image. Writing it
found four things, all in the generator, each caught by the real reader
rejecting what a hand-rolled one would have accepted:

CUE `INDEX` times are file-relative -- 00:00:00 is the first sector of the
file, with no lead-in offset. Using the absolute form shifts every track by
two seconds; the TOC then reports track 2 at LBA 600 for a track that starts
at 450.

A Mode 2 Form 1 sector is 12 + 4 + 8 + 2048 + 4 + 276. There is no 8-byte
reserved gap -- Mode 1 has one, and Mode 2 Form 1 spends those bytes on the
subheader instead. Including both makes a 2360-byte sector, and nothing
reports it: the file is a plausible size, sector 0 reads correctly, and every
sector after it is progressively misaligned.

The EDC has to be right. `CDIF_ReadSector` runs each sector through
`edc_lec_check_and_correct` and returns nothing if it fails, so a probe
reading INFO.VCD off an image without a valid EDC gets zero bytes and decides
the disc is not a Video CD -- while raw reads of the same sector work fine,
which makes it look like a probe bug rather than an image one.

The EDC polynomial is the CD one, x^32 + x^31 + x^16 + x^15 + x^4 + x^3 + x +
1, not the ordinary CRC-32 polynomial.

## vcd_pipeline.c

Wraps a real MPEG-1 program stream in Mode 2 Form 2 sectors the way a Video
CD carries it and pushes them through `VCD_FeedSector`, covering the sector
tap, subheader filtering, packet routing to the two decoders, and the YCbCr
to RGB565 conversion.

Build every harness with `tools/vcd/build.sh`:

```sh
sh tools/vcd/build.sh vcd_probe -fsanitize=address,undefined
sh tools/vcd/build.sh vcd_disc  -fsanitize=address,undefined
```

Harnesses: `vcd_probe`, `vcd_pipeline`, `vcd_state`, `cdstream_map_test`,
`vcd_disc`. The binary lands in `$OUT` (default `/tmp/<harness>`).

**Do not hand-write the flags.** `build.sh` gets them from
`tools/harness_cflags.sh`, which reads them back out of the core's own build
with `make -n`, so there is no second list to keep in step. The reason this
exists is that these harnesses previously carried hand-written `-D` lists,
and those drifted: built without `-DHAVE_MMAP`, a harness that opens the same
disc images the core opens never takes the file mapping path, so a heap
corruption that fired on every mapped image was unreachable in test while the
coverage looked complete. Restoring the real flags also pulls in dependencies
the short lists hid -- the CHD stack under `-DHAVE_CHD`, rthreads under
`-DHAVE_THREADS` -- which is the same drift seen from the other side.

Both are clean under ASan and UBSan.
