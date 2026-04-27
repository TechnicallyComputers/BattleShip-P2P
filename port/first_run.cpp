#include "first_run.h"
#include "port_log.h"

#include <libultraship/libultraship.h>
#include <SDL2/SDL.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ssb64 {

namespace {

// Quote a path for the shell. Wraps in double quotes and escapes embedded
// double quotes. Sufficient for our internal-only use; not a general-purpose
// shell-escape.
std::string ShellQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

// Search candidates in order; return the first that exists.
std::string FindExisting(const std::vector<std::string>& candidates) {
    for (const auto& c : candidates) {
        if (!c.empty() && fs::exists(c)) {
            return c;
        }
    }
    return {};
}

// Locate the torch binary. Tries (in priority order):
//   1. Next to ssb64 binary (shipped layout — Win/Linux: same dir; macOS:
//      Contents/MacOS/torch).
//   2. macOS Resources dir (Contents/Resources/torch).
//   3. Dev build location (build/TorchExternal/src/TorchExternal-build/torch).
//   4. PATH lookup via the bare name (last-resort).
std::string FindTorchBinary() {
    const std::string app = Ship::Context::GetAppBundlePath();
    std::vector<std::string> candidates = {
        app + "/torch",
        app + "/torch.exe",
#ifdef __APPLE__
        app + "/../Resources/torch",
#endif
        // dev: when ssb64 binary lives in <build>/, sibling dir holds torch
        app + "/TorchExternal/src/TorchExternal-build/torch",
        app + "/TorchExternal/src/TorchExternal-build/Release/torch.exe",
        app + "/TorchExternal/src/TorchExternal-build/Debug/torch.exe",
    };
    auto hit = FindExisting(candidates);
    if (!hit.empty()) return hit;

    // Last resort: PATH lookup. Returning the bare name lets std::system rely
    // on the caller's PATH; if torch isn't installed globally this just fails
    // cleanly when we exec it.
    return "torch";
}

// Locate the directory that contains config.yml (the torch project config —
// enumerates all yamls/us/*.yml extraction recipes). Torch must be invoked
// with this directory as cwd.
std::string FindTorchConfigDir() {
    const std::string app = Ship::Context::GetAppBundlePath();
    std::vector<std::string> candidates = {
#ifdef __APPLE__
        app + "/../Resources",      // shipped: ssb64.app/Contents/Resources/config.yml
#endif
        app,                        // shipped Win/Linux: next to exe
        app + "/..",                // dev: build/ssb64 → project root
        ".",                        // last-ditch cwd
    };
    for (const auto& dir : candidates) {
        if (fs::exists(dir + "/config.yml") && fs::exists(dir + "/yamls/us")) {
            return dir;
        }
    }
    return {};
}

// Locate a ROM file. Searches in priority order:
//   1. App data dir   — where the user is told to drop their ROM.
//   2. Bundle dir     — sometimes shipped together for testing.
//   3. cwd / project  — dev workflow.
std::string FindBaseRom() {
    const std::string appData = Ship::Context::GetAppDirectoryPath();
    const std::string bundle = Ship::Context::GetAppBundlePath();
    std::vector<std::string> candidates;
    for (const auto& base : {appData, bundle, bundle + "/..", std::string(".")}) {
        for (const char* ext : {"z64", "n64", "v64"}) {
            candidates.push_back(base + "/baserom.us." + ext);
        }
    }
    return FindExisting(candidates);
}

} // namespace

bool ExtractAssetsIfNeeded(const std::string& target_o2r_path) {
    if (fs::exists(target_o2r_path)) {
        return true;
    }

    port_log("first_run: %s missing — running asset extraction\n",
             target_o2r_path.c_str());

    const std::string rom = FindBaseRom();
    if (rom.empty()) {
        const std::string appData = Ship::Context::GetAppDirectoryPath();
        port_log("first_run: ERROR no ROM found.\n"
                 "  Drop a baserom.us.{z64,n64,v64} into %s\n",
                 appData.c_str());
        const std::string msg =
            "Asset extraction needs your Super Smash Bros. NTSC-U v1.0 ROM.\n\n"
            "Place a baserom.us.z64 (or .n64 / .v64) into:\n  " + appData +
            "\n\nThen launch the game again.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "ROM not found", msg.c_str(), nullptr);
        return false;
    }
    port_log("first_run: using ROM %s\n", rom.c_str());

    const std::string torch = FindTorchBinary();
    port_log("first_run: torch binary -> %s\n", torch.c_str());

    const std::string cfgDir = FindTorchConfigDir();
    if (cfgDir.empty()) {
        port_log("first_run: ERROR could not locate torch config.yml + yamls/us/\n");
        return false;
    }
    port_log("first_run: torch config dir -> %s\n", cfgDir.c_str());

    // Compose: cd "<cfgDir>" && "<torch>" o2r "<rom>"
    // Torch reads config.yml from cwd and emits ssb64.o2r in cwd
    // (the `binary:` key in config.yml is just "ssb64.o2r").
    //
    // Append the ssb64.log path to the child's stdio. Our parent process
    // has spdlog and assorted file descriptors open; if the child writes
    // to a closed/redirected stdout it can be killed by SIGPIPE (status
    // 141). Routing both streams to ssb64.log keeps the extraction trace
    // diagnosable and isolates us from stdio inheritance quirks.
    const std::string logPath = Ship::Context::GetPathRelativeToAppDirectory(
        "logs/torch-extract.log");
    fs::create_directories(fs::path(logPath).parent_path());
    std::string cmd;
#ifdef _WIN32
    cmd = "cmd /C \"cd /D " + ShellQuote(cfgDir) + " && " +
          ShellQuote(torch) + " o2r " + ShellQuote(rom) +
          " > " + ShellQuote(logPath) + " 2>&1\"";
#else
    cmd = "cd " + ShellQuote(cfgDir) + " && " +
          ShellQuote(torch) + " o2r " + ShellQuote(rom) +
          " > " + ShellQuote(logPath) + " 2>&1";
#endif
    port_log("first_run: > %s\n", cmd.c_str());

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        port_log("first_run: ERROR torch exited with %d\n", rc);
        const std::string msg =
            "Torch failed to extract assets from your ROM.\n\n"
            "Check the extraction log for details:\n  " + logPath +
            "\n\nThe most common cause is a non-NTSC-U-v1.0 ROM. Verify your "
            "dump's SHA-1 matches a supported hash printed in that log.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Asset extraction failed", msg.c_str(),
                                 nullptr);
        return false;
    }

    const std::string emitted = cfgDir + "/ssb64.o2r";
    if (!fs::exists(emitted)) {
        port_log("first_run: ERROR torch reported success but %s is missing\n",
                 emitted.c_str());
        return false;
    }

    // Move (or copy + remove if move-across-filesystems fails) into the
    // target app-data path.
    fs::create_directories(fs::path(target_o2r_path).parent_path());
    std::error_code ec;
    fs::rename(emitted, target_o2r_path, ec);
    if (ec) {
        // Cross-filesystem rename failed — fall back to copy + delete.
        ec.clear();
        fs::copy_file(emitted, target_o2r_path,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            port_log("first_run: ERROR failed to install %s -> %s: %s\n",
                     emitted.c_str(), target_o2r_path.c_str(),
                     ec.message().c_str());
            return false;
        }
        fs::remove(emitted, ec);
    }

    port_log("first_run: extracted ssb64.o2r -> %s\n",
             target_o2r_path.c_str());
    return true;
}

} // namespace ssb64
