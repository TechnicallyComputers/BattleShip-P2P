#!/usr/bin/env bash
# Builds Super Smash Bros. 64 as a Linux AppImage.
#
# Output: <repo-root>/dist/SuperSmashBros64-x86_64.AppImage
#
# AppDir layout produced (before appimagetool packs it):
#   AppDir/
#     usr/bin/SuperSmashBros64           — main executable
#     usr/bin/torch                      — sidecar for first-run extraction
#     usr/share/ssb64/f3d.o2r            — Fast3D shaders (ROM-independent)
#     usr/share/ssb64/config.yml         — Torch extraction config
#     usr/share/ssb64/yamls/us/*.yml     — Torch extraction recipes
#     usr/share/ssb64/gamecontrollerdb.txt — SDL controller mappings
#     SuperSmashBros64.desktop           — XDG desktop entry
#     AppRun                             — entry-point shim
#     ssb64.png                          — placeholder icon (256x256)
#
# Built with NON_PORTABLE=ON so saves and config land in
# $XDG_DATA_HOME/ssb64/ (or ~/.local/share/ssb64/) instead of cwd.
# ssb64.o2r is NOT bundled — extracted on first launch via the ImGui
# wizard from the user's ROM into the app-data dir.
#
# Requires: appimagetool in PATH. linuxdeploy is optional but useful.
#   appimagetool: https://github.com/AppImage/AppImageKit/releases
#
# If appimagetool isn't available, the script still produces the
# AppDir tree under dist/ so you can package it later.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-bundle-linux"
DIST_DIR="$ROOT/dist"
APPDIR="$DIST_DIR/SuperSmashBros64.AppDir"
APP_NAME="SuperSmashBros64"
APPIMAGE="$DIST_DIR/${APP_NAME}-x86_64.AppImage"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

step() { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

[[ "$(uname -s)" == "Linux" ]] || fail "package-linux.sh runs on Linux only"

# ── 0. Run codegen scripts that don't need the ROM ──
step "Encoding credits text"
(
    cd "$ROOT/src/credits"
    for f in staff.credits.us.txt titles.credits.us.txt; do
        python3 "$ROOT/tools/creditsTextConverter.py" "$f" > /dev/null
    done
    for f in info.credits.us.txt companies.credits.us.txt; do
        python3 "$ROOT/tools/creditsTextConverter.py" -paragraphFont "$f" > /dev/null
    done
)

# ── 1. Configure + build with NON_PORTABLE=ON ──
step "Configuring release build with NON_PORTABLE=ON"
cmake -B "$BUILD_DIR" "$ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNON_PORTABLE=ON \
    >/dev/null

step "Building ssb64 + torch"
cmake --build "$BUILD_DIR" -j"$JOBS"

# ── 2. Build f3d.o2r (zip of LUS shaders) ──
step "Packaging Fast3D shader archive"
F3D_O2R="$BUILD_DIR/f3d.o2r"
rm -f "$F3D_O2R"
( cd "$ROOT/libultraship/src/fast" && zip -rq "$F3D_O2R" shaders )
[[ -f "$F3D_O2R" ]] || fail "f3d.o2r was not created"

# ── 3. Locate built artifacts ──
SSB64_BIN="$BUILD_DIR/ssb64"
TORCH_BIN="$BUILD_DIR/TorchExternal/src/TorchExternal-build/torch"
[[ -x "$SSB64_BIN" ]] || fail "ssb64 binary not found at $SSB64_BIN"
[[ -x "$TORCH_BIN" ]] || fail "torch binary not found at $TORCH_BIN"

# ── 4. Assemble AppDir ──
step "Assembling $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/ssb64/yamls/us"

cp "$SSB64_BIN"  "$APPDIR/usr/bin/$APP_NAME"
cp "$TORCH_BIN"  "$APPDIR/usr/bin/torch"
cp "$F3D_O2R"    "$APPDIR/usr/share/ssb64/f3d.o2r"
cp "$ROOT/gamecontrollerdb.txt" "$APPDIR/usr/share/ssb64/gamecontrollerdb.txt"
cp "$ROOT/config.yml" "$APPDIR/usr/share/ssb64/config.yml"
cp "$ROOT/yamls/us/"*.yml "$APPDIR/usr/share/ssb64/yamls/us/"

# ── 5. AppRun + .desktop + icon ──
# AppRun is what the AppImage calls when launched. Switch cwd to the
# data dir so torch can find config.yml + yamls/, then exec the binary.
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="$HERE/usr/bin:$PATH"
cd "$HERE/usr/share/ssb64" || exit 1
exec "$HERE/usr/bin/SuperSmashBros64" "$@"
EOF
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/SuperSmashBros64.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Super Smash Bros. 64
Exec=SuperSmashBros64
Icon=ssb64
Categories=Game;ArcadeGame;
Terminal=false
EOF

# Placeholder icon — solid PNG until real artwork lands. Generate a
# 256x256 transparent PNG using printf if convert isn't available.
if command -v convert >/dev/null 2>&1; then
    convert -size 256x256 xc:'#1a1a2e' \
        -fill white -gravity center -pointsize 48 -annotate 0 "SSB64" \
        "$APPDIR/ssb64.png"
else
    # Minimal valid 1x1 PNG. AppImage requires the icon file to exist;
    # users can replace with real art later.
    printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\rIDATx\x9cc\xfc\xff\xff?\x00\x05\xfe\x02\xfe\xa3o\xae\x9d\x00\x00\x00\x00IEND\xaeB`\x82' \
        > "$APPDIR/ssb64.png"
fi

# ── 6. Pack into AppImage if appimagetool is available ──
if command -v appimagetool >/dev/null 2>&1; then
    step "Packing AppImage"
    rm -f "$APPIMAGE"
    appimagetool "$APPDIR" "$APPIMAGE"
    [[ -f "$APPIMAGE" ]] || fail "appimagetool did not produce $APPIMAGE"
    chmod +x "$APPIMAGE"
    APP_KB=$(du -k "$APPIMAGE" | awk '{print $1}')
    printf '\n\033[32m✓ AppImage ready: %s (%s KB)\033[0m\n' "$APPIMAGE" "$APP_KB"
else
    APP_KB=$(du -sk "$APPDIR" | awk '{print $1}')
    printf '\n\033[33m! appimagetool not in PATH — produced AppDir only.\033[0m\n'
    printf '   AppDir: %s (%s KB)\n' "$APPDIR" "$APP_KB"
    printf '   Install appimagetool from https://github.com/AppImage/AppImageKit/releases\n'
    printf '   then run: appimagetool "%s" "%s"\n' "$APPDIR" "$APPIMAGE"
fi
printf '   App-data: $XDG_DATA_HOME/ssb64/ (or ~/.local/share/ssb64/)\n'
printf '   First launch will prompt for your ROM via the ImGui wizard.\n'
