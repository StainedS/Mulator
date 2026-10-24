# M-ulator

A handheld game system emulator for Android, written from scratch in C++17 (core) and Kotlin (app).

## Layout

```
core/            Platform-independent emulator (no Android or OS code)
  include/mulator/machine.h  Public API: loadRom, runFrame, framebuffer, audio, saves, states
  src/cpu.cpp             ARM7TDMI interpreter (ARM + Thumb, 2-stage prefetch)
  src/bus.cpp             Memory map, I/O registers, wait states
  src/ppu.cpp             Scanline renderer: modes 0-5, sprites, affine, windows, blending, mosaic
  src/apu.cpp             4 PSG channels + 2 Direct Sound FIFOs
  src/timers_dma.cpp      Timers (cascade) and 4 DMA channels
  src/cart.cpp            ROM + SRAM / Flash 64K / Flash 128K / EEPROM saves + GPIO real-time clock
  src/bios_hle.cpp        Built-in BIOS replacement (no BIOS dump needed)
  src/system.cpp          Frame scheduler, interrupts, save states
android/         Android Studio project (Kotlin UI + JNI bridge)
  app/src/main/cpp/jni_bridge.cpp   Emulation thread, ANativeWindow video, AAudio sound, rewind buffer
  app/src/main/java/com/mulator/app/
    data/        Library (folder scan, play stats), Settings, SaveSlots (state files + thumbnails)
    ui/          Jetpack Compose screens: library, settings, in-game pause menu
    EmulatorActivity.kt / ControllerView.kt   Game screen, touch pad, gamepad input
desktop/         PC tools for testing the core
  cli.cpp         Headless runner: screenshots, input scripts, save-state check
  win32_main.cpp  Playable Windows window (keyboard + sound)
testroms/        Local test programs (not in the repo, see "Testing" below)
```

## Build the Android app

Open the `android/` folder in Android Studio and press Run, or from a terminal:

```
cd android
gradlew assembleRelease      (or assembleDebug)
```

APK: `android/app/build/outputs/apk/release/app-release.apk` (shrunk with R8, ~2.5 MB).
Release builds use the key described in `android/keystore.properties` if that file exists
(see the comment in `app/build.gradle.kts`), otherwise the debug key so they can be sideloaded.
For Google Play, build a bundle with `gradlew bundleRelease` using your own upload key.

Requires: Android SDK 35, NDK 26.1.10909125, CMake 3.22.1, JDK 17.

## Build the desktop tools (Windows)

```
build_desktop.bat
build\desktop\desktop\mulator_win.exe  <game file>      (play)
build\desktop\desktop\mulator_cli.exe  <game file> --frames 600 --png shot.png --press START@100-105
```

Desktop keys: arrows = D-pad, Z = A, X = B, Enter = Start, Backspace = Select, A/S = L/R,
hold Tab = fast forward, F5/F7 = save/load state.

## Android app

- Library: add a games folder once (scanned recursively, rescan with pull-to-refresh) or open
  single files. Cover art is the latest save-state screenshot. Favorites, search, recently played,
  play-time tracking, home-screen shortcuts, and recent games in the launcher long-press menu.
- Resume where you left off: an automatic save state is written whenever a game is left, and
  restored on the next launch ("Restart" in the notice boots from scratch).
- Pause menu (☰, Back, or the controller's Home button): 4 save slots with thumbnails, fast
  forward, reset, quit.
- « hold to rewind (about 30 s of history), » toggles fast forward (2x/3x/4x/8x/unlimited).
- Gamepads: L1/R1 = L/R, hold L2 = rewind, hold R2 = fast forward, A/B/Select close the menu.
- Touch controls: six button styles (Classic, Minimal, Console, Neon, Retro, Pastel) picked from
  live previews in Settings, and a layout editor (Settings > Customize layout, or the pause menu's
  controls icon) to drag and pinch-resize every button. Portrait and landscape layouts are saved
  separately (`ControllerLayouts`, stored as fractions of the screen so they fit any phone).
- Settings: resume, rewind, fast-forward speed, haptics, button opacity and size, wallpaper colors.

Battery saves and states live in the app's private storage (`files/saves`, `files/states/<game>/`).

## Status

Passes jsmolka's open-source CPU/memory/save test suite (arm, thumb, memory, bios, nes, unsafe,
sram, flash64, flash128) and
renders all Tonc tutorial demos correctly. A large 32 MB game (Flash 128K save,
real-time clock) boots and plays through its intro
into the overworld.

Not done yet: link cable, cycle-exact prefetch and
open-bus edge cases, shaders, cheats, controller remapping.

## Legal

Do not ship game files or any console maker's BIOS or system software with the app. Users
must supply their own game files. Keep console makers' names, logos and game artwork out of
the app name, icon, store listing and screenshots (use homebrew or the test ROMs for screenshots).

## Testing

Test programs are kept out of the repository. To run the core against them, put open-source
homebrew test binaries (for example jsmolka's test suite and the Tonc tutorial demos, both
freely published by their authors) in `testroms/` locally. That folder is ignored by git.
