# Building 0.1.1

Windows x86, C++17. No game files, DirectX SDK or IV-SDK checkout are needed to compile.

## Verified toolchain

Official [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases) package `llvm-mingw-20261020-ucrt-x86_64`, clang 23.1.4. The i686 compiler produces the 32-bit ASI even though the compiler host is x64.

```powershell
./build-llvm.ps1 -Toolchain 'C:\tools\llvm-mingw-20261020-ucrt-x86_64'
```

Output: `bin/VehicleUnarmedSelection.asi` and INI. Uses warnings as errors, optimization and static C++ linkage; Windows/UCRT remain runtime dependencies.

## Alternative build (not executed here)

Visual Studio 2022 Desktop development with C++, Windows SDK, and CMake:

```powershell
cmake -S . -B build -G 'Visual Studio 17 2022' -A Win32
cmake --build build --config Release
```

Output: `build/Release/VehicleUnarmedSelection.asi`.

## Verification

Install `tests/requirements.txt` in a Python virtual environment. Compile `tests/dump-stubs.cpp` with the i686 compiler and run the executable in a scratch directory. It emits four stub files from the same HookCode.h used by the ASI.

```powershell
& 'C:\tools\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang++.exe' -std=c++17 -static tests/dump-stubs.cpp -o dump-stubs.exe
./dump-stubs.exe
python tests/verify.py --exe 'C:\Games\GTAIV\GTAIV.exe' --asi bin/VehicleUnarmedSelection.asi --stubs .
```

The test reads the EXE without modifying it. It checks signatures, PE architecture and imports, emulates the emitted hooks, and emulates the game's selector with external inventory/ammo helpers mocked. This is not full-game testing. See VERIFICATION.txt for the executed results.



