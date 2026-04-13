# Fighter Black Squares: Missing SetTile masks/maskt Wrap (2026-04-11) — FIXED

**Symptoms:** During the intro cinematic (scene 45, DK-vs-Samus jungle), Donkey Kong rendered with opaque black squares on his hands and feet, and Samus had a black square on the left side of her helmet.  Every other fighter area rendered correctly.  The prior commit 857cacc had already fixed costume colours by walking `portFixupMObjSub` through all fighter material paths; this class of artefact was distinct.  Instrumenting Fast3D's CI4 decoder to flag any texture decode producing ≥50% `(R,G,B)==(0,0,0)` pixels surfaced exactly three offenders: `file=317 off=0x00CCD0`, `file=317 off=0x00CD18` (DK palm / foot materials) and `file=320 off=0x00C968` (Samus helmet-left).  All three are 64-byte CI4 textures (tile `line*8 = 8` bytes per row → `width = 16 × height = 8` in Fast3D's computation), and their DRAM bytes have a uniform `[4 bytes of valid indices][4 zero bytes]` layout in every row.

**Root cause:** The real textures are 8-wide, not 16-wide — the trailing 4 bytes per row are unused TMEM-alignment padding.  N64 tiles express "this texture is smaller than one TMEM line" via the `masks`/`maskt` fields in `gDPSetTile` (e.g. `masks=3` → `1<<3 = 8` pixels), which the RDP sampler uses to wrap/clamp the texture to its logical size.  Sampling with `wrap(s, 8)` never reads columns 8-15, so the zero padding is invisible on real hardware and palette entry 0 (opaque black on CI4 fighter palettes) never appears in the rendered image.

Fast3D's `Interpreter::GfxDpSetTile` accepts `masks`/`maskt` but **only uses them to promote `G_TX_WRAP → G_TX_CLAMP` when `masks == NOMASK`**; the actual values are then discarded.  The `texture_tile` struct has no field to store them, so downstream `ImportTexture*` decoders only clamp to the line-derived width/height and the `lrs/uls`-derived `tile_w/tile_h` (which for these materials was *larger* than the line width → no clamp fired).  The result is that Fast3D uploaded a 16x8 CI4 texture for a logically-8x8 material, half of the uploaded texels were padding → palette index 0 → opaque black, and the shader rendered them as-is.  Every other fighter material that happened to have `mask_w >= line_w` (i.e. densely packed data across the whole TMEM line) was unaffected, which is why only hands/feet/helmet-left showed the artefact.

**Fix:**
1. Add `uint8_t masks, maskt` fields to `texture_tile[8]` in `libultraship/include/fast/interpreter.h`.
2. Store the parameters in `GfxDpSetTile` alongside the existing shifts/line fields.
3. In `ImportTextureRgba16`, `ImportTextureCi4`, and `ImportTextureCi8`, clamp the decoded `width`/`height` to `1<<masks` / `1<<maskt` when those values are smaller than the line-derived dimensions (after the existing `SetTileSize lrs/uls` clamp).  This matches the N64 sampler's wrap behaviour for the common case and eliminates the black padding.

The IA and I decoders weren't touched because no fighter/stage BLACK hits were observed in those paths during testing, but the same clamp applies in principle — if any IA/I texture surfaces with similar symptoms, extend the fix.

**Files:**
- `libultraship/include/fast/interpreter.h` — `texture_tile` struct grew `masks`/`maskt`.
- `libultraship/src/fast/interpreter.cpp` — `GfxDpSetTile` stores `masks`/`maskt`; `ImportTextureRgba16`/`Ci4`/`Ci8` honour them as a width/height clamp after the existing `lrs/uls` clamp.

**Class-of-bug lesson:** Fast3D implements only the RDP features needed for most games.  Whenever an "unused" parameter of a GBI command *is* used by SSB64 for correctness (here: `masks`/`maskt` for 8-wide-in-16-wide-TMEM-line textures), the fix is always on the libultraship side.  Before grinding on byte-level data interpretations, grep the Fast3D tile struct for the field name and confirm it's actually stored — if it isn't, that's the bug.

**Diagnostic that cracked it:** An `SSB64_TEX_RENDER_LOG` scan of every `ImportTexture*` call with a per-texture "how many decoded RGBA pixels are `(0,0,0)` ≥50%" threshold.  The scan surfaced fighter file+offset pairs in one pass.  After identifying the offenders, dumping their raw 64 bytes from the .o2r and decoding as CI4 made the `[data 0 0 0 0]` padding pattern obvious, which pointed straight at a sub-tile-line-width texture and from there to the missing masks clamp.
