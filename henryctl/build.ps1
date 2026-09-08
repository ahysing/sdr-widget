<#
.SYNOPSIS
    Builds henryctl.exe on Windows PowerShell using MSVC (cl) and statically-linked libusb from vcpkg.

.DESCRIPTION
    Assumes `cl` is in PATH and `vcpkg` has installed `libusb:x64-windows-static`.
    Prerequisite:
        vcpkg install libusb:x64-windows-static

.EXAMPLE
    .\build.ps1
    .\build.ps1 -VcpkgDir C:\Users\<username>\code\vcpkg
#>

[CmdletBinding()]
param(
    [string]$VcpkgDir = ""
)

$ErrorActionPreference = "Stop"

# 1. Determine vcpkg directory
if (-not $VcpkgDir) {
    if (Get-Command vcpkg -ErrorAction SilentlyContinue) {
        $vcpkgCmd = Get-Command vcpkg
        $candidateDir = Split-Path $vcpkgCmd.Source -Parent
        if (Test-Path "$candidateDir\installed\x64-windows-static") {
            $VcpkgDir = $candidateDir
        }
    }
    if (-not $VcpkgDir -and $env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\installed\x64-windows-static")) {
        $VcpkgDir = $env:VCPKG_ROOT
    }
    if (-not $VcpkgDir -and (Test-Path "..\vcpkg\installed\x64-windows-static")) {
        $VcpkgDir = "..\vcpkg"
    }
    if (-not $VcpkgDir -and $env:VCPKG_INSTALLATION_ROOT -and (Test-Path "$env:VCPKG_INSTALLATION_ROOT\installed\x64-windows-static")) {
        $VcpkgDir = $env:VCPKG_INSTALLATION_ROOT
    }
}

$VcpkgInstalled = "$VcpkgDir\installed\x64-windows-static"
$LibusbInc = "$VcpkgInstalled\include"
$LibusbLib = "$VcpkgInstalled\lib"
$LibusbHeader = "$LibusbInc\libusb-1.0\libusb.h"
$LibusbLibFile = "$LibusbLib\libusb-1.0.lib"

if (-not (Test-Path $LibusbHeader) -or -not (Test-Path $LibusbLibFile)) {
    Write-Error "Static libusb not found in '$VcpkgInstalled'.`nPlease run: vcpkg install libusb:x64-windows-static"
}

# 2. Ensure cl is available
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Error "MSVC compiler 'cl' not found in PATH. Please run in a Developer Command Prompt or initialize Visual Studio environment via vcvars64.bat."
}

# 3. Compile henryctl.exe
$scriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
Push-Location $scriptDir
try {
    Write-Host "Building henryctl.exe with static libusb..." -ForegroundColor Cyan
    $compileArgs = @(
        "/nologo",
        "/O2",
        "/I.",
        "/I..",
        "/I..\src",
        "/I$LibusbInc",
        "henryctl.c",
        "/Fe:henryctl.exe",
        "/link",
        "/LIBPATH:$LibusbLib",
        "libusb-1.0.lib"
    )
    & cl @compileArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation failed with exit code $LASTEXITCODE"
    }
    Write-Host "Successfully built henryctl.exe (static libusb)" -ForegroundColor Green
} finally {
    Pop-Location
}
