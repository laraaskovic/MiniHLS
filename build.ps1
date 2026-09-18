<#
.SYNOPSIS
  Build and test MiniHLS from Windows by delegating to WSL.

.DESCRIPTION
  The toolchain this project needs (g++, CMake, Verilator, Yosys) is installed in
  WSL, not on Windows, so every build runs there. The repository itself stays on
  the Windows filesystem and is reached through /mnt/c.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -BuildType Debug
#>
[CmdletBinding()]
param(
  [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
  [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
  throw 'wsl was not found. This project builds inside WSL; install it with "wsl --install".'
}

$repoWsl = (& wsl wslpath -a ($PSScriptRoot -replace '\\', '/')).Trim()
if (-not $repoWsl) { throw "Could not map $PSScriptRoot into WSL." }

Write-Host "Building in WSL at $repoWsl ($BuildType)" -ForegroundColor Cyan

& wsl -e bash -lc "cd '$repoWsl' && BUILD_TYPE='$BuildType' bash scripts/build_and_test.sh"
exit $LASTEXITCODE
