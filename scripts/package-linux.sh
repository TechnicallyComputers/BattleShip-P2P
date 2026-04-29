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

# Application icon. AppImage looks for <Icon>.png at the AppDir root and
# (for hicolor integration) under usr/share/icons/hicolor/<size>/apps/.
# Source is assets/icon.png; we downscale a 256x256 copy for the AppDir
# root (kept small to keep the AppImage lean) and ship the full-resolution
# PNG in the hicolor 512x512 slot.
ICON_SRC="$ROOT/assets/icon.png"
[[ -f "$ICON_SRC" ]] || fail "missing assets/icon.png"
mkdir -p "$APPDIR/usr/share/icons/hicolor/512x512/apps" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"
if command -v magick >/dev/null 2>&1; then
    magick "$ICON_SRC" -resize 256x256 "$APPDIR/ssb64.png"
    magick "$ICON_SRC" -resize 256x256 "$APPDIR/usr/share/icons/hicolor/256x256/apps/ssb64.png"
elif command -v convert >/dev/null 2>&1; then
    convert "$ICON_SRC" -resize 256x256 "$APPDIR/ssb64.png"
    convert "$ICON_SRC" -resize 256x256 "$APPDIR/usr/share/icons/hicolor/256x256/apps/ssb64.png"
elif python3 -c "import PIL" 2>/dev/null; then
    python3 - "$ICON_SRC" "$APPDIR/ssb64.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/ssb64.png" <<'PY'
import sys
from PIL import Image
src, *outs = sys.argv[1:]
img = Image.open(src).convert("RGBA").resize((256, 256), Image.LANCZOS)
for o in outs:
    img.save(o)
PY
else
    # No resizer available — ship the source PNG as-is. AppImage tolerates
    # larger icons; Explorer integration is just slightly heavier.
    cp "$ICON_SRC" "$APPDIR/ssb64.png"
    cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/ssb64.png"
fi
cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/512x512/apps/ssb64.png"

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
