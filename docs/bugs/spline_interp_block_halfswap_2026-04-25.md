# Spline Interp Data Halfswap — 2026-04-25

## Symptom

Samus's roll/dodge (`nFTCommonStatusEscapeF` / `nFTCommonStatusEscapeB`) plays the morph-ball animation but doesn't translate her position. Same for any other animation that drives `TransN` translation through a figatree `SetTranslateInterp` event (cubic/linear path interpolation across multiple control points). Visible as: the character enters the dodge state, the animation runs frame-by-frame, ground velocity stays at zero, character slides in place.

Reproduces in single-player gameplay and in the intro fight scene (`SSB64_START_SCENE=45`, OpeningJungle DK vs Samus).

## Root cause

Fighter figatree files go through two load-time passes:

1. `pass1_swap_u32` — blanket BSWAP32 of every u32 (the standard ROM-BE → host-LE conversion).
2. `portRelocFixupFighterFigatree` — for figatree files only, halfswaps every non-token u32 slot. Halfswap = swap the two u16 halves of each u32. This is required for AObjEvent16 streams whose 16-bit events were packed two-per-u32 in the ROM.

The figatree's `SetTranslateInterp` opcode points to a `SYInterpDesc` block plus three data tables (`Vec3f points[N]`, `f32 keyframes[N]`, `f32 quartics[5*(N-1)]`). The desc and data blocks all live in the figatree file, so the load-time halfswap pass touches them too. For full-width float data (Vec3f and f32 arrays) the halfswap is wrong — each float gets its u16 halves swapped, producing values like `9.6e9`, `1.27e17`, `-4.3e36` instead of the actual control-point coordinates and keyframe times.

The visible effect: `syInterpCubic(out, desc, t)` walks `t` from 0 to 1 across the dodge frames, but every interpolation result lands on garbage point data (typically the last point's bytes), so `out` (= `dobj->translate.vec.f`) stays constant at one value. `ftPhysicsApplyGroundVelTransN` then computes `vel_ground = (translate.now - translate.prev) * scale = 0` and the character doesn't move.

## Fix

`src/sys/interp.c` — un-halfswap the spline data blocks at first access in `syInterpGetPoints` / `syInterpGetKeyframes` / `syInterpGetQuartics`, gated on `port_aobj_is_in_halfswapped_range(p)` so non-figatree-derived interp blocks (CObj camera anims from non-fighter files, etc.) pass through untouched. Idempotent via a visited-pointer set.

Block sizes derived from `desc->points_num`:
- `points`: `points_num * 3` u32s (Vec3f).
- `keyframes`: `points_num` u32s.
- `quartics`: `(points_num - 1) * 5` u32s for cubic kinds, 0 for linear.

Verification (intro scene, scene 45, 5000 frames):
- Before fix: `translate=(9.6e9, 0, 383.5)` constant for all 30 frames of Samus's dodge. `vel_ground=(0, _, 0)`.
- After fix: `translate.x` walks from `+47` → `-133`; `translate.z` walks from `-22` → `+446`; `vel_ground` peaks at `(22, _, -47)` early then settles around `(18, _, 2)` for the steady forward roll, decaying toward zero as the animation completes.
- Probe confirms `was_unswapped=1` on first call (block in halfswapped range, key not yet visited), `was_unswapped=0` on subsequent calls (idempotency).

## Why the SYInterpDesc header is left alone

The header is *also* halfswapped, but the LE struct layout happens to land `s16 points_num` on bytes that decode to the correct value (5 in Samus's case) anyway. The `kind` byte reads as 0 (Linear) instead of the original ROM kind (probably 2/Bezier), and `length` reads as 0.846F instead of ~38.5F — but downstream spline code consumes those derived values coherently for Linear interpolation across the (now-correct) point/keyframe arrays. Un-halfswapping the header was attempted and crashed the spline math (`points_num` jumped to 512, target_frame indexed out of bounds). The data-block-only fix is enough to make the visible movement correct. If a future bug surfaces where the header's `kind` actually matters (e.g., an animation that *needs* Bezier interpolation), revisit this — likely with a per-field byteswap rather than blanket halfswap.

## Class

Same family as `aobjevent32_halfswap_2026-04-18` — figatree halfswap is correct for u16-packed events, wrong for full-width data living in the same file. Look for this pattern any time figatree-loaded data is read as float/Vec3f/u32 array and produces values with implausible exponents or constant-across-frames behaviour.

## Files touched

- `src/sys/interp.c` — un-halfswap helpers + accessor wrappers.
