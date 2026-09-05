# Game database compatibility checks

This harness tests the shipping game database and controller-compatibility
code outside the emulator. It covers serial normalization, packed compatibility
settings, database lookup and titles, controller support masks and fallback
resolution, and runtime-table structure.

The database include supplies the structural test data directly. Every runtime
row must have a unique canonical serial, nonzero recognized settings, and the
same settings and title when retrieved through the shipping lookup. Handwritten
normalization assertions use synthetic identifiers, so changing a real
database serial does not require updating the harness.

Run it with:

```sh
make -C tools/game_database clean check
```

The harness links the real `libretro_game_database.c` and
`input_compatibility.c` implementations. It does not duplicate their runtime
logic and does not require PlayStation content or a frontend.
