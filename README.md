<!--
# SPDX-FileCopyrightText: Copyright 2026 atlonxp (Eden Extended changes)
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
-->

<h1 align="center">
  <br>
  <img src="./dist/branding/eden-extended-icon-1024.png" alt="Eden Extended" width="160">
  <br>
  <b>Eden Extended</b>
  <br>
</h1>

<h4 align="center">A community fork of the <a href="https://git.eden-emu.dev/eden-emu/eden">Eden</a> Nintendo Switch emulator with extra rendering, compatibility and performance fixes, focused on Android devices with Adreno GPUs.</h4>

<p align="center">Not affiliated with, or endorsed by, the Eden team. Eden Extended installs next to stock Eden and never touches its data.</p>

## What this repo is

Eden Extended (`edenex`) is Eden's `master` branch plus a small set of patches that stock Eden does not have yet. The goal is narrow: take games that render wrongly, stall, or crash the GPU on stock Eden, find the actual cause in the emulator, and fix it there instead of papering over it with per-game hacks. Everything else, from the UI to the settings layout, is unchanged upstream Eden.

The first target was **Momotaro Dentetsu: Showa, Heisei, Reiwa mo Teiban! Asia Edition** (`010021801DD26000`), which ran at 0 FPS on Eden 0.2.1 and, once that was fixed, still showed flickering sea, garbage streaks, black tiles, periodic 100 ms freezes and GPU hangs. All of those are gone in this fork. The fixes are generic, so other titles that hit the same emulator paths benefit too.

- Upstream base: Eden `master` at commit `bebc19d`.
- Test hardware: AYN Thor (Snapdragon 8 Gen 2 class, Adreno 740, Android 13) with the Turnip `mrpurple T23` driver. The core changes are platform independent but have only been tested on Android so far.
- Android package: `dev.edenex.eden_emulator` (release) and `dev.edenex.eden_emulator.relWithDebInfo` (debuggable), so it coexists with `dev.eden.eden_emulator`.

## What we improved

### Rendering: `VOTE.VTG` and hardware vertex culling (shader recompiler)

NVIDIA's shader compiler ends many vertex shaders with a clip-test epilogue that uses the Maxwell `VOTE.VTG` instruction and the `SR_WSCALEFACTOR_XY` / `SR_WSCALEFACTOR_Z` system registers. The yuzu-family recompiler never decoded that epilogue, so games that rely on it (Momotaro Dentetsu is one) rendered with streaks across the map, black tiles, brown-green sea and wrong depth ordering.

This fork adds:

- A proper decoder for `VOTE.VTG` (`src/shader_recompiler/frontend/maxwell/translate/impl/vote.cpp`). It shares the operand layout of `VOTE` but only produces a predicate; the low byte is not a ballot register, and writing it as one was corrupting the vertex Z coordinate.
- `SR_WSCALEFACTOR_XY` and `SR_WSCALEFACTOR_Z` now return `1.0` instead of an arbitrary bit pattern (`emit_spirv_context_get_set.cpp`).
- A new IR pass, `VtgCullPass` (`src/shader_recompiler/ir_opt/vtg_cull_pass.cpp`), that runs when the epilogue was seen. It reproduces what the hardware does after the vote: a vertex whose position has `w <= 0` or a NaN component is culled by rewriting its position to `(0, 0, 0, -1)`, so the primitive is clipped instead of being drawn with garbage coordinates.

### Stutter: no redundant guest uploads in the texture cache (video core)

When a game copies into an image on the GPU and then binds the same memory as a new image, the texture cache re-uploaded the whole image from guest memory even though the GPU copy was the newer data. In Momotaro Dentetsu this happened every time the sea came into view and cost about 100 ms per frame.

`TextureCache::JoinImages` (`src/video_core/texture_cache/texture_cache.h`) now detects an overlapping image that is GPU-modified and not CPU-modified, covers the new image at the same address and has a single level, and in that case tracks the new image without refreshing it from guest memory.

### GPU hangs on Adreno: MSAA image compression control (Vulkan)

On Turnip, multisampled render targets are UBWC-compressed by default. In this game that combination triggered kernel-level GPU hangs (`adreno-gen7-gmu: GPU hang detected`) every few minutes, which showed up as the game freezing and then closing to the emulator. Disabling UBWC globally stops the hangs but costs a lot of bandwidth.

The fork enables `VK_EXT_image_compression_control` when the driver exposes it and creates multisampled images with compression disabled on Turnip and Qualcomm drivers only (`vk_texture_cache.cpp`, `vulkan_device.h`). Single-sampled images keep their compression, so the bandwidth cost stays small.

### Debugging aids

With **Log shader dumps** enabled in the GPU settings, the pipeline cache now writes both the guest Maxwell shader binaries and the translated IR (`<dump dir>/shaders_ir/<pipeline>_<hash>_s<index>.ir`) for every pipeline it builds. That is how the `VOTE.VTG` problem was found, and it makes the next rendering bug much cheaper to diagnose.

### Android

- Eden Extended identity: new package id, app name, adaptive launcher icon with a monochrome layer for Android 13 themed icons. Sources are in `dist/branding/`.
- The custom-config launch action follows the package id (`${applicationId}.LAUNCH_WITH_CUSTOM_CONFIG`) so shortcuts keep working in every build variant.
- The main activity declares a `PROCESS_TEXT` intent filter. Under Android 11+ package-visibility rules this lets overlay tools such as screen translators see the emulator without a broad `QUERY_ALL_PACKAGES` permission.

### Reported upstream

- Eden 0.2.1 ran Momotaro Dentetsu at 0 FPS because the HID Npad LIFO seeded empty entries from a shifted sampling number, so the game never accepted a controller state. Reported as [eden-emulator/Issue-Reports#671](https://github.com/eden-emulator/Issue-Reports/issues/671); upstream `master` already contains the fix, so this fork inherits it.

## Installing on Android

1. Install the APK from the Releases page, or build it yourself (see below). Stock Eden can stay installed; the two apps use separate data folders under `Android/data/`.
2. On first launch, add your game folder and keys as you would in Eden. Storage permissions are per app, so they have to be granted again even if stock Eden already has them.
3. Saves live in `Android/data/dev.edenex.eden_emulator/files/nand/user/save`. They are not shared with stock Eden; copy them over if you want to continue an existing game.

The debuggable `relWithDebInfo` build (`dev.edenex.eden_emulator.relWithDebInfo`) is the one used for development. It is slightly slower than a release build and can be inspected with `adb shell run-as`.

## Building

The build system is unchanged from upstream, so the [Eden build instructions](https://git.eden-emu.dev/eden-emu/eden) apply. For Android:

```sh
# JDK 17, Android NDK 28.2.13676358, SDK CMake 3.31.6, plus pkg-config, nasm, autoconf and glslang on the host
cd src/android
./gradlew assembleMainlineRelease          # release APK
./gradlew assembleMainlineRelWithDebInfo   # debuggable APK, separate package id
```

The APK ends up in `src/android/app/build/outputs/apk/mainline/`.

## Contributing and upstreaming

Bug reports and patches for this fork are welcome here. Please note that the Eden project does not accept contributions or reports produced with AI assistance, so anything you intend to send upstream has to follow their rules, not ours.

## License

Eden Extended is licensed under the GNU General Public License v3.0 or later, the same as Eden. See [LICENSE.txt](LICENSE.txt) and the `LICENSES/` folder for the licenses of bundled third-party code.

Copyright for the emulator belongs to the Eden Emulator Project and the yuzu Emulator Project. The changes specific to this fork are copyright 2026 atlonxp. Eden Extended does not include any Nintendo software, keys or firmware; you need a Switch you own to obtain them.
