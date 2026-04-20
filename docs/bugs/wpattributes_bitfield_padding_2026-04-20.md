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

After the padding fix, Mario Fireball deals exactly **64% damage per hit** (one wpMake = one hit = Link's % goes up by 64). Expected is ~5-6%. Ruled out:

1. **Not multi-hit.** The wpMake log confirms one weapon spawn per B-press, and `ftMainSearchHitWeapon`'s hit loop has `goto next_gobj` after a hit registers — same weapon can't hit same victim twice in one frame even with `attack_count=2`.
2. **Not staling.** `wpMainGetStaledDamage` returns `damage * stale + 0.999`; `stale=1.0` for fresh moves, so the raw `damage=64` goes through unchanged.
3. **Not victim damage_mul.** `ftParamGetCapturedDamage` only scales when `fp->capture_gobj != NULL` or `fp->damage_mul != 1.0F`. Default path is pass-through.

The raw `damage=64` appears to be what the ROM stores in the decomp-declared bit position (bits 4-11 of Word 1). On N64 hardware this same value is read and produces ~5% in-game, so either:

(a) The decomp's Word 1 layout is wrong for `damage` — it's actually at a different bit position (e.g., bits 24-31 of Word 1 gives `0x06 = 6` for Mario, much closer to 5-6%), but Fox Blaster's 0x19018001 at those bits gives `25` which doesn't match Fox's 3% per shot — so no single alternate layout fits all weapons.
(b) There's a runtime damage scaling factor somewhere in the N64 binary that the decomp/port doesn't perform. Unidentified.

Also suspicious: `element=0` for Mario Fireball (should be 1 = `nGMHitElementFire`). At bits 16-19 the value is `1`, matching Fire — another hint the decomp bit positions may be off for Word 1 beyond just the packing issue.

**Recommendation for the next session:** extract 5-6 weapons' Word 1 bytes via `reloc_extract.py`, cross-reference each weapon's expected damage/element with every plausible bit arrangement, and find the layout that satisfies all of them simultaneously. My single-weapon guesses aren't enough data.

## Impact beyond Mario Fireball

Every weapon that reads Word 1 bitfields (damage, element, knockback_scale, angle) was returning garbage. Same-class bug in the Samus Charge Shot investigation (doc 2026-04-13) — that investigation proposed this exact fix but it was never committed. With this fix in place, revisit the charge-shot bypass path: its `attack_count=0` from ROM is now properly decoded, so the explicit `attack_count=1` in `wpSamusChargeShotLaunch` + `pos_curr`/`pos_prev` initialization should be the only runtime workaround needed.

## Files touched

- `src/wp/wptypes.h` — add `u16 _pad_before_combat_bits` under `#ifdef PORT`.
- `src/wp/wpmanager.c` — diagnostic `port_log` at weapon spawn (decoded values per spawn).

## Related

- `docs/bugs/itattributes_type_field_offset_2026-04-20.md` — sibling bug in ITAttributes. Parallel root cause (decomp/clang bitfield mismatch) but different symptom (type-based dispatch instead of damage/hit decode).
- `docs/bugs/samus_charge_shot_hit_detection_2026-04-13.md` — originated the fix proposal; should be retired after the runtime-init workaround is confirmed against the now-correct decode.
- `docs/mario_fireball_passthrough_2026-04-19.md` — partially closed by this fix; damage-too-high issue supersedes it.
