# AGENTS.md

Instructions for anyone, human or agent, changing beetle-psx-libretro.
Read the section that covers the code you touch before touching it.

## Ground rules

- **C89 for new code.** A new source file is C89 that MSVC compiles:
  `/* */` comments, declarations at the top of a block.
- **No locks, no sleeps.** Threads hand work over through retro_atomic,
  SPSC rings and eventcount park/wake. No mutex, no spinning, no sleep
  or wall-clock poll: a thread waits on a real event.
- **Zero-copy.** Content is read in place where it already is - a
  mapping, a decoder's buffer - and lent rather than copied. Every copy
  on a data path is one to remove.
- **Nothing allocated during play.** Buffers are sized and allocated at
  load or on a resolution change, never per frame.
- **Self-contained build.** The core builds from this tree with no
  system development packages; what it needs is in the tree.
- **Every fix ships with its test**, which fails on the broken code and
  passes on the fix.
- **Both toolchains.** A change is built on Linux and cross-compiled for
  Windows with mingw-w64 before it goes in.

## libretro-common: RetroArch first

`libretro-common/` is a copy of RetroArch's `libretro-common/`
(https://github.com/libretro/RetroArch), file for file. No file there is
ever edited in this core.

- **Upstream first.** Anything the core needs changed in libretro-common
  (a fix, a new function, a new mode of an existing one) is made in
  RetroArch's `libretro-common/` first, as a patch against RetroArch,
  with its test under `libretro-common/samples/`. Once RetroArch has it,
  the core takes it by a resync. A core adaptation that is not a
  libretro-common change goes in the core's own files.
- **A resync is a copy, never a merge.** Every file here that RetroArch
  has is replaced by RetroArch's, so a change made only here is deleted
  by the next resync. Before one, compare the two: anything this copy has
  that RetroArch's lacks goes upstream first.
- **Bring in what the new files need.** A synced file that now needs
  another (a new source, a new header) brings it along, and the new
  source is added to every build that lists its neighbour: the
  Makefiles, the Android and CMake builds, the test scripts.
- **Follow moved declarations.** Where a function moved to another
  header, every call site includes the new one. A clean build of the
  core has no implicit-declaration warning: GCC 14 makes it an error, and
  older compilers cut the returned pointer to an int.

## Tests

- `sh tools/chd/run.sh` - the CHD reader against every codec. Needs
  `chdman` and `python3`.
- `sh tools/readahead/run.sh` - the mapped read-ahead.
- `sh tools/vcd/build.sh cdstream_map_test -fsanitize=address,undefined`,
  then `/tmp/cdstream_map_test <file>` - a mapped cdstream's borrowed
  buffer, built with the mapping on.
- `sh tools/ttio/run.sh` and `sh tools/tt_replace/run.sh` - texture
  replacement I/O.
- `sh tools/gte_state/run.sh` - the GTE data registers across state loads,
  resets, CPU mode switches and reloads. Needs a built core.
- `sh tools/gpu_span/run.sh` - the software rasterizer's textured spans
  against VRAM and frame hashes pinned from the scalar renderer, at 1x, 2x
  and 4x. Needs a built core; runs an ARM build under qemu too.
- `sh tools/gpu_thread/run.sh` - the GPU thread against the inline
  renderer: VRAM, pictures, GPUREAD and emulated GPU timing with the thread
  off, on and switched mid-run, at 1x, 2x and 4x, with PGXP and the "Off"
  deinterlacer. Needs a built core; build the core and the host with
  `-fsanitize=thread` (`CC="cc -fsanitize=thread"`) to check the drains.
- `sh tools/vkhost/lifetime_lanes.sh` - managed savestates on the Vulkan
  renderer: a rollback every frame with the frontend's lifetime callbacks
  gives the frames of a run without one, at 1x to 4x, with the Software
  Framebuffer, adaptive smoothing and HDR. Needs a HAVE_HW=1 build and a
  Vulkan device (lavapipe works).
