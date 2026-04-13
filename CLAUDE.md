# SSB64 PC Port — Claude Session Context

PC port of Super Smash Bros. 64 built from the complete decompilation at github.com/Killian-C/ssb-decomp-re. Target integration: libultraship (LUS) + Torch asset pipeline.

## Documentation

Detailed reference material lives under `docs/`. Read the file that matches the task before touching code. When looking for a topic not listed here, run `ls docs/` and `ls docs/bugs/` to see what's available.

| Topic | File |
|-------|------|
| Project status, ROM info, dependencies, source tree layout | `docs/architecture.md` |
| C type system, decomp naming prefixes, code style, macros | `docs/c_conventions.md` |
| RDRAM / RSP / RDP / GBI / audio / threading / controller / endianness | `docs/n64_reference.md` |
| CMake build, reloc stub regen, runtime logs, LP64 compat notes | `docs/build_and_tooling.md` |
| GBI trace capture (port + M64P plugin) and `gbi_diff.py` usage | `docs/debug_gbi_trace.md` |
| Resolved bugs (index + per-bug root cause / fix write-ups) | `docs/bugs/README.md` |

Ongoing investigations and handoff notes are loose `.md` files at the top level of `docs/` — check there before starting work on rendering, collision, or animation issues so you don't duplicate prior effort.

When you fix a new significant bug, add an entry under `docs/bugs/` using the slug pattern `<topic>_<YYYY-MM-DD>.md` and link it from `docs/bugs/README.md`.

---

## Agent Directives

### Pre-Work

1. **THE "STEP 0" RULE**: Before any structural refactor on a file >300 LOC, first remove dead code, unused exports, unused imports, and debug logs. Commit cleanup separately.

2. **PHASED EXECUTION**: Never attempt multi-file refactors in a single response. Break work into phases. Complete Phase 1, run verification, wait for approval before Phase 2. Max 5 files per phase.

### Code Quality

3. **THE SENIOR DEV OVERRIDE**: If architecture is flawed, state is duplicated, or patterns are inconsistent — propose and implement structural fixes. Ask: "What would a senior, experienced, perfectionist dev reject in code review?" Fix all of it.

4. **FORCED VERIFICATION**: Do not report a task complete until you have run the build and fixed all errors. If no build is configured yet, state that explicitly.

5. **DECOMP PRESERVATION**: Never "clean up" or "modernize" decompiled game code in `src/` unless it is necessary for compilation on modern toolchains. IDO patterns (goto, odd casts, temp variables) exist for matching and must be preserved. Port-specific modifications should be wrapped in `#ifdef PORT` / `#endif` guards where possible.

### Context Management

6. **SUB-AGENT SWARMING**: For tasks touching >5 independent files, launch parallel sub-agents. Each agent gets its own context window.

7. **CONTEXT DECAY AWARENESS**: After 10+ messages, re-read any file before editing. Do not trust memory of file contents.

8. **FILE READ BUDGET**: For files over 500 LOC, use offset and limit parameters to read in chunks.

9. **EDIT INTEGRITY**: Before every edit, re-read the file. After editing, verify the change applied correctly. Never batch >3 edits to the same file without a verification read.

10. **NO SEMANTIC SEARCH**: When renaming or changing any function/type/variable, search separately for: direct calls, type references, string literals, dynamic references, re-exports, and tests.
