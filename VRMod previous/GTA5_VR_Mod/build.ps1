# GTA5VR Build Script (PowerShell)
# Usage: .\build.ps1 [-BuildType Debug|Release] [-Clean] [-Package]

param(
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release",
    [switch]$Clean,
    [switch]$Package,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

# Colors
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Warn { Write-Host $args -ForegroundColor Yellow }
function Write-Err { Write-Host $args -ForegroundColor Red }
function Write-Info { Write-Host $args -ForegroundColor Cyan }

Write-Host ""
Write-Info "========================================"
Write-Info "  GTA5VR Build Script (PowerShell)"
Write-Info "========================================"
Write-Host ""
Write-Host "Build Type: $BuildType"
Write-Host ""

# Check for CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Err "[ERROR] CMake not found!"
    Write-Host "Please install CMake and add it to your PATH."
    exit 1
}

# Find Visual Studio
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    if ($vsPath) {
        Write-Host "Found Visual Studio: $vsPath"
    }
}

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning build directory..."
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build"
    }
}

# Create build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location "build"

try {
    # Configure
    Write-Host ""
    Write-Info "Configuring with CMake..."
    Write-Host ""

    $cmakeArgs = @(
        "..",
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    )

    if ($Verbose) {
        $cmakeArgs += "--log-level=VERBOSE"
    }

    & cmake $cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }

    # Build
    Write-Host ""
    Write-Info "Building $BuildType..."
    Write-Host ""

    & cmake --build . --config $BuildType --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }

} finally {
    Pop-Location
}

# Check output
Write-Host ""
Write-Info "========================================"
Write-Info "  Build Complete!"
Write-Info "========================================"
Write-Host ""

$outputDir = "build\bin\$BuildType"

$files = @(
    @{Name = "GTA5VR.dll"; Required = $true},
    @{Name = "GTA5VR_Injector.exe"; Required = $true},
    @{Name = "GTA5VR.ini"; Required = $false}
)

foreach ($file in $files) {
    $path = Join-Path $outputDir $file.Name
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        Write-Success "[OK] $($file.Name) ($size bytes)"
    } elseif ($file.Required) {
        Write-Err "[MISSING] $($file.Name)"
    } else {
        Write-Warn "[OPTIONAL] $($file.Name) not found"
    }
}

# Copy config
if (Test-Path "config\GTA5VR.ini") {
    Copy-Item "config\GTA5VR.ini" $outputDir -Force
    Write-Success "[OK] Copied GTA5VR.ini"
}

Write-Host ""
Write-Host "Output directory: $outputDir"

# Package if requested
if ($Package) {
    Write-Host ""
    Write-Info "Creating release package..."

    $version = "1.0.0"
    $packageName = "GTA5VR_v$version"
    $packageDir = "release\$packageName"

    if (Test-Path "release") {
        Remove-Item -Recurse -Force "release"
    }
    New-Item -ItemType Directory -Path $packageDir | Out-Null

    # Copy files
    $filesToPackage = @(
        "build\bin\$BuildType\GTA5VR.dll",
        "build\bin\$BuildType\GTA5VR_Injector.exe",
        "config\GTA5VR.ini",
        "README.md"
    )

    foreach ($file in $filesToPackage) {
        if (Test-Path $file) {
            Copy-Item $file $packageDir
        }
    }

    # Copy docs
    if (Test-Path "docs") {
        Copy-Item -Recurse "docs" "$packageDir\docs"
    }

    # Create ZIP
    $zipPath = "release\$packageName.zip"
    Compress-Archive -Path $packageDir -DestinationPath $zipPath -Force

    Write-Success "[OK] Created $zipPath"
    Write-Host "Package size: $((Get-Item $zipPath).Length / 1MB) MB"
}

Write-Host ""
Write-Success "Done!"
Write-Host ""
