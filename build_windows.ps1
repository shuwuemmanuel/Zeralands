# Builds ZeraLands with Visual Studio 2022 and packages it into dist\ZeraLands
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config $Config --parallel
$dist = "dist\ZeraLands"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item "build\$Config\ZeraLands.exe" $dist
Copy-Item "build\$Config\zeralands_cli.exe" $dist
Copy-Item -Recurse assets "$dist\assets"
Copy-Item README.md $dist
Copy-Item -Recurse docs "$dist\docs"
Write-Host "Packaged to $dist"
