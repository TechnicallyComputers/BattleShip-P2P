# Mario side/right B-special → segfault — Handoff (2026-04-19) — OPEN

## Symptom

In a battle, triggering Mario's **right-B special** (user's description; most likely Side-B or Up-B invoked while holding right on the stick) crashes the game with a segmentation fault. Hard crash, process exits — not a watchdog hang, so this is a memory access fault rather than an infinite loop.

Note: SSB64 Mario officially has three specials — B (Fireball), Up-B (Super Jump Punch), Down-B (Tornado). There is no side-B move in the base game, so the actual trigger may be:
- Up-B while holding right (diagonal Super Jump Punch), or
- B pressed while dashing/walking right (a sideways Fireball variant), or
- A menu-dispatched CPU path that invokes a non-existent side-B index and walks off a table.

Confirm with the user which input actually reproduces the crash before digging in.

## First things to capture

1. **Stack trace.** Reproduce under `lldb ./build/ssb64`, trigger the move, `bt all` on the crash. The watchdog's SIGUSR1 dump doesn't fire for synchronous segfaults — you need a debugger or a SIGSEGV handler.
2. **Last ftMainSetStatus.** Our existing `SSB64: ftMainSetStatus - status=... motion=... figatree=...` logs will show the status the fighter transitioned into just before the fault. That's the entry point for the special.

## Likely areas

- `src/ft/ftchar/ftmario/ftmariospecial*.c` — Mario's special-move status handlers. If side-B specifically: check whether any code dereferences a per-fighter table indexed by a direction enum that has no entry for Mario.
- `src/ft/ftmain.c` — `ftMainSetStatus`, status-table lookup. A NULL figatree or proc function pointer here would fault on the first `ftAnimParse` pass.
- `src/wp/` — if the move tries to spawn a weapon whose reloc file or param table is NULL on our port (unresolved PORT_RESOLVE, missing reloc_data entry), the spawn call will jump to 0.
- Reloc-table consistency: `tools/reloc_data_symbols.us.txt` + `port/resource/RelocFileTable.cpp`. A fighter-special that was renamed or reordered in one table but not the other silently returns NULL and faults on first use.

## Reproduce

Mario in any battle. Exact input sequence TBD — ask user to confirm whether it's Up-B-held-right, side-B, dashing-B, or something else. Capture reliable repro before chasing code.

## Next steps

1. Get an exact lldb backtrace of the fault.
2. Check `port_log`/`ssb64.log` for the last `ftMainSetStatus` before the crash — identifies which Mario status handler is entered.
3. If it's a weapon spawn fault, audit the reloc table for Mario-special entries.
