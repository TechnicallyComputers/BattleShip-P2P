#pragma once

#include <string>

namespace ssb64 {

/* Ensure BattleShip.o2r exists at `target_o2r_path`. If it doesn't, locate a ROM
 * (.z64 / .n64 / .v64), the torch binary, and the asset extraction yamls,
 * then shell out to torch to extract.
 *
 * Returns true if `target_o2r_path` exists (or was successfully extracted)
 * after the call. Returns false if extraction was needed but couldn't be
 * performed (no ROM, no torch, or torch failed) — caller should surface a
 * user-facing error.
 *
 * Logs every step via port_log so failures are diagnosable from ssb64.log.
 */
// silent=true suppresses native SDL_ShowSimpleMessageBox popups. The
// ImGui wizard sets this so failures land in the wizard's own status
// line instead of an alien-looking second dialog.
bool ExtractAssetsIfNeeded(const std::string& target_o2r_path,
                           bool silent = false);

/* Drive an ImGui first-run wizard until either:
 *   - the user provides a ROM and extraction succeeds (returns true,
 *     target_o2r_path now exists), or
 *   - the user closes the window / picks Quit (returns false, caller
 *     should exit).
 *
 * Requires Window/Gui already initialized. Spins its own pre-gameloop
 * render loop using gui->StartDraw() / window->RunGuiOnly() / gui->EndDraw().
 */
bool RunFirstRunWizard(const std::string& target_o2r_path);

} // namespace ssb64
