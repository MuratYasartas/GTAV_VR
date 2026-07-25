# GTAVR log-bundle collector. Gathers everything needed to diagnose a
# problem into one timestamped folder on the Desktop. Read-only.
$ErrorActionPreference = 'SilentlyContinue'

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$dest = Join-Path ([Environment]::GetFolderPath('Desktop')) "GTAVR_LogBundle_$stamp"
New-Item -ItemType Directory -Path $dest -Force | Out-Null

$repo = 'C:\Repos\GTA_VR\GTAV_VR'
$candidateDirs = @($env:GTAVR_LOG_DIR, $env:GTAV_INSTALL_DIR, $env:TEMP, $repo)
$cfg = Join-Path $env:USERPROFILE 'gtavr_play.ini'
if (Test-Path $cfg) { $candidateDirs += (Get-Content $cfg | Select-Object -First 1) }
$candidateDirs = $candidateDirs | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

$logFiles = @('gtavrInjectLog.txt', 'gtavrInjectShimLog.txt', 'gtavr_perf.csv')
$copied = @()
foreach ($dir in $candidateDirs) {
    foreach ($f in $logFiles) {
        $src = Join-Path $dir $f
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $dest "$([IO.Path]::GetFileName($dir)).$f") -Force
            $copied += $src
        }
    }
    Get-ChildItem (Join-Path $dir '*.dmp') | ForEach-Object {
        Copy-Item $_.FullName $dest -Force; $copied += $_.FullName
    }
}

# Config + manifest snapshots (repo and game dir)
foreach ($dir in @($repo) + $candidateDirs) {
    foreach ($f in @('gtavr_settings.ini', 'gtavr_camera.ini')) {
        $src = Join-Path $dir $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $dest "$([IO.Path]::GetFileName($dir)).$f") -Force }
    }
    $mani = Join-Path $dir 'manifests\gtav_legacy.ini'
    if (Test-Path $mani) { Copy-Item $mani (Join-Path $dest "$([IO.Path]::GetFileName($dir)).gtav_legacy.ini") -Force }
}

# System info
$gpu = Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) (driver $($_.DriverVersion))" }
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
@"
GTAVR diagnostic bundle $stamp
OS:  $($os.Caption) $($os.Version)
CPU: $($cpu.Name)
RAM: $([math]::Round($os.TotalVisibleMemorySize/1MB,1)) GB
GPU: $($gpu -join '; ')
Verbose logging: $(if ($env:GTAVR_VERBOSE) { "GTAVR_VERBOSE=$env:GTAVR_VERBOSE" } else { 'off' })
Files collected from: $($candidateDirs -join '; ')
Logs copied: $($copied -join '; ')
"@ | Out-File (Join-Path $dest 'sysinfo.txt') -Encoding utf8

Write-Host ""
Write-Host "Bundle created: $dest"
Write-Host "Share this folder when reporting a problem (it contains no game files)."
