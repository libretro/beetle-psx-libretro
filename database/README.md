# PlayStation game database

`psx_games.inc` contains the PlayStation game database. Its initial title,
category, version, and serial metadata came from Redump's Sony PlayStation
serial/version DAT 10974, dated 2026-08-28.

Redump metadata information:
https://wiki.redump.info/Redump#Does_Redump_.27own.27_the_content_we_contribute_to_the_database.3F

Source DAT:
https://redump.info/datfile/psx/serial,version

Use `PSX_GAME` for metadata-only entries and `PSX_GAME_COMPAT` for entries with
compatibility settings. Metadata-only rows are excluded from the compiled
runtime table.

Compatibility settings share one settings word and may be combined with `|`.
PGXP settings modify an active PGXP configuration but do not enable PGXP.
Universal relationships between settings remain in `libretro.c`.

## Supported settings

| Setting | Expression |
| --- | --- |
| PGXP Memory + CPU mode | `PSX_COMPAT_PGXP_MEM_CPU` |
| Enable perspective-correct texturing | `PSX_COMPAT_PGXP_PCT_ON` |
| Disable perspective-correct texturing | `PSX_COMPAT_PGXP_PCT_OFF` |
| Enable primitive culling | `PSX_COMPAT_PGXP_CULLING_ON` |
| Enable vertex caching | `PSX_COMPAT_PGXP_CACHE_ON` |
| Disable vertex caching | `PSX_COMPAT_PGXP_CACHE_OFF` |
| Enable framebuffer-write FIFO delay | `PSX_COMPAT_FBWRITE_FIFO_DELAY` |
| Limit maximum CD-ROM speed | `PSX_COMPAT_MAX_CD_SPEED(value)` |

Example:

```c
PSX_COMPAT_PGXP_PCT_ON | PSX_COMPAT_PGXP_CULLING_ON
```
