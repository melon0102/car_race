cmake_minimum_required(VERSION 3.22.1)
project(revolt LANGUAGES C CXX)

# SDL-style shell: Java SurfaceView + JNI surface handoff (no app glue)

set(GAME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/game)

# ---------------------------------------------------------------------------
# Game core sources — enabled incrementally as the shim layer grows.
# Phase 0 target: pure math/logic modules compile and link.
# Uncomment the next block as each file is made to build (see README).
# ---------------------------------------------------------------------------
set(GAME_SOURCES
    ${GAME_DIR}/Geom.cpp
    ${GAME_DIR}/Gaussian.cpp
    ${GAME_DIR}/Util.cpp
    ${GAME_DIR}/Debug.cpp
    # physics core
    ${GAME_DIR}/timing.cpp
    ${GAME_DIR}/NewColl.cpp
    ${GAME_DIR}/Body.cpp
    ${GAME_DIR}/Wheel.cpp
    ${GAME_DIR}/Aerial.cpp
    ${GAME_DIR}/Field.cpp
    ${GAME_DIR}/move.cpp
    # car & objects
    ${GAME_DIR}/car.cpp
    ${GAME_DIR}/Particle.cpp
    ${GAME_DIR}/object.cpp
    ${GAME_DIR}/objinfo.cpp
    ${GAME_DIR}/obj_init.cpp
    ${GAME_DIR}/instance.cpp
    ${GAME_DIR}/player.cpp
    ${GAME_DIR}/Ghost.cpp
    ${GAME_DIR}/weapon.cpp
    ${GAME_DIR}/trigger.cpp
    ${GAME_DIR}/piano.cpp
    ${GAME_DIR}/smoke.cpp
    # world & level
    ${GAME_DIR}/level.cpp
    ${GAME_DIR}/model.cpp
    ${GAME_DIR}/light.cpp
    ${GAME_DIR}/visibox.cpp
    ${GAME_DIR}/grid.cpp
    ${GAME_DIR}/mirror.cpp
    # AI
    ${GAME_DIR}/ai.cpp
    ${GAME_DIR}/ai_car.cpp
    ${GAME_DIR}/ai_init.cpp
    ${GAME_DIR}/ainode.cpp
    ${GAME_DIR}/aizone.cpp
    # control
    ${GAME_DIR}/Control.cpp
    ${GAME_DIR}/ctrlread.cpp
    # rendering-adjacent (draw through the shimmed D3D device)
    ${GAME_DIR}/camera.cpp
    ${GAME_DIR}/Spark.cpp
    ${GAME_DIR}/sfx.cpp
    ${GAME_DIR}/draw.cpp
    ${GAME_DIR}/DrawObj.cpp
    ${GAME_DIR}/panel.cpp
    ${GAME_DIR}/texture.cpp
    ${GAME_DIR}/world.cpp
    ${GAME_DIR}/text.cpp
    ${GAME_DIR}/shadow.cpp
    ${GAME_DIR}/ReadInit.cpp
    # in-game editors (DX-free, referenced by ai/object modules)
    ${GAME_DIR}/editai.cpp
    ${GAME_DIR}/editobj.cpp
    ${GAME_DIR}/editcam.cpp
    ${GAME_DIR}/editzone.cpp
    ${GAME_DIR}/edittrig.cpp
    ${GAME_DIR}/edfield.cpp
    ${GAME_DIR}/edportal.cpp
    # game flow
    ${GAME_DIR}/gameloop.cpp
    ${GAME_DIR}/TitleScreen.cpp
    ${GAME_DIR}/Menu.cpp
    ${GAME_DIR}/MenuData.cpp
    ${GAME_DIR}/MenuText.cpp
)

# Stage 2 candidates (need more shim coverage / stub globals):
#   ${GAME_DIR}/timing.cpp      ${GAME_DIR}/NewColl.cpp
#   ${GAME_DIR}/Body.cpp        ${GAME_DIR}/car.cpp
#   ${GAME_DIR}/Wheel.cpp       ${GAME_DIR}/ai.cpp
#   ${GAME_DIR}/ainode.cpp      ${GAME_DIR}/aizone.cpp
#   ${GAME_DIR}/model.cpp       ${GAME_DIR}/world.cpp
#   ... (full list in README)
# Never enable (replaced by the platform layer):
#   dx.cpp Dxerrors.cpp main.cpp registry.cpp Input.cpp sfx.cpp

add_library(revolt SHARED
    main/android_native.cpp
    main/game_bridge.cpp
    main/game_boot.cpp
    platform/winshim_impl.cpp
    platform/mss_audio.cpp
    platform/gl_device.cpp
    platform/gl_renderer.cpp
    platform/dx_replacement.cpp
    platform/android_input.cpp
    platform/cpu_driver.cpp
    platform/debug_overlay.cpp
    platform/touch_overlay.cpp
    platform/net_stub.cpp
    ${GAME_SOURCES}
)

target_include_directories(revolt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/winshim   # fake <windows.h>, <d3d.h>, ...
    ${GAME_DIR}/inc
    ${GAME_DIR}
)

target_compile_definitions(revolt PRIVATE
    _PC                 # select the PC (float) paths in the game code
    ANDROID_PORT        # our own marker for port-specific patches
)

# 1999-era code: silence what would otherwise be thousands of warnings
target_compile_options(revolt PRIVATE
    -Wno-writable-strings
    -Wno-dangling-else
    -Wno-parentheses
    -Wno-logical-op-parentheses
    -Wno-deprecated-declarations
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-missing-braces
    -fno-strict-aliasing
    -Wno-c++11-narrowing
    -ferror-limit=200
)

target_link_libraries(revolt
    android
    EGL
    GLESv3
    aaudio
    log
)
