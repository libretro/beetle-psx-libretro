# Packed RHI interpolation tests

Run `make check` with a C compiler and the usual POSIX shell/sed tools.
No GPU, core, BIOS or game is required.

The `check-shaders` step compares the line tie expression in the C test with
both shader sources after removing whitespace. A missing or changed expression
fails the check. This catches source drift; it does not execute either shader.

The test includes the shipping `rhi_interpolation.h`. It checks descending-X
endpoint packing, zero-length lines, anchor ties, and color planes sampled at
software-style fixed-point DDA pixels for every endpoint pair in [-4,4]^2
(6,561 lines). A C equivalent of the shipped shader tie expression is checked
against that independent walk at every step, including both coordinates. An
additional 4,096 deterministic legal-span inputs are checked in both endpoint
orders, plus maximum-span and half-step regressions. The software model includes
the initial -1024 coordinate bias; omitting it changes the expected tie rule.

It also compares 20,000 deterministic triangle inputs against
barycentric derivatives and an 8.12 accumulator, including constant channels,
negative coordinates, reversed winding and wrapping. Degenerate triangles are
skipped, matching their rejection before this helper is called.

The triangle derivative comparison shares the mathematical plane formulation;
it is not an independent software-rasterizer coverage oracle. Renderer fixtures
must compare actual shader results against software output separately.

These are CPU packing/interpolation tests. They do not execute either shader or
establish GPU coverage, blending, filtering, scanout or complete game accuracy.
