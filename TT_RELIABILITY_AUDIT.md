# rhi_tt HD replacement pipeline — reliability audit (2026-08-21)

*Trigger: with the fused-path Reduce fix deployed, text replacements still fail
intermittently — reload (') recovers a varying subset, Eager loads more than Lazy,
all modes miss some, and single-upload sprites occasionally slip one native frame
even in Lazy (synchronous). Audit of the request → IO → cache → attach → bind → fuse
lifecycle in `rhi/rhi_tt.c` (branch `reduce-fused-fix`). Every finding below marked
CONFIRMED was verified by direct code reading; line numbers refer to this branch.*

Symptoms: **S1** text intermittently never loads / reload shuffles the subset;
**S2** Eager > Lazy for text; **S3** even Eager misses some; **S4** 1-frame native
slips of ordinary sprites in Lazy (synchronous); (budgets are not a factor).

## Confirmed defects (ranked)

### A. Draw-loop early-return starves requests for fused draws — CONFIRMED (S1, S2 primary)
`texture_tracker_get_hd_texture_index`'s overlap loop is the ONLY producer of
upload-rect load requests in Lazy modes (`request_hd_texture`, rhi_tt.c:4331), but
it `return fused_pages_get_or_make(...)` MID-LOOP at the second image-bearing
upload (4340-4351). Uploads later in overlap order are never iterated — never
requested, this frame or any later frame (positions 0,1 stay bound, so the gate
re-forms identically). The fused machinery itself only looks up
(`hd_tex_map_find`, fusion_rects:5717), never requests. In Lazy (synchronous) it
is brutally deterministic: exactly TWO glyphs of a 20-glyph line ever load.
**Fix:** complete the overlap iteration (requesting every unbound upload) before
deciding single-vs-fused; return the fused handle after the loop.

### B. `want_combo` cache-hit early-out never attaches — CONFIRMED (S3, S1)
rhi_tt.c:3825-3826: combo already in GPU/CPU cache → plain `return` — no bind, no
`pending_attach`. Bindings live on `TextureUpload` objects, and text uploads churn
(rects overwritten → upload destroyed → re-created with an EMPTY textures map). So
Eager's upload-time prefetch is a NO-OP exactly for recreated uploads whose images
are already cached — those depend entirely on the starved draw loop (A).
**Fix:** on cache hit insert `pending_attach` (the attach pass already handles
both tiers) instead of returning silently.

### C. `pending_attach` is one-shot; unattached combos are dropped — CONFIRMED (S1, S3)
Attach pass: `find_upload(id.hash) == NULL → continue /* kept in cache */`
(4997-4999) — but `hd_key_set_clear(&self->pending_attach)` right after the pass
(5051) discards the marker for skipped combos too. A response draining while the
glyph's upload is momentarily dead (text rows recycle constantly) is consumed with
no attach and no re-arm; with B, nothing ever re-binds it. The IO-vs-recycle race
decides which glyphs survive → the reload-to-reload variance.
**Fix:** retain unattached keys (clear only attached / dimension-rejected /
cache-evicted entries).

### D. In-frame GPU-cache bind never dirties fused pages — CONFIRMED (S1, S2)
`request_hd_texture`'s GPU-hit branch binds and returns (3877-3882) WITHOUT
`fused_pages_mark_dirty` — unlike sync_load_combo (4802-4809) and the attach pass
(5008-5013). A glyph that does get bound via the draw loop stays invisible to an
existing fused page until something else dirties it. Lazy binds mostly through
this silent path; Eager mostly through the marking attach pass — another Eager/Lazy
asymmetry.
**Fix:** mark this upload's live rects dirty after the bind (same loop as
sync_load_combo).

### E. `fused_pages_get_or_make` serves an existing DIRTY page stale — CONFIRMED (S4, S1)
The found-branch (5900-5915) never checks `p->dirty` and never rebuilds; dirty
pages rebuild only at the next upload/blit or safe point. Binds made THIS frame
(sync loads included) render native for ≥1 frame on any fused draw — the likely
mechanism of the 1-frame sprite slips when a draw routes through fusion, and a
+1-frame lag on all text convergence.
**Fix:** `if (p->dirty) rebuild_page(p, tracker, tt);` before returning (the
new-page path already calls rebuild_page from this context; `fusionrects_eq`
keeps the no-change cost trivial).

### F. IO worker silent on failure → permanent `requested` poison — CONFIRMED (S1, S3 residue)
The worker pushes a response only on success (3106-3125); failure just logs. The
combo stays in `requested` forever (the design comment at 3818-3820 states it:
"never retried (until a reload clears it)"). Intended for missing files, but it
fires identically for TRANSIENT failures — AV/indexer sharing violations on a
25k-file folder, handle pressure during 4-worker bursts. `sync_load_combo` inserts
the same permanent negative on failure (4774-4777).
**Fix:** always push a response with a success flag; on failure erase `requested`
(bounded retries), keep the permanent negative only for genuine not-found.
Grep the RetroArch log for "failed to load:" to gauge how often this fires.

### G. `rect_tracker_place` doesn't dirty the lookup grid — CONFIRMED (S4)
`rect_tracker_clear`/`upload`/`blit` set `lookup_grid_dirty`; `rect_tracker_place`
(5481-5485, used by the readback-restore path at 3756-3761) does not — restored
rects are invisible to `rect_tracker_overlapping` (4188) until something else
dirties the grid, so a fully-cached sprite can miss for the rest of the frame.
One-line fix: set the flag in `rect_tracker_place`.

## Plausible, not yet verified in depth

- **Eager prefetch fires only on first-ever upload of a hash** (`!preexisting`,
  3776) and is never re-run by reload → post-reload Eager behaves like Lazy.
- **Mode switch doesn't clear `requested`** (set_config): switching into
  Lazy (synchronous) with async requests in flight blocks the sync loader (4765).
- **Reload doesn't quiesce in-flight IO** (no generation stamp) and doesn't clear
  `handle_cache`.
- **GL renderer never calls `texture_tracker_on_queues_reset`** → async attach
  never runs on GL at all (Vulkan unaffected).
- **`replace_textures_applied` is a process-lifetime function-static** (6250):
  after a tracker recreation (context rebuild) replacement stays silently disabled
  until the option is re-toggled.
- **Non-canonical pack filenames** (zero-padded hex) enumerate into known_files
  via `sscanf %x-%x` but the loader probes only the canonical `%x` spelling.

## Efficiency notes (secondary)

- `known_files` is built by per-insert memmove into a sorted array → O(n²): with
  25k files that is ~2.5 GB of moves on init AND on every reload keypress (the
  reload stall). Collect unsorted + one qsort.
- `texture_tracker_find_upload` is a linear scan over all live rects, run per
  upload and per attach; the attach pass also scans ALL rects per attached combo
  to mark fused pages dirty. O(uploads × rects) per frame in text-heavy scenes.
- `hd_tex_map_find` computed twice in one expression on the handle-cache hit path
  (4177).
- The shader fastpath is force-disabled in rhi_lib_vulkan.c:8780
  (`fastpath_capable_out = false;` immediately after the call — debug leftover),
  yet the tracker still computes fastpath capability per draw.
- Savestate re-warm (`load_hd_texture`, 3792) bypasses `want_combo`'s cache/
  in-flight dedup → up to triple decodes per combo after a savestate load.

## How the symptoms decompose

- **S1** = A (starvation) + C (dropped attaches) + D (invisible binds) + F (poison),
  all reshuffled by reload because reload clears bindings/caches/`requested` and
  re-runs every race with fresh timing.
- **S2** = Eager's upload-time prefetch + marking attach pass bypass A and D;
  Lazy depends wholly on the starved, non-marking draw loop.
- **S3** = B (prefetch no-op for cached combos on recreated uploads) + C + F.
- **S4** = E (stale fused page served) + G (restored rect invisible).

## Patch set — IMPLEMENTED (branch `reduce-fused-fix`, 2026-08-21)

P1 (A) full overlap iteration before the fuse decision; P2 (B) want_combo
cache-hit schedules pending_attach; P3 (C) attach pass retains unresident keys
(dropped only when evicted from both caches); P4 (D) GPU-hit bind marks fused
pages dirty; P5 (E) fused_pages_get_or_make rebuilds a dirty page before serving
it; P6 (F) IO worker pushes empty-levels failure responses, drain erases
`requested` so the next draw retries; P7 (G) rect_tracker_place sets
lookup_grid_dirty. Efficiency: known_files bulk build (append + one qsort +
dedup — kills the O(n^2) reload stall), handle-cache doubled hd_tex_map_find
fixed, savestate re-warm routed through want_combo (dedup + cache-skip).
All in rhi/rhi_tt.c (154 insertions / 44 deletions).

### Finding H (2026-08-21b, from Jed's flicker localization) — per-mutation inline
fused rebuilds. Flicker appeared ONLY in streaming-text scenes (typewriter
dialogue at boss intros, end credits) and never in upload-once static text, and
predated every loader patch. Cause: the upload/blit hooks called
`fused_pages_rebuild_dirty` INLINE on every VRAM mutation — once per typed
character — clearing + re-blitting an in-place-reused composite image mid-frame
while earlier draws of the frame still referenced it (vk_tt_page_begin reuses
the same handle and records into the current command buffer). FIXED (commit
7eb7ddea): hooks only mark dirty; rebuilds coalesced to the on_queues_reset safe
point + a page's FIRST serve of each frame (`rebuilt_frame` stamp on FusedPage).
Trade-off: mid-frame mutations reach the composite one serve later.

Not yet done (candidates for a later pass): Eager re-prefetch after reload,
`requested`/pending clear on caching-mode switch, reload IO generation stamp,
the GL on_queues_reset gap, the `replace_textures_applied` static latch, the
fastpath force-disable in rhi_lib_vulkan.c:8780, find_upload hash map.
