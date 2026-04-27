# Builds Super Smash Bros. 64 as a self-contained Windows release zip.
#
# Output: <repo-root>\dist\SuperSmashBros64-windows.zip
#
# Layout produced (extracted):
#   SuperSmashBros64\
#     SuperSmashBros64.exe       — main executable
#     torch.exe                  — sidecar for first-run extraction
#     f3d.o2r                    — Fast3D shader archive (ROM-independent)
#     config.yml                 — Torch extraction config
#     yamls\us\*.yml             — Torch extraction recipes
#     gamecontrollerdb.txt       — SDL controller mappings
#     SDL2.dll                   — runtime dependency (vcpkg-bundled)
#     <other vcpkg DLLs>         — picked up by Get-ChildItem from build dir
#
# Built with NON_PORTABLE=ON so saves and config land in
# %APPDATA%\ssb64\ instead of next to the .exe — same as macOS bundle.
# ssb64.o2r is NOT bundled; the first-run wizard extracts it from the
# user's ROM into %APPDATA%\ssb64\ssb64.o2r.

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build-bundle-win"
$DistDir = Join-Path $Root "dist"
$AppName = "SuperSmashBros64"
$StageDir = Join-Path $DistDir $AppName
$ZipPath = Join-Path $DistDir "$AppName-windows.zip"
$Jobs = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 }

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# ── 1. Configure + build with NON_PORTABLE=ON (Release) ──
Write-Step "Configuring release build with NON_PORTABLE=ON"
cmake -B $BuildDir $Root `
    -DCMAKE_BUILD_TYPE=Release `
    -DNON_PORTABLE=ON `
    | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }

Write-Step "Building ssb64 + torch"
cmake --build $BuildDir --config Release -j $Jobs
if ($LASTEXITCODE -ne 0) { Fail "build failed" }

# ── 2. Build f3d.o2r (zip of LUS shaders, ROM-independent) ──
Write-Step "Packaging Fast3D shader archive"
$F3DO2R = Join-Path $BuildDir "f3d.o2r"
if (Test-Path $F3DO2R) { Remove-Item $F3DO2R -Force }
$ShaderSrc = Join-Path $Root "libultraship\src\fast"
Push-Location $ShaderSrc
Compress-Archive -Path "shaders" -DestinationPath $F3DO2R -CompressionLevel Optimal
Pop-Location
if (-not (Test-Path $F3DO2R)) { Fail "f3d.o2r was not created" }

# ── 3. Locate built artifacts ──
$SsbExe = Join-Path $BuildDir "Release\ssb64.exe"
if (-not (Test-Path $SsbExe)) {
    # Fall back to non-multi-config layout (Ninja).
    $SsbExe = Join-Path $BuildDir "ssb64.exe"
}
$TorchExe = $null
foreach ($cand in @(
    "TorchExternal\src\TorchExternal-build\Release\torch.exe",
    "TorchExternal\src\TorchExternal-build\torch.exe",
    "torch-install\bin\torch.exe"
)) {
    $p = Join-Path $BuildDir $cand
    if (Test-Path $p) { $TorchExe = $p; break }
}
if (-not (Test-Path $SsbExe))    { Fail "ssb64.exe not found at $SsbExe" }
if (-not $TorchExe)              { Fail "torch.exe not found in $BuildDir" }

# ── 4. Stage the release tree ──
Write-Step "Staging $StageDir"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Path $StageDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $StageDir "yamls\us") | Out-Null

Copy-Item $SsbExe        (Join-Path $StageDir "$AppName.exe")
Copy-Item $TorchExe      (Join-Path $StageDir "torch.exe")
Copy-Item $F3DO2R        (Join-Path $StageDir "f3d.o2r")
Copy-Item (Join-Path $Root "gamecontrollerdb.txt") $StageDir
Copy-Item (Join-Path $Root "config.yml") $StageDir
Copy-Item (Join-Path $Root "yamls\us\*.yml") (Join-Path $StageDir "yamls\us")

# Bundle DLLs that landed next to ssb64.exe (vcpkg drops SDL2.dll, etc.).
$ExeBuildDir = Split-Path $SsbExe -Parent
Get-ChildItem -Path $ExeBuildDir -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName $StageDir
}

# ── 5. Zip ──
Write-Step "Compressing $ZipPath"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal
if (-not (Test-Path $ZipPath)) { Fail "zip was not created" }

$ZipKB = [int]((Get-Item $ZipPath).Length / 1024)
Write-Host "`n✓ Release zip ready: $ZipPath ($ZipKB KB)" -ForegroundColor Green
Write-Host "   App-data: %APPDATA%\ssb64\"
Write-Host "   First launch will prompt for your ROM via the ImGui wizard."
