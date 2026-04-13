# Particle Bank ROM DMA Segfault (2026-04-08) — FIXED

**Symptoms:** Segfault in scene 30 (nSCKindOpeningMario) during `itManagerInitItems()` → `efParticleGetLoadBankID()`. Crash handler didn't fire. No log output after "about to call func_start".

**Root cause:** `efParticleGetLoadBankID` uses `&lITManagerParticleScriptBankLo/Hi` linker symbol addresses as ROM offsets to DMA-read particle bank data. On PC: (1) symbols are stubs in `port/stubs/segment_symbols.c` (all = 0), so `&symbol` gives meaningless PC addresses; (2) `hi - lo` = 8 bytes (adjacent vars); (3) `syTaskmanMalloc(8)` allocates from the general heap which contains **stale data from the previous scene** (heap pointer resets between scenes but memory isn't zeroed); (4) `syDmaReadRom()` is a no-op so the buffer keeps stale data; (5) `lbParticleSetupBankID()` casts it as `LBScriptDesc*`, reads garbage `scripts_num` → iterates into a segfault. Scene 28 didn't crash because the static heap was zero-initialized on first use.

**Fix:** `#ifdef PORT` guard in `efParticleGetLoadBankID()` skips ROM DMA path entirely. Registers a dummy empty bank so callers get valid bank IDs. Particle emission code already has bounds checks that handle empty banks gracefully.

**Files:** `src/ef/efparticle.c`
