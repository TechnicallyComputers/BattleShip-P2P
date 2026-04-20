# WPAttributes bitfield storage-unit pun with implicit padding — PARTIAL (2026-04-20)

## Symptom

All fighter-weapon projectiles (Mario Fireball, Samus Charge Shot, Fox Blaster, Pikachu Thunder Jolt, Link Arrow, etc.) **passed through opponents without registering hits**. Fighter melee attacks hit normally. The bug was isolated to projectile-vs-fighter hit detection. Tracked as OPEN in `docs/bugs/samus_charge_shot_hit_detection_2026-04-13.md` and `docs/mario_fireball_passthrough_2026-04-19.md` for weeks; the current fix resolves the primary decode issue but introduces a separate "damage too high" problem that's still under investigation.

## Root Cause

Clang on LP64/LE was packing `WPAttributes.element:4 + damage:8` (12 bits total) into the **implicit 2 bytes of padding** between `u16 size` at struct offset `0x24` and the natural u32 alignment at `0x28`. Verified empirically:

```c
// Minimal reproduction (no _pad_before_combat_bits):
struct WPAttributes {
    ...
    u16 size;                 // 0x24
    // <-- clang treats these 2 pad bytes as an available u16 storage unit
    u32 element : 4;
    u32 damage : 8;
    u32 knockback_scale : 10;
    u32 angle : 10;
    ...
};

struct WPAttributes a = {0};
a.damage = 0xFF;
// bytes 0x24-0x2F: 00 00 00 00  f0 0f 00 00  00 00 00 00
//                  ^^^^^ size   ^^^^^ damage!    ^^^^^ empty Word 1
```

The bytes at `0x26-0x27` aren't actually padding in the loaded ROM — they're the high half of the u16 pair containing `size`, rotated in by `portFixupStructU16(attr, 0x10, 6)`. For Mario Fireball's attribute file that region decodes to `0x5A40`, so `damage` read `(0x5A40 >> 4) & 0xFF = 0xA4 = 164`. Meanwhile `knockback_scale` and `angle` (which do need a full u32 of storage) got placed at `0x28` and read from what the decomp *thought* was Word 1 — but only the last 20 bits of their intended 32-bit word, with the first 12 bits (element+damage) entirely missing from that storage unit. The result: every `attr->*` bitfield read from Word 1 returned garbage.

Most conspicuously, `attr->damage` read 164 instead of the ROM's intended 64 (bits 11-4 of the real Word 1 = `0x0641c400`), and `attr->knockback_scale` read 0 (because the ROM has 0 in what clang now treated as kb_scale's slot at bits 12-21 of Word 1 at 0x28).

## Fix

Mirror the ITAttributes workaround at `src/it/ittypes.h:243`. Add an explicit `u16 _pad_before_combat_bits` field at offset `0x26`, which consumes the implicit-pad space and forces clang to start Word 1's u32 storage unit at the natural-aligned `0x28` as the SGI IDO BE compile did:

```c
u16 size;
#ifdef PORT
    u16 _pad_before_combat_bits;
#endif

// Bitfield Word 1
u32 element : 4;
u32 damage : 8;
u32 knockback_scale : 10;
u32 angle : 10;
```

`sizeof(WPAttributes)` remains `0x34` (the implicit pad already contributed 2 bytes; replacing it with an explicit field is size-neutral). The `_Static_assert` still passes.

### Why BE worked

On SGI IDO BE, bitfields are packed MSB-first and the compiler's storage-unit selection differs. The original N64 binary put Word 1 at offset `0x28` correctly. The decomp imported this struct intending the same layout; the LE breakage came from clang's decision to reuse the preceding u16 pad as a bitfield storage unit.

## Still Open: damage value too high

After the padding fix, Mario Fireball's decoded fields are:
- `damage=64, knockback_scale=28, angle=25, attack_count=2, can_rehit_fighter=0`

On the test build, fireballs now **hit** opponents — but Link went from 5% to 249% after a sequence of fireballs (approx +244% over a visually single-ish exchange). Expected behavior is ~5-6% per hit.

Hypotheses to investigate:
1. `attack_count=2` causes the fighter hit-search loop to register two hits per frame of contact (`ftMainSearchHitWeapon` iterates `i < attack_count` and calls `gmCollisionTestRectangle` for each). `can_rehit_fighter=0` protects against *re*-hit across frames via `attack_records`, but not against same-frame double-hits through two separate hitboxes at the same world position.
2. The raw `damage=64` may be scaled somewhere we haven't found. `wpMainGetStaledDamage` multiplies by `attack_coll.stale` (1.0 for fresh moves) + 0.999, and `ftParamUpdateDamage` applies the result directly — no scaling. The ROM value `64` is suspicious; maybe a separate scaling factor (e.g., damage-in-quarters, ×0.25) exists that the port is missing.
3. Fireball lingers in the contact volume for multiple frames. If each frame re-registers (because `attack_records` isn't being populated correctly), 4 frames × 64 = 256 ≈ 249. Need to verify `wpProcessUpdateHitPositions` walks `attack_state: New → Transfer → Interpolate` correctly so the per-victim record is set on first hit.

## Impact beyond Mario Fireball

Every weapon that reads Word 1 bitfields (damage, element, knockback_scale, angle) was returning garbage. Same-class bug in the Samus Charge Shot investigation (doc 2026-04-13) — that investigation proposed this exact fix but it was never committed. With this fix in place, revisit the charge-shot bypass path: its `attack_count=0` from ROM is now properly decoded, so the explicit `attack_count=1` in `wpSamusChargeShotLaunch` + `pos_curr`/`pos_prev` initialization should be the only runtime workaround needed.

## Files touched

- `src/wp/wptypes.h` — add `u16 _pad_before_combat_bits` under `#ifdef PORT`.
- `src/wp/wpmanager.c` — diagnostic `port_log` at weapon spawn (decoded values per spawn).

## Related

- `docs/bugs/itattributes_type_field_offset_2026-04-20.md` — sibling bug in ITAttributes. Parallel root cause (decomp/clang bitfield mismatch) but different symptom (type-based dispatch instead of damage/hit decode).
- `docs/bugs/samus_charge_shot_hit_detection_2026-04-13.md` — originated the fix proposal; should be retired after the runtime-init workaround is confirmed against the now-correct decode.
- `docs/mario_fireball_passthrough_2026-04-19.md` — partially closed by this fix; damage-too-high issue supersedes it.
