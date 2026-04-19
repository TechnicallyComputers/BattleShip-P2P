# Item / powerup collision clips slightly through platforms — Handoff (2026-04-19) — OPEN

## Symptom

When items (powerups, capsules, food, etc.) land on a stage platform during a battle, they appear to sink **slightly** into the platform surface instead of resting flat on top. Fighter collision against the same platforms looks correct — only item/powerup collision is off. First noticed after the battle-start hang was fixed in c2a536c, so this is the first time any match has run long enough to observe item physics.

## Likely area

- Item/weapon floor collision lives in `src/gm/gmground.c` / `src/gm/gmcollision*.c` (the `gm` namespace shares floor-probe code between fighters and items, but items use a different probe entry point and/or offset).
- Items/weapons use `wpProcess*` / `itemMainPhysics` update paths (see `src/wp/` and `src/it/`); those call into the collision subsystem with their own hit-box/ground-probe radius.
- Fighters use `ftPhysicsGroundLanding` and friends, which work — so the shared floor-map data is almost certainly fine. Suspect a per-entity Y-offset applied by the item proc-update, or a probe that's slightly too short.

## Ways the LE port could have regressed this

- `GroundLink` / `GroundInfo` / `MPVertex*` halfswap — see `docs/bugs/mpvertex_byte_swap_2026-04-11.md` for the fighter-side of this. Items may read a different collision field (e.g. `y_offset`, `floor_friction`) that wasn't covered by the previous fixup.
- Float endian / field-order mismatch on the item's bounding sphere.
- `PORT_RESOLVE` applied to the wrong pointer in an item struct (similar to `docs/bugs/ground_geometry_collision_2026-04-08.md`).

## Reproduce

Start any VS or 1P battle, let the CPU or item spawner drop a capsule/food on a platform, observe it from a side angle — the bottom of the item sprite sinks below the platform edge.

## Next steps

1. Confirm whether the clip amount is constant (suggests fixed Y offset bug) or proportional to item size (suggests collision-radius / scale mismatch).
2. Add `port_log` at the point where an item's ground probe commits Y position; compare against the fighter path for the same platform.
3. Diff the item vs fighter collision struct reads to find any halfswapped or endian-swapped field that feeds the probe.
