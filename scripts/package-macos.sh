#!/usr/bin/env bash
# Builds Super Smash Bros. 64 as a self-contained macOS .app bundle.
#
# Output: <repo-root>/dist/SuperSmashBros64.app
#
# Layout produced:
#   SuperSmashBros64.app/
#     Contents/
#       Info.plist
#       MacOS/
#         SuperSmashBros64           — main executable
#         torch                      — sidecar for first-run extraction
#       Resources/
#         f3d.o2r                    — Fast3D shader archive (ROM-independent)
#         config.yml                 — Torch extraction config
#         yamls/us/*.yml             — Torch extraction recipes
#         gamecontrollerdb.txt       — SDL controller mappings
#
# The bundle is built with NON_PORTABLE=ON so the runtime resolves saves,
# ssb64.cfg.json, and the user's extracted ssb64.o2r out of the OS app-data
# dir (~/Library/Application Support/ssb64/) instead of cwd.
#
# Notes:
# - The .app does NOT include ssb64.o2r — that's ROM-derived and gets
#   extracted on first launch via the ImGui wizard.
# - Code signing / notarization is not handled here; expect Gatekeeper to
#   warn on first launch. A future pass should sign with `codesign --deep`
#   and notarize via `notarytool`.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-bundle"
DIST_DIR="$ROOT/dist"
APP_NAME="SuperSmashBros64"
APP="$DIST_DIR/$APP_NAME.app"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

step() { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || fail "package-macos.sh runs on macOS only"

# ── 0. Run codegen scripts that don't need the ROM ──
# Encoded credit files are gitignored (input text is in src/credits/),
# so a fresh checkout (CI or otherwise) must run the encoder before
# cmake builds scstaffroll.c. ROM-independent — same step as build.sh.
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

# Build the f3d.o2r shader archive (ROM-independent, just zips the LUS
# shaders directory). build.sh produces this at $ROOT/f3d.o2r — reuse the
# same recipe rather than re-implement.
step "Packaging Fast3D shader archive"
F3D_O2R="$BUILD_DIR/f3d.o2r"
rm -f "$F3D_O2R"
( cd "$ROOT/libultraship/src/fast" && zip -rq "$F3D_O2R" shaders )
[[ -f "$F3D_O2R" ]] || fail "f3d.o2r was not created"

# ── 2. Locate built artifacts ──
SSB64_BIN="$BUILD_DIR/ssb64"
TORCH_BIN="$BUILD_DIR/TorchExternal/src/TorchExternal-build/torch"
[[ -x "$SSB64_BIN" ]] || fail "ssb64 binary not found at $SSB64_BIN"
[[ -x "$TORCH_BIN" ]] || fail "torch binary not found at $TORCH_BIN"

# ── 3. Assemble the bundle ──
step "Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/yamls/us"

cp "$SSB64_BIN"  "$APP/Contents/MacOS/$APP_NAME"
cp "$TORCH_BIN"  "$APP/Contents/MacOS/torch"
cp "$F3D_O2R"    "$APP/Contents/Resources/f3d.o2r"
cp "$ROOT/gamecontrollerdb.txt" "$APP/Contents/Resources/gamecontrollerdb.txt"
cp "$ROOT/config.yml" "$APP/Contents/Resources/config.yml"
cp "$ROOT/yamls/us/"*.yml "$APP/Contents/Resources/yamls/us/"
cp "$ROOT/assets/icon.icns" "$APP/Contents/Resources/AppIcon.icns"

# ── 4. Info.plist ──
# Minimal but sufficient: bundle ID, version, executable name, high-DPI flag.
# CFBundleIdentifier picks the same reverse-DNS the user's app-data dir
# is scoped to (ssb64), keeping save state stable across signed/unsigned
# rebuilds.
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                <string>Super Smash Bros. 64</string>
    <key>CFBundleDisplayName</key>         <string>Super Smash Bros. 64</string>
    <key>CFBundleIdentifier</key>          <string>com.ssb-decomp-re.ssb64</string>
    <key>CFBundleVersion</key>             <string>1.0</string>
    <key>CFBundleShortVersionString</key>  <string>1.0</string>
    <key>CFBundlePackageType</key>         <string>APPL</string>
    <key>CFBundleSignature</key>           <string>????</string>
    <key>CFBundleExecutable</key>          <string>$APP_NAME</string>
    <key>CFBundleIconFile</key>            <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>      <string>11.0</string>
    <key>NSHighResolutionCapable</key>     <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
</dict>
</plist>
EOF

# Make the binaries executable (cp preserves mode, but be defensive).
chmod +x "$APP/Contents/MacOS/$APP_NAME" "$APP/Contents/MacOS/torch"

# ── 4b. Adhoc-sign the bundle as a unit ──
# Modern Gatekeeper (Sequoia / 15.x+) flags downloaded adhoc-signed
# bundles as "damaged" if the signature isn't deep enough to cover
# every Mach-O inside. cp preserved each binary's linker-signature
# individually, but the bundle as a whole isn't sealed — so the .app
# refuses to install / launch from a quarantined DMG.
#
# `codesign --deep --force --sign -` walks the bundle, adhoc-signs
# every executable and dylib, and writes a bundle-level signature
# referencing all of them. Doesn't need an Apple Developer cert; it's
# enough to satisfy Gatekeeper's structural check on a quarantined
# download. For full "no warnings, no right-click-Open" we'd need a
# real Developer ID Apple Distribution cert + notarization, but that
# costs $99/yr and isn't tractable for an unsigned community port.
#
# Users who still hit "damaged" can run:
#   xattr -dr com.apple.quarantine /Applications/SuperSmashBros64.app
step "Adhoc-signing bundle"
codesign --deep --force --sign - "$APP"
codesign --verify --deep --strict "$APP" \
    && echo "  signature verified" \
    || fail "codesign verify failed on $APP"

# ── 5. Build a drag-and-drop DMG ──
# Standard macOS UX: user double-clicks the .dmg, sees a window with the
# .app and a shortcut to /Applications side by side, drags one onto the
# other. We layer in a custom background image (assets/macos_dmg_banner.png)
# by first producing a writable UDRW image, mounting it, applying Finder
# styling via AppleScript, then converting to a compressed UDZO.
DMG_VOLNAME="Super Smash Bros. 64"
DMG_BG_SRC="$ROOT/assets/macos_dmg_banner.png"
# Cap the long edge at 600px so the installer window matches the
# standard macOS DMG footprint (~600x400 territory) instead of the
# source artwork's full 1586x992. We preserve the artwork's aspect
# ratio and derive window bounds from the actual scaled output, so
# re-cuts of the source banner drop in without code changes. A 2x
# variant is generated alongside; Finder picks `background@2x.png`
# automatically on retina displays.
DMG_BG_LONG=600

step "Building DMG"
DMG="$DIST_DIR/$APP_NAME.dmg"
DMG_TMP="$DIST_DIR/$APP_NAME-rw.dmg"
DMG_STAGE="$DIST_DIR/dmg-stage"
rm -rf "$DMG_STAGE" "$DMG" "$DMG_TMP"
mkdir -p "$DMG_STAGE/.background"
cp -R "$APP" "$DMG_STAGE/"
ln -s /Applications "$DMG_STAGE/Applications"
sips -Z $((DMG_BG_LONG * 2)) "$DMG_BG_SRC" \
    --out "$DMG_STAGE/.background/background@2x.png" >/dev/null
sips -Z $DMG_BG_LONG "$DMG_BG_SRC" \
    --out "$DMG_STAGE/.background/background.png" >/dev/null
# Window bounds match the actual scaled image dimensions — keeps the
# artwork undistorted regardless of the source's aspect ratio.
DMG_BG_W=$(sips -g pixelWidth  "$DMG_STAGE/.background/background.png" | awk '/pixelWidth/  {print $2}')
DMG_BG_H=$(sips -g pixelHeight "$DMG_STAGE/.background/background.png" | awk '/pixelHeight/ {print $2}')

# Create a writable image we can mount and decorate.
hdiutil create \
    -volname "$DMG_VOLNAME" \
    -srcfolder "$DMG_STAGE" \
    -ov \
    -format UDRW \
    -fs HFS+ \
    "$DMG_TMP" >/dev/null

# Mount under /Volumes/<volname> (no -mountpoint override) so Finder
# AppleScript can address it by `disk "<volname>"`. Detach any prior
# mount of the same volume first, in case a previous run left it
# attached.
if [[ -d "/Volumes/$DMG_VOLNAME" ]]; then
    hdiutil detach "/Volumes/$DMG_VOLNAME" -force >/dev/null 2>&1 || true
fi
hdiutil attach "$DMG_TMP" -nobrowse -noverify -noautoopen >/dev/null
MOUNT_DIR="/Volumes/$DMG_VOLNAME"
[[ -d "$MOUNT_DIR" ]] || fail "DMG did not mount at $MOUNT_DIR"

# Apply Finder styling: window size matches the background, icons placed
# over the two halves (app on left, Applications shortcut on right).
osascript <<APPLESCRIPT >/dev/null || true
tell application "Finder"
    tell disk "$DMG_VOLNAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 200 + $DMG_BG_W, 120 + $DMG_BG_H}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set background picture of viewOptions to file ".background:background.png"
        set position of item "$APP_NAME.app" of container window to {$((DMG_BG_W / 4)), $((DMG_BG_H * 3 / 5))}
        set position of item "Applications" of container window to {$((DMG_BG_W * 3 / 4)), $((DMG_BG_H * 3 / 5))}
        update without registering applications
        delay 1
        close
    end tell
end tell
APPLESCRIPT

sync
hdiutil detach "$MOUNT_DIR" >/dev/null

# Convert to compressed read-only image — this is what ships.
hdiutil convert "$DMG_TMP" -format UDZO -imagekey zlib-level=9 -ov -o "$DMG" >/dev/null
rm -f "$DMG_TMP"
rm -rf "$DMG_STAGE"
[[ -f "$DMG" ]] || fail "DMG was not created"

# ── 6. Report ──
APP_KB=$(du -sk "$APP" | awk '{print $1}')
DMG_KB=$(du -sk "$DMG" | awk '{print $1}')
printf '\n\033[32m✓ Bundle: %s (%s KB)\033[0m\n' "$APP" "$APP_KB"
printf '\033[32m✓ DMG:    %s (%s KB)\033[0m\n' "$DMG" "$DMG_KB"
printf '   To run from the bundle:        open "%s"\n' "$APP"
printf '   To install from the DMG:       open "%s"  (then drag to Applications)\n' "$DMG"
printf '   App-data: ~/Library/Application Support/ssb64/\n'
printf '   First launch will prompt for your ROM via the ImGui wizard.\n'
