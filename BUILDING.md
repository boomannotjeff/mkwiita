# Building Mario Kart Wii for PS Vita

>[!IMPORTANT]
>For MacOS and Linux please refer to [BUILDING-MAC-LINUX.md](BUILDING-MAC-LINUX.md)

The build makes two things:
- **`build/vita/mkw.vpk`:** the program.
- **`build/vita/data/mkw/`:** the game files, converted from your own disc.

## 1. Install the tools (once)

| Tool | Notes |
|---|---|
| [VitaSDK](https://vitasdk.org) | Only to build the VPK (not needed for the game files alone). Set the `VITASDK` environment variable to its folder, e.g. `C:\vitasdk`. On Windows, extract the `vitasdk-x86_64-w64-mingw32` snapshot from [vitasdk/autobuilds](https://github.com/vitasdk/autobuilds/releases) to `C:\vitasdk`, then run `vdpm refresh nightly` once. |
| [Python 3.11+](https://www.python.org/downloads/) | Tick "Add python.exe to PATH" |
| [Node.js LTS](https://nodejs.org) | Used by the model exporters |
| [Git](https://git-scm.com) | |

The build fetches the rest itself:
- vitaGL and its dependencies, through VitaSDK's `vdpm`
- CMake and Ninja, through pip
- `tex3ds`, devkitPro's texture converter, into `.toolchain/`
- the pinned sources (Kinoko, citro3d, libctru, noclip, gclib) into `vendor/`

## 2. Build

**Windows:** double-click `Build MKW Vita.bat`, drag in your Mario Kart Wii
(USA) `.iso` or `.wbfs`, and press Enter. Output is also saved to
`build-vita-log.txt`.

**Only the game files** (for the VPK from the
[releases](https://github.com/boomannotjeff/mkwiita/releases/latest), no
VitaSDK): double-click `Build Game Files.bat` instead, or run
`tools/vita/build.py --assets-only <disc>`. Output is saved to
`build-assets-log.txt`.

**Any OS, from a terminal:**

```sh
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt    # .venv/bin/python elsewhere
.venv/Scripts/python tools/vita/build.py "Mario Kart Wii (USA).wbfs"
```

**First build:**
- It takes 30–60 minutes.
- Every step is skipped once done, so rerun the same command to resume after a failure.
- After changing only code, `tools/vita/build.py --skip-assets` rebuilds just the VPK in a minute or two.

**What the steps do:**
1. **Disc check:** unpacks the disc and checks every file against the clean USA disc (`tools/release/usa-disc.json`).
2. **Cook:** 43 steps that convert courses, characters, vehicles, menus, movies and sound (`tools/release/build_cia.py` `COOK_STEPS`).
3. **Package:** stages the files into `build/vita/data/mkw` and makes the LiveArea art from the disc's title screen.
4. **Compile:** builds the game with CMake (`CMakeLists.txt`) and packs `mkw.vpk`.

## 3. Install

1. Install `build/vita/mkw.vpk` with VitaShell.
2. Copy the contents of `build/vita/data/mkw/` to `ux0:data/mkw/`.
3. Make sure `ur0:data/libshacccg.suprx` exists. If it doesn't, run [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D) once.

## The Retro Rewind edition

The same source builds **MKW Retro Rewind**, a separate app (title ID `MKWR00001`, game files in
`ux0:data/mkwrr`) made from your disc and your own Retro Rewind 6 install (the `RetroRewind6` folder).

**Build it in its own folder.** Clone or unzip the source a second time. Both editions write to the
same places in a folder (`data/`, `assets/`, `sd-card/`, `build/`), so a folder remembers which
edition it built (`build/edition.txt`) and refuses to build the other one.

**Windows:** double-click `Build MKW Retro Rewind Vita.bat` (VPK and game files, needs VitaSDK) or
`Build Retro Rewind Game Files.bat` (game files only, for `mkw-retro-rewind.vpk` from the releases).
Each asks for the disc, then the Retro Rewind folder; press Enter there to use Dolphin's
(`%APPDATA%\Dolphin Emulator\Load\Riivolution\WheelWizard\RetroRewind6` or `...\Riivolution\RetroRewind6`).
Logs: `build-rr-vita-log.txt` and `build-rr-assets-log.txt`.

**Any OS, from a terminal:**

```sh
.venv/Scripts/python tools/vita/build.py "Mario Kart Wii (USA).wbfs" --rr "path/to/RetroRewind6"
```

It makes `build/vita/mkw.vpk` (the Retro Rewind app) and `build/vita/data/mkwrr/`. The first build
takes a few hours and about 45 GB. Tracks or skins that don't convert are left out and show
"Not available"; the build log lists them.

## Build options

| | |
|---|---|
| `tools/vita/build.py --release` | No test hooks or `runtime.log` |
| `cmake -G Ninja -B build/vita && cmake --build build/vita` | The VPK only (VITASDK set, sources bootstrapped) |
| `tools/vita/build.py --assets-only <disc>` | The game files only, no VitaSDK needed (what `Build Game Files.bat` runs) |
| `tools/vita/build.py --mipmaps --force-cook <disc>` | Experimental: texture mipmaps for the textures that had them on the Wii |
| `tools/vita/build.py <disc> --rr <RetroRewind6 folder>` | The Retro Rewind edition (see above) |
| `--rr-cups RT:0` / `--rr-skins mr,lg` | Retro Rewind: cook only some cups (`RT:0`, `RT:0-3,CT:1`) or drivers' skins, for a quick test; the default is `all` |

## Debug switches

Create these files (they can be empty) to change behaviour at start-up:

| File | Effect |
|---|---|
| `ux0:data/mkw/gpu-sync.txt` | Wait for the GPU every frame, as the 3DS does. Slower; use it to compare when something flickers. |
| `ux0:data/mkw/gpu-trace.txt` | Log every draw's state in a few frames to `vita.log` |
| `ux0:data/mkw/no-shader-cache.txt` | Always compile shaders (ignore `ux0:data/mkw/shaders/`) |
