#pragma once

#include <string>

namespace ssb64 {

/* Ensure ssb64.o2r exists at `target_o2r_path`. If it doesn't, locate a ROM
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
bool ExtractAssetsIfNeeded(const std::string& target_o2r_path);

} // namespace ssb64
