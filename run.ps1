# Compile sync_completion.c with the C toolchain GHC ships with, then run it.
#   .\run.ps1 original | fixed            (-Ghc ghc-9.12.4 to pick a particular GHC)
param(
  [Parameter(Mandatory)][ValidateSet("original", "fixed")][string]$Variant,
  [string]$Ghc = "ghc"
)
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
  & $Ghc -v0 -no-hs-main -O1 sync_completion.c -lws2_32 -o sync_completion.exe
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  .\sync_completion.exe $Variant
} finally { Pop-Location }
