# SMASH Title Screen 32bpp Sprite — Rendering Artifact

## Status: OPEN — root cause identified, fix needed in Fast3D

## Symptom

The "SMASH" text on the title screen (scene 1, `nSCKindTitle`) renders with fine horizontal combing/interlacing lines through the letters. The gradient colors (red top to yellow bottom) and letter shapes are correct — only the scanline-level structure is wrong. All other sprites on the title screen (SUPER, BROS, TM, copyright, background wallpaper) render correctly.

## Sprite Details

- Source: `llMNTitleSmashSprite` in `sMNTitleFiles[0]`, created via `lbCommonMakeSObjForGObj` in `mnTitleMakeLabels`
- Format: **RGBA32 (bmsiz=3, bpp=32)**
- Layout: 21 bitmap strips, each `w=172 h=4`, total sprite ~172x84
- The sprite scales during the title animation (gets larger, shrinks back)

## What Was Ruled Out

### Not a TMEM line deswizzle issue

Byte-level analysis of the texture data in `portFixupSpriteBitmapData` confirmed the 32bpp pixel data is stored **linearly** (not pre-swizzled). Adjacent pixel pairs on odd rows form a smooth gradient:

```
PRE  bm=9 @320 row0=FF0011FF FF0011FF  row1=FFB424FF FFD529FF
POST bm=9 @320 row0=FF0011FF FF0011FF  row1=FFD529FF FFB424FF
```

Applying XOR-4 deswizzle **reverses** the gradient on odd rows, corrupting the data. XOR-2 was also tested and made things worse (corrupted the background wallpaper too). The 32bpp sprite data must be left as-is — `bpp < 32` exclusion in the deswizzle condition is correct.

Why 32bpp is different from 4b/8b/16b: on the N64, 32bpp textures loaded via `LOAD_BLOCK` with `G_IM_SIZ_32b` are handled differently by the hardware than the 16bpp-format loads that 4b/8b/16b sprites use. The 4b/8b/16b sprites all use `G_IM_SIZ_*_LOAD_BLOCK = G_IM_SIZ_16b`, which requires DRAM pre-swizzle. SSB64's `gbi.h` defines `G_IM_SIZ_32b_LOAD_BLOCK = G_IM_SIZ_32b` (not 16b), and the data is stored linearly.

### Not a byte-swap issue

The BSWAP32 restore in `portFixupSpriteBitmapData` correctly restores N64 big-endian RGBA order. Fast3D's `ImportTextureRgba32` reads `addr[0]=R, addr[1]=G, addr[2]=B, addr[3]=A` sequentially, which matches. Pixel values like `FFCA28FF` decode to R=255,G=202,B=40,A=255 (opaque orange) — consistent with the visible red-to-yellow gradient.

### Not an idempotency collision

The 32bpp textures showed `bswap_skipped=0` in the diagnostic log — they were NOT previously processed by pass2 or the chain-walk. The BSWAP32 and deswizzle both run fresh (no stale tracking set entries blocking them).

## Probable Root Cause: Fast3D tile stride for 32bpp

The N64 GBI defines `G_IM_SIZ_32b_LINE_BYTES = 2` (not 4). This is because RGBA32 textures on the N64 are split across two TMEM banks (high 16 bits in one bank, low 16 bits in the other), and the `line` parameter in `gDPSetTile` represents qwords per row in a single bank.

The sprite rendering code computes the render tile's `line` as:
```c
((tex_width * G_IM_SIZ_32b_LINE_BYTES) + 7) >> 3
= ((172 * 2) + 7) >> 3
= 43 qwords
```

Fast3D stores this as `line_size_bytes = 43 * 8 = 344 bytes` (`GfxDpSetTile`, line 2688 of interpreter.cpp).

The actual row stride for 172-pixel RGBA32 is `172 * 4 = 688 bytes`. Fast3D's `ImportTextureRgba32` compensates by passing `line_size_bytes * 2` to `GetEffectiveLineSize`, which produces the correct `widthBytes = 688`. So the texture **import** computes the right dimensions (172x4).

However, the `line_size_bytes = 344` stored in the tile descriptor may be used elsewhere in the rendering pipeline (texture rectangle stride, cache keying, or shader parameters) without the `*2` correction, causing the renderer to interpret the texture with half the correct row stride — interleaving the left and right halves of each row as separate rows, producing the horizontal combing artifact.

## Files Involved

- `src/mn/mncommon/mntitle.c` — title screen sprite creation
- `src/lb/lbcommon.c:2629-2675` — `lbCommonDrawSObjBitmap` 32bpp LOAD_BLOCK + SetTile
- `include/PR/gbi.h:455,460` — `G_IM_SIZ_32b_LINE_BYTES=2`, `G_IM_SIZ_32b_LOAD_BLOCK=G_IM_SIZ_32b`
- `libultraship/src/fast/interpreter.cpp:2688` — `GfxDpSetTile` stores `line * 8` without 32bpp correction
- `libultraship/src/fast/interpreter.cpp:831-880` — `ImportTextureRgba32` (import is correct, uses `*2`)

## Suggested Fix Direction

The fix should be in Fast3D (`libultraship/src/fast/interpreter.cpp`). When `GfxDpSetTile` receives a tile with `siz = G_IM_SIZ_32b`, the stored `line_size_bytes` should be doubled to reflect the full RGBA32 row width (since the N64's `line` parameter represents only one TMEM bank's worth of data for 32bpp). This would make the tile descriptor consistent with what `ImportTextureRgba32` already computes internally.

Alternatively, audit every consumer of `texture_tile[tile].line_size_bytes` and ensure they account for the 32bpp half-width convention.
