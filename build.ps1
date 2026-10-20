<#
.SYNOPSIS
    Builds RDSM (Remote Disk Space Monitor).

.DESCRIPTION
    A one-stop build script that stops any running RDSM.exe, configures the
    project with CMake (if needed), builds it, and runs the unit tests.

    This is a convenience wrapper around the manual CMake commands documented
    in docs/BUILDING.md. Run it from the project root (or anywhere; it locates
    the project from its own path).

.PARAMETER Config
    The build configuration. Default: Release.

.PARAMETER NoTests
    Skip running the unit tests after the build.

.PARAMETER Clean
    Delete the build directory before configuring (a from-scratch build).

.PARAMETER Exe
    Launch the built RDSM.exe after a successful build.

.EXAMPLE
    .\build.ps1
    Configure (if needed), build Release, and run the unit tests.

.EXAMPLE
    .\build.ps1 -Clean -Exe
    Remove the build directory, build from scratch, and launch the app.

.EXAMPLE
    .\build.ps1 -Config Debug -NoTests
    Build the Debug configuration without running the unit tests.
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "MinSizeRel", "RelWithDebInfo")]
    [string]$Config = "Release",

    [switch]$NoTests,
    [switch]$Clean,
    [switch]$Exe
)

$ErrorActionPreference = "Stop"

$root     = $PSScriptRoot
$buildDir = Join-Path $root "build"

if ($env:OS -ne "Windows_NT") {
    throw "RDSM is a Windows-only project; run this script on Windows."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake (or the Visual Studio CMake tools) and retry."
}

function Invoke-Step {
    param([string]$Message, [scriptblock]$Block)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
    & $Block
}

# --- Stop a running RDSM.exe so the linker can overwrite the output ----------------------
Get-Process -Name "RDSM" -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "Stopping running RDSM.exe (PID $($_.Id)) ..."
    Stop-Process -Id $_.Id -Force
}

# --- Optional clean -----------------------------------------------------------------------
if ($Clean) {
    if (Test-Path $buildDir) {
        Write-Host "Removing $buildDir ..."
        Remove-Item -Recurse -Force $buildDir
    }
}

# --- Configure (idempotent: CMake reuses an existing cache) -------------------------------
Invoke-Step "Configuring ($Config)" {
    cmake -S $root -B $buildDir -A x64
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "CMake configure failed (exit $code)." }
}

# --- Build --------------------------------------------------------------------------------
Invoke-Step "Building ($Config)" {
    cmake --build $buildDir --config $Config
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "Build failed (exit $code)." }
}

# --- Tests --------------------------------------------------------------------------------
if (-not $NoTests) {
    Invoke-Step "Running unit tests" {
        ctest --test-dir $buildDir -C $Config --output-on-failure
        $code = $LASTEXITCODE
        if ($code -ne 0) { throw "Unit tests failed (exit $code)." }
    }
}

# --- Result -------------------------------------------------------------------------------
$exePath = Join-Path $buildDir "bin\$Config\RDSM.exe"
Write-Host ""
Write-Host "Build succeeded: $exePath" -ForegroundColor Green

if ($Exe) {
    Write-Host "Launching RDSM.exe ..."
    Start-Process -FilePath $exePath
}
