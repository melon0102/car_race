# Re-Volt — Android Port

Port of the original Re-Volt PC codebase (1999) to Android 11–13
(minSdk 30 / targetSdk 33), Java app shell + native C++ game core.

## Layout

```
app/src/main/
  java/com/revolt/game/MainActivity.java   Java shell (GameActivity subclass)
  cpp/
    CMakeLists.txt          native build; game files enabled incrementally
    main/
      android_main.cpp      entry point: lifecycle, EGL/GLES3 context, frame loop
      game_bridge.cpp       stub globals + C entry points into the game core
    platform/
      winshim/              fake <windows.h>, <d3d.h>, <ddraw.h>, ... headers
      winshim_impl.cpp      timing -> clock_gettime, MessageBox -> logcat, etc.
    game/                   UNMODIFIED copy of rvsource/source (PC codebase)
      inc/                  original game headers
```

## Building

1. Open `RevoltAndroid/` in Android Studio (Hedgehog or newer).
   It will fetch Gradle 8.2 / AGP 8.2.2 and prompt to install
   NDK 26.1.10909125 + CMake 3.22.1 if missing (or install via SDK Manager).
2. Build & run on an Android 11+ device/emulator (arm64 or armv7).
3. Success looks like: a pulsing orange screen, and in logcat
   (`adb logcat -s revolt-main revolt-core revolt-shim`):
   `GL ready: ...` and `Re-Volt core self-test: PASS` — the PASS line means
   original game code (Geom.cpp matrix math) compiled, linked and ran.

## Porting workflow (the rules)

- **Never edit `cpp/game/` to fix Win32/compile issues** — extend the shim
  headers in `cpp/platform/winshim/` instead. Editing game code is reserved
  for real porting work (64-bit fixes, replacing subsystems), so upstream
  diffs stay reviewable.
- To bring a new game file in: uncomment it in `CMakeLists.txt`, build, then
  - missing type/constant -> add to the matching winshim header;
  - undefined symbol from a *replaced* module -> add a stub to
    `game_bridge.cpp` (and remove it later when the real module is enabled);
  - undefined symbol from a *portable* module -> enable that file too.
- Modules that are **never** enabled (replaced by the platform layer):
  `main.cpp` (→ android_main.cpp), `dx.cpp`/`Dxerrors.cpp` (→ GLES3 renderer),
  `registry.cpp` (→ config file), `Input.cpp` (→ Android input),
  `sfx.cpp` (→ Oboe mixer), `play.cpp` (DirectPlay networking, later rebuilt).

## Phase plan

- [x] **0 — Scaffold**: project, shim layer, GL heartbeat, core self-test link
- [x] **1 — Core compiles**: 59 of 64 game modules building on NDK clang
      (remaining 5 are replaced platform modules)
- [x] **2 — Renderer**: GLES3 behind IDirect3DDevice3 (TL-vertex pipeline,
      RHW perspective restore, state mapping, texture upload from shim
      surfaces); mini-GDI BMP loader; boots straight into a time trial
- [x] **3 — Audio/input/assets** (first pass): AAudio 33-voice mixer behind
      the Miles API; touch zones + gamepad + keyboard into Keys[]; retail
      data pack staged lowercased into APK assets, unpacked on first run
- [ ] **RUNTIME BRING-UP**: first on-device run and crash-fixing pass —
      everything above is compile-verified but has never executed
- [ ] **4 — Android UI**: Java frontend menus (car/track select, settings),
      JNI bridge, audio focus, tilt steering option
- [ ] **5 — Features & polish**: music tracks, replays balance, 64-bit
      `long` sweep for file formats (armv7 works today; arm64 loaders need
      the sweep), sockets multiplayer

## Touch controls (bring-up layout, landscape)

- bottom-left: steer left (`x<0.18`) / steer right (`0.18–0.36`)
- bottom-right: accelerate (`x>0.82`) / brake-reverse (`0.64–0.82`)
- top-right corner: fire weapon; top-left corner: restart
- gamepad: left stick + dpad steer, A/RT accelerate, B/LT brake, X fire

## Regenerating the asset pack

`app/src/main/assets/gamedata/` is gitignored (123 MB, derived data). To
restage it: copy `rvsource/PC/assets/final/**` to that folder with every
file/dir name LOWERCASED, plus `rvsource/CARINFO.TXT` as `carinfo.txt`.

## Known first-build risks

No NDK was available on the machine that authored this scaffold, so the
first build may surface a short list of missing shim constants/types —
expected; fix per the workflow above. The `ALooper_pollAll` call in
android_main.cpp is deprecated in newer glue versions; swap to
`ALooper_pollOnce` semantics if games-activity >= 2.1 is used.
