# Hyrule Evil 4 — runtime and installer source

Source code of the compiled and scripted parts of **Hyrule Evil 4**, a free, non-profit fan mod for
Resident Evil 4 UHD (Steam) by Donixar: https://www.nexusmods.com/residentevil4/mods/954

This repository contains only original code. It holds **no game assets**: no models, textures, audio,
cutscenes or files from Capcom, Nintendo or Koei Tecmo.

## What is here

| Path | What it is |
|---|---|
| `dll/` | The runtime shipped as `Bin32/dinput8.dll`: a DirectInput8 proxy (`link_runtime.def` forwards `DirectInput8Create` to the system `dinput8.dll`) that adds Link's sword actions, parry, Z-targeting, HUD and sound hooks. Hooks use [MinHook 1.3.4](https://github.com/TsudaKageyu/minhook) (downloaded and checksum-pinned at build time by `scripts/fetch_minhook.py`). |
| `release-2.0-rc1/link_runtime-public.ini` | The settings file shipped next to the DLL. |
| `scripts/build_link_runtime.bat` | Builds the DLL and runs its native tests. |
| `tests/native/` | Native tests the build runs (core, health, damage, trace, log, DirectInput proxy forwarding). |
| `scripts/package/` | The installer, uninstaller, updater and diagnostics shipped in the mod ZIPs (`Install-LinkInRE4.bat` → `install.ps1`, etc.). They only copy the mod's files into the game folder, back up the originals and restore them on uninstall. |

## Building `dinput8.dll`

Requirements (Windows):
- Visual Studio 2022 Build Tools with the C++ workload (x86 toolset) and Windows 10 SDK 10.0.22621.0.
- Python 3.10+ (only to download and verify MinHook).

```bat
rem optional if Build Tools are not in the default location:
set VCVARS32=C:\path\to\VC\Auxiliary\Build\vcvars32.bat
scripts\build_link_runtime.bat build
```

The script downloads MinHook 1.3.4 (SHA-256 pinned), compiles it, builds and runs the native tests, compiles
`dll/link_runtime.cpp` with `/std:c++17 /W4 /WX /O2 /MT` into `build\dinput8.dll` with its version resource
(product "Hyrule Evil 4", version from `dll/link_runtime_version.h`), and finally checks that the proxy loads and
forwards `DirectInput8Create`. It prints `READY: build\dinput8.dll` on success. It never installs anything or starts
the game. Timestamps and build paths are embedded, so a rebuild is not byte-identical to the shipped file.

## Version

This source corresponds to Hyrule Evil 4 **3.2.0** (shipped `dinput8.dll` SHA-256
`4eae540a598aac4d6a2d30c6411b19199d4b90b97fd757878fd4734a98eea7a6`); later commits only changed source comments.

## Disclaimer

Resident Evil 4 belongs to Capcom; The Legend of Zelda belongs to Nintendo. This project is not affiliated with or
endorsed by them. You need your own copy of Resident Evil 4 on Steam to use the mod.
