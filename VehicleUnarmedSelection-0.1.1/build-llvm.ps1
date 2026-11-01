param([Parameter(Mandatory=$true)][string]$Toolchain)
$ErrorActionPreference='Stop'
$compiler=Join-Path $Toolchain 'bin/i686-w64-mingw32-clang++.exe'
$destination=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $destination | Out-Null
& $compiler -std=c++17 -O2 -Wall -Wextra -Werror -shared -static (Join-Path $PSScriptRoot 'src/VehicleUnarmedSelection.cpp') -lversion -o (Join-Path $destination 'VehicleUnarmedSelection.asi')
if($LASTEXITCODE -ne 0){throw 'Build failed'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'VehicleUnarmedSelection.ini') -Destination $destination
Get-FileHash (Join-Path $destination 'VehicleUnarmedSelection.asi')
