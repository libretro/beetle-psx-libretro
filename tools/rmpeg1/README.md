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
