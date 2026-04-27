# build.ps1 - Build SSB64 port and extract assets (Windows)
#
# Usage:
#   .\build.ps1                # Full build + extract (Debug)
#   .\build.ps1 -SkipExtract   # Build only, skip asset extraction
#   .\build.ps1 -ExtractOnly   # Extract assets only (assumes Torch already built)
#   .\build.ps1 -Clean         # Clean build from scratch
#   .\build.ps1 -Release       # Release config (default: Debug)
#   .\build.ps1 -Jobs 8        # Parallel build job count (default: NUMBER_OF_PROCESSORS)
#   .\build.ps1 -Help          # Show this help

param(
    [switch]$SkipExtract,
    [switch]$ExtractOnly,
    [switch]$Clean,
    [switch]$Release,
    [switch]$Debug,
    [int]$Jobs = 0,
    [switch]$Help
)

if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -First 11 | ForEach-Object { Write-Host $_ }
    exit 0
}

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$Config = if ($Release) { "Release" } else { "Debug" }
if ($Jobs -le 0) {
    $Jobs = [int]($env:NUMBER_OF_PROCESSORS)
    if ($Jobs -le 0) { $Jobs = 4 }
}

$BuildDir = Join-Path $Root "build"
$ROM = Join-Path $Root "baserom.us.z64"
$O2R = Join-Path $Root "ssb64.o2r"
$F3DO2R = Join-Path $Root "f3d.o2r"
$Fast3DShaderDir = Join-Path $Root "libultraship\src\fast\shaders"
$TorchExe = Join-Path $BuildDir "TorchExternal\src\TorchExternal-build\$Config\torch.exe"
$GameExe = Join-Path $BuildDir "$Config\ssb64.exe"
$ExeDir = Split-Path $GameExe -Parent

function Write-Step($msg) {
    Write-Host "`n=== $msg ===" -ForegroundColor Cyan
}

# ── Clean ──
if ($Clean) {
    Write-Step "Cleaning build directory"
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    if (Test-Path $O2R) {
        Remove-Item -Force $O2R
    }
    if (Test-Path $F3DO2R) {
        Remove-Item -Force $F3DO2R
    }
}

# ── Validate ROM ──
if (-not (Test-Path $ROM)) {
    Write-Host "ERROR: ROM not found at $ROM" -ForegroundColor Red
    Write-Host "Place your NTSC-U v1.0 ROM as baserom.us.z64 in the project root."
    exit 1
}

# ── Submodules ──
if (-not $ExtractOnly) {
    Write-Step "Initializing submodules"
    git -C $Root submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { Write-Host "Submodule init failed" -ForegroundColor Red; exit 1 }
}

# ── Generate reloc_data.h, Torch YAML configs, RelocFileTable.cpp ──
# All three are downstream of tools/reloc_data_symbols.us.txt and the ROM.
#
# Pipeline:
#   reloc_data_symbols.us.txt
#     -> generate_reloc_stubs.py  -> include/reloc_data.h
#          -> generate_yamls.py   -> yamls/us/reloc_*.yml       (gitignored)
#               -> generate_reloc_table.py -> port/resource/RelocFileTable.cpp
#                    -> Torch               -> ssb64.o2r
#
# reloc_data.h + yamls/us/reloc_*.yml are gitignored and must be rebuilt on
# every fresh checkout. RelocFileTable.cpp is committed, but we still
# regenerate it here so it stays in lock-step with whatever the YAML
# generator emitted — if the two ever disagree at runtime, the resource
# loader falls back to the file_NNNN fallback names and every fighter /
# sprite / icon lookup silently returns NULL.
if (-not $ExtractOnly) {
    Write-Step "Regenerating include/reloc_data.h"
    python "$Root\tools\generate_reloc_stubs.py"
    if ($LASTEXITCODE -ne 0) { Write-Host "reloc_data.h generation failed" -ForegroundColor Red; exit 1 }

    Write-Step "Regenerating Torch YAML extraction configs"
    python "$Root\tools\generate_yamls.py"
    if ($LASTEXITCODE -ne 0) { Write-Host "generate_yamls.py failed" -ForegroundColor Red; exit 1 }

    Write-Step "Regenerating port\resource\RelocFileTable.cpp"
    Push-Location $Root
    try {
        python "$Root\tools\generate_reloc_table.py"
        if ($LASTEXITCODE -ne 0) { Write-Host "generate_reloc_table.py failed" -ForegroundColor Red; exit 1 }
    } finally { Pop-Location }
}

# ── Encode credits text ──
# staff/titles/info/companies credit strings are #include'd directly
# into scstaffroll.c via .credits.encoded / .credits.metadata files,
# which are gitignored. Every fresh checkout has to rerun the encoder.
# staff/titles use the default title font; info/companies need the
# paragraph font for digits, punctuation and accents. The tool is
# idempotent — it overwrites the outputs unconditionally.
if (-not $ExtractOnly) {
    Write-Step "Encoding credits text"
    Push-Location (Join-Path $Root "src\credits")
    try {
        foreach ($name in @("staff.credits.us.txt", "titles.credits.us.txt")) {
            python "$Root\tools\creditsTextConverter.py" $name | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Host "credits encode failed: $name" -ForegroundColor Red; exit 1 }
        }
        foreach ($name in @("info.credits.us.txt", "companies.credits.us.txt")) {
            python "$Root\tools\creditsTextConverter.py" -paragraphFont $name | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Host "credits encode failed: $name" -ForegroundColor Red; exit 1 }
        }
    } finally {
        Pop-Location
    }
}

# ── CMake configure ──
if (-not $ExtractOnly) {
    Write-Step "Configuring CMake ($Config)"
    cmake -S $Root -B $BuildDir
    if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure failed" -ForegroundColor Red; exit 1 }
}

# ── Build game ──
if (-not $ExtractOnly) {
    Write-Step "Building ssb64 ($Config, j=$Jobs)"
    cmake --build $BuildDir --target ssb64 --config $Config --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { Write-Host "Game build failed" -ForegroundColor Red; exit 1 }
    Write-Host "Game built: $GameExe" -ForegroundColor Green
}

# ── Build Torch + Extract assets ──
if (-not $SkipExtract) {
    # Build Torch via ExternalProject
    Write-Step "Building Torch ($Config, j=$Jobs)"
    cmake --build $BuildDir --target TorchExternal --config $Config --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { Write-Host "Torch build failed" -ForegroundColor Red; exit 1 }

    # Try several known torch.exe locations (mirrors build.sh fallback list)
    $TorchExe = $null
    foreach ($cand in @(
        (Join-Path $BuildDir "torch-install\bin\torch.exe"),
        (Join-Path $BuildDir "TorchExternal\src\TorchExternal-build\$Config\torch.exe"),
        (Join-Path $BuildDir "TorchExternal\src\TorchExternal-build\Debug\torch.exe"),
        (Join-Path $BuildDir "TorchExternal\src\TorchExternal-build\Release\torch.exe"),
        (Join-Path $BuildDir "TorchExternal\src\TorchExternal-build\torch.exe")
    )) {
        if (Test-Path $cand) { $TorchExe = $cand; break }
    }

    if (-not $TorchExe) {
        Write-Host "ERROR: torch.exe not found after build" -ForegroundColor Red
        exit 1
    }

    Write-Step "Extracting assets from ROM"
    Push-Location $Root
    & $TorchExe o2r $ROM 2>&1 | ForEach-Object { $_ }
    $exitCode = $LASTEXITCODE
    Pop-Location

    if ($exitCode -ne 0) {
        Write-Host "Asset extraction failed (exit code $exitCode)" -ForegroundColor Red
        exit 1
    }

    if (-not (Test-Path $O2R)) {
        Write-Host "ERROR: ssb64.o2r was not created" -ForegroundColor Red
        exit 1
    }

    $o2rSize = (Get-Item $O2R).Length / 1MB
    Write-Host ("Assets extracted: ssb64.o2r ({0:N1} MB)" -f $o2rSize) -ForegroundColor Green

    # Copy o2r next to exe
    if ((Test-Path $ExeDir) -and ($ExeDir -ne $Root)) {
        Copy-Item $O2R (Join-Path $ExeDir "ssb64.o2r") -Force
        Write-Host "Copied ssb64.o2r to $ExeDir"
    }
}

# ── Package Fast3D shader archive ──
Write-Step "Packaging Fast3D shader archive"
if (-not (Test-Path $Fast3DShaderDir)) {
    Write-Host "ERROR: Fast3D shader directory not found at $Fast3DShaderDir" -ForegroundColor Red
    exit 1
}

if (Test-Path $F3DO2R) {
    Remove-Item -Force $F3DO2R
}

# The resource manager loads shaders as "shaders/opengl/...", "shaders/directx/...", etc.
# ZipFile::CreateFromDirectory on Windows stores entries with backslashes, which breaks
# zip_name_locate (exact forward-slash match). Write entries individually with explicit
# forward-slash paths using ZipArchive.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [System.IO.File]::Open($F3DO2R, [System.IO.FileMode]::Create)
$zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
Get-ChildItem -Recurse -File $Fast3DShaderDir | ForEach-Object {
    $rel = "shaders/" + $_.FullName.Substring($Fast3DShaderDir.Length + 1).Replace('\', '/')
    $entry = $zip.CreateEntry($rel)
    $es = $entry.Open()
    $fs = [System.IO.File]::OpenRead($_.FullName)
    $fs.CopyTo($es)
    $fs.Close()
    $es.Close()
}
$zip.Dispose()
$stream.Close()

if (-not (Test-Path $F3DO2R)) {
    Write-Host "ERROR: f3d.o2r was not created" -ForegroundColor Red
    exit 1
}

$f3dSizeKB = [math]::Round((Get-Item $F3DO2R).Length / 1KB, 1)
Write-Host ("Packaged f3d.o2r ({0:N1} KB)" -f $f3dSizeKB) -ForegroundColor Green

if ((Test-Path $ExeDir) -and ($ExeDir -ne $Root)) {
    Copy-Item $F3DO2R (Join-Path $ExeDir "f3d.o2r") -Force
    Write-Host "Copied f3d.o2r to $ExeDir"
}

# ── Done ──
Write-Host "`n" -NoNewline
Write-Step "Build complete"
if (Test-Path $GameExe) {
    Write-Host "  Executable: $GameExe"
}
if (Test-Path $O2R) {
    Write-Host "  Assets:     $O2R"
}
if (Test-Path $F3DO2R) {
    Write-Host "  Fast3D:     $F3DO2R"
}
Write-Host ""
