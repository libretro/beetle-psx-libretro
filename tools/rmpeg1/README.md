# rmpeg1 test harnesses

Not built by the core. Build them by hand against the tree:

```sh
# differential test vs the pl_mpeg demuxer (needs deps/pl_mpeg)
gcc -O2 -std=gnu99 -o diffdemux tools/rmpeg1/diff_demux.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c \
    -Ilibretro-common/include -Ideps/pl_mpeg -lm

# robustness sweep under ASan/UBSan
gcc -O1 -g -std=gnu89 -fsanitize=address,undefined \
    -o fuzzdemux tools/rmpeg1/fuzz_demux.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c -Ilibretro-common/include
```

Generate reference streams with ffmpeg:

```sh
ffmpeg -f lavfi -i "testsrc=size=352x240:rate=29.97:duration=3" \
       -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=3" \
       -target ntsc-vcd vcd_ntsc.mpg
ffmpeg -f lavfi -i "testsrc=size=352x288:rate=25:duration=4" \
       -f lavfi -i "sine=frequency=300:sample_rate=44100:duration=4" \
       -target pal-vcd  vcd_pal.mpg
```

`diff_demux` takes a chunk size as its second argument; run it at 1, 7, 64,
512, 2048, 2324 and something larger than the file to exercise the streaming
path at every boundary.

## Results at the time of writing

Differential vs pl_mpeg, comparing packet count, type, substream index,
payload length, payload FNV-1a hash and PTS to within one 90 kHz tick:

| stream | packets | resyncs | result |
|---|---|---|---|
| vcd_ntsc.mpg (352x240, MP2 stereo) | 221 | 0 | PASS |
| vcd_pal.mpg (352x288, MP2 stereo) | 296 | 0 | PASS |
| vcd_noaudio.mpg | 121 | 0 | PASS |
| generic.mpg (non-VCD mux) | 80 | 0 | PASS |

Identical output at chunk sizes 1, 7, 64, 512, 2048, 2324 and 1 MiB.

Robustness, ASan + UBSan, on vcd_ntsc.mpg: 64 truncations, 64 mid-stream
entry points, 3000 random byte-corruption runs (1..64 flips each) and 400
pure-random buffers. No leaks, no out-of-bounds access, no stalls, no
zero-length packets emitted.

## rmpeg1_video

`diff_video.c` cross-checks the bitstream layers against pl_mpeg;
`idct_accuracy.c` measures the IDCT against a double-precision reference in
the style of IEEE 1180-1990 and is the authoritative check on pixel values.

```sh
gcc -O2 -std=gnu99 -o diffvid tools/rmpeg1/diff_video.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c \
    libretro-common/formats/mpeg1/rmpeg1_video.c \
    -Ilibretro-common/include -Ilibretro-common/formats/mpeg1 \
    -Ideps/pl_mpeg -lm

gcc -O2 -std=gnu99 -o idctacc tools/rmpeg1/idct_accuracy.c \
    -Ilibretro-common/include -Ilibretro-common/formats/mpeg1 -lm
```

### IDCT accuracy at the time of writing

| coefficient range | peak | mse | me | worst pme |
|---|---|---|---|---|
| intra-like (DC ~1024) | 1 | 0.01058 | 0.000045 | 0.00180 |
| 1180 L=256 | 1 | 0.00935 | -0.001259 | 0.00485 |
| 1180 L=5 | 1 | 0.00516 | -0.000098 | 0.00160 |
| 1180 L=300 | 1 | 0.00988 | -0.001392 | 0.00505 |
| DC only | 0 | 0 | 0 | 0 |

All within the 1180 bounds (peak 1, mse 0.06, me 0.015, pme 0.015), and
all-zero input gives all-zero output.

### I-frame cross-check vs pl_mpeg

90 to 95% of luma samples identical, the rest almost all off by one, a
handful in 84,000 by up to 5. Since ours is within peak error 1 of the
double-precision reference, that tail is the other decoder's IDCT.
Identical results at chunk sizes 1, 7, 64, 2324 and 1 MiB; zero slice errors.

### P-picture support

`diff_video.c` compares every frame, not just the first: a P picture is built
on its predecessor, so a prediction error accumulates down the GOP and a
frame-0 check would pass a decoder whose motion compensation is subtly wrong.
Set `RMPEG1_PERFRAME=1` for a per-frame breakdown and `RMPEG1_LOCATE=1` to
dump the worst macroblock when a frame exceeds the threshold.

| stream | frames | worst Y maxdiff | mean |
|---|---|---|---|
| vcd_ntsc | 90/90 | 5 | 0.11 |
| vcd_pal | 100/100 | 5 | 0.10 |
| vcd_noaudio | 60/60 | 3 | 0.05 |
| generic (full-pel, f_code 7) | 60/60 | 6 | 0.12 |

Zero slice errors, zero skipped pictures, identical at chunk sizes 1 through
1 MiB, and clean under ASan and UBSan.

The residual deviation resets at every I-picture and stays bounded across a
GOP, which is the signature of two decoders' IDCTs drifting apart rather than
a prediction fault -- the thing MPEG's oddification mismatch control exists
to bound. A motion compensation bug does not reset cleanly, which is how the
skipped-macroblock DC predictor defect was found: it showed as a uniform
-46 across one 16x16 block that persisted, unchanged, for the rest of the GOP.

### B-picture support

Generate a stream with B pictures:

```sh
ffmpeg -f lavfi -i "testsrc=size=352x240:rate=29.97:duration=3" \
       -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=3" \
       -c:v mpeg1video -b:v 1150k -bf 2 -g 15 -c:a mp2 -b:a 224k \
       -f mpeg bframes.mpg
```

| stream | frames | worst Y maxdiff | mean |
|---|---|---|---|
| vcd_ntsc | 90/90 | 5 | 0.11 |
| vcd_pal | 100/100 | 5 | 0.10 |
| vcd_noaudio | 60/60 | 3 | 0.05 |
| generic (full-pel, f_code 7) | 60/60 | 6 | 0.12 |
| bframes (6 I, 24 P, 59 B) | 89 compared | 5 | 0.11 |

Coded order `IPBBPBBPBB...` comes out as `IBBPBBPBB...`, which is the
reordering the format requires.

On the B stream we emit 90 frames where pl_mpeg emits 89. That is not a
discrepancy in the pictures: pl_mpeg holds its final reference and has no
flush, so it drops the last one. rmpeg1_video_flush() releases ours.

## Performance

`bench.c` times decode only, against pl_mpeg on the same stream.

```sh
gcc -O3 -std=gnu99 -o bench tools/rmpeg1/bench.c \
    libretro-common/formats/mpeg1/rmpeg1_ps.c \
    libretro-common/formats/mpeg1/rmpeg1_video.c \
    -Ilibretro-common/include -Ilibretro-common/formats/mpeg1 \
    -Ideps/pl_mpeg -lm
```

| stream | rmpeg1 | pl_mpeg |
|---|---|---|
| vcd_ntsc (90 frames) | 14.6 ms | 15.8 ms |
| vcd_pal (100 frames) | 22.4 ms | 23.2 ms |
| bframes (90 frames) | 14.1 ms | 15.0 ms |

Demux alone is far below the noise floor for both (>10 GB/s).

### How it got there

The first working version was 60.8 ms against pl_mpeg's 15.0, a 4x deficit.
gprof put 36% in `vlc_decode` and 33% in `mc_predict`; three changes closed
it, each measured rather than assumed.

`peek_bits` walked the input one bit at a time. Gathering eight bytes into a
64-bit accumulator and shifting took it to 37.5 ms -- the single largest win,
and it also removed most of the cost attributed to `vlc_decode`.

The VLC decoder scanned up to 113 entries per symbol. A 9-bit lookup index,
built at init from the generated tables so they remain the only copy of the
data, made `vlc_decode` disappear from the profile entirely. On its own it
measured as noise, because the faster peek had already absorbed the cost --
worth keeping for targets where the branchy scan would not be free, but it is
not where the time was.

`mc_predict` was then 68%: it branched on the interpolation mode and clamped
to the frame bounds inside the innermost loop. Hoisting both out, with a
fast path for the (near-universal) case of a block fully inside the
reference, took the whole decoder to 13.1 ms.
