cmake_minimum_required(VERSION 3.22.1)

project(sm64coopdxvr_android LANGUAGES C CXX)

find_package(OpenXR REQUIRED CONFIG)

set(SM64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../..")
set(SM64_GENERATED_ROOT "C:/vr-dev/sm64coopdx/build/us_pc")
set(LUA_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/lua-5.3.5/src")
set(COOPNET_ANDROID_ROOT "${SM64_ROOT}/lib/coopnet/android/arm64-v8a")

# Link only the Android transport from the reference port. This deliberately
# avoids importing its unrelated renderer, activity, or game-loop changes.
add_library(coopnet_android STATIC IMPORTED)
set_target_properties(coopnet_android PROPERTIES
    IMPORTED_LOCATION "${COOPNET_ANDROID_ROOT}/libcoopnet.a")
add_library(juice_android STATIC IMPORTED)
set_target_properties(juice_android PROPERTIES
    IMPORTED_LOCATION "${COOPNET_ANDROID_ROOT}/libjuice.a")

# Compile the existing SM64 display-list/OpenGL renderer against Android's
# native OpenGL ES headers. This object target is intentionally kept separate
# from the OpenXR host until the game lifecycle and frame hand-off are wired.
add_library(
    sm64_game_renderer
    OBJECT
    ${SM64_ROOT}/src/pc/gfx/gfx_cc.c
    ${SM64_ROOT}/src/pc/gfx/gfx_opengl.c
)

file(GLOB SM64_GAME_CORE_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/game/*.c
    ${SM64_ROOT}/src/engine/*.c
    ${SM64_ROOT}/src/menu/*.c
    ${SM64_ROOT}/src/buffers/*.c
)
list(REMOVE_ITEM SM64_GAME_CORE_SOURCES ${SM64_ROOT}/src/game/main.c)
add_library(sm64_game_core OBJECT ${SM64_GAME_CORE_SOURCES})

# Quest testing uses the debuggable APK so ADB/logcat remain available. CMake's
# default Debug flags are otherwise completely unoptimized, which makes the
# full game and display-list interpreter substantially slower than the release
# users will receive. Keep symbols, but compile native code at a release-like
# optimization level for representative on-headset performance.
set(SM64_QUEST_NATIVE_TARGETS
    sm64_game_renderer
    sm64_game_core
)

file(GLOB SM64_LUA_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/lua/*.c
    ${SM64_ROOT}/src/pc/lua/utils/*.c
    ${LUA_SOURCE_ROOT}/*.c
)
list(REMOVE_ITEM SM64_LUA_SOURCES
    ${LUA_SOURCE_ROOT}/lua.c
    ${LUA_SOURCE_ROOT}/luac.c)
file(GLOB SM64_AUDIO_SOURCES CONFIGURE_DEPENDS ${SM64_ROOT}/src/audio/*.c)
file(GLOB SM64_LIBULTRA_SOURCES CONFIGURE_DEPENDS ${SM64_ROOT}/lib/src/*.c)
list(REMOVE_ITEM SM64_LIBULTRA_SOURCES
    ${SM64_ROOT}/lib/src/leointerrupt.c
    ${SM64_ROOT}/lib/src/unk_shindou_file_3.c)
file(GLOB SM64_GODDARD_SOURCES CONFIGURE_DEPENDS ${SM64_ROOT}/src/goddard/*.c)
file(GLOB SM64_GODDARD_DYNLIST_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/goddard/dynlists/*.c)
file(GLOB SM64_WORLD_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/actors/*.c
    ${SM64_ROOT}/assets/*.c
    ${SM64_ROOT}/bin/*.c
    ${SM64_ROOT}/data/*.c
    ${SM64_ROOT}/data/*.cpp
    ${SM64_ROOT}/levels/*/geo.c
    ${SM64_ROOT}/levels/*/leveldata.c
    ${SM64_ROOT}/levels/*/script.c
    ${SM64_ROOT}/levels/scripts.c
    ${SM64_ROOT}/levels/entry.c
    ${SM64_GENERATED_ROOT}/assets/demo_data.c
    ${SM64_GENERATED_ROOT}/assets/mario_anim_data.c
)
list(REMOVE_ITEM SM64_WORLD_SOURCES
    ${SM64_ROOT}/data/dynos_c.cpp ${SM64_ROOT}/data/dynos_warps.cpp)
file(GLOB_RECURSE SM64_NETWORK_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/network/*.c)
file(GLOB SM64_DJUI_SOURCES CONFIGURE_DEPENDS ${SM64_ROOT}/src/pc/djui/*.c)
file(GLOB SM64_UTIL_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/utils/*.c
    ${SM64_ROOT}/src/pc/utils/miniz/*.c)
file(GLOB SM64_CONTROLLER_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/controller/*.c)
list(REMOVE_ITEM SM64_CONTROLLER_SOURCES
    ${SM64_ROOT}/src/pc/controller/controller_sdl.c
    ${SM64_ROOT}/src/pc/controller/controller_mouse.c
    ${SM64_ROOT}/src/pc/controller/controller_bind_mapping.c)
file(GLOB_RECURSE SM64_FS_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/fs/*.c)
file(GLOB SM64_MOD_SOURCES CONFIGURE_DEPENDS
    ${SM64_ROOT}/src/pc/mods/*.c
    ${SM64_ROOT}/src/pc/mods/*.cpp)
add_library(
    sm64_game_support OBJECT
    ${SM64_AUDIO_SOURCES}
    ${SM64_LIBULTRA_SOURCES}
    ${SM64_GODDARD_SOURCES}
    ${SM64_GODDARD_DYNLIST_SOURCES}
    ${SM64_ROOT}/sound/samples_assets.c
    ${SM64_ROOT}/sound/sequences_assets.c
    ${SM64_ROOT}/sound/sound_data.c
    ${SM64_LUA_SOURCES}
    ${SM64_WORLD_SOURCES}
    ${SM64_NETWORK_SOURCES}
    ${SM64_DJUI_SOURCES}
    ${SM64_UTIL_SOURCES}
    ${SM64_CONTROLLER_SOURCES}
    ${SM64_FS_SOURCES}
    ${SM64_MOD_SOURCES}
    ${SM64_ROOT}/data/dynos_c.cpp
    ${SM64_ROOT}/data/dynos_warps.cpp
    ${SM64_ROOT}/src/engine/lighting_engine.cpp
    ${SM64_ROOT}/src/pc/cliopts.c
    ${SM64_ROOT}/src/pc/chat_commands.c
    ${SM64_ROOT}/src/pc/configfile.c
    ${SM64_ROOT}/src/pc/release_notes.c
    ${SM64_ROOT}/src/pc/dialog_table.c
    ${SM64_ROOT}/src/pc/debug_context.c
    ${SM64_ROOT}/src/pc/ini.c
    ${SM64_ROOT}/src/pc/gfx/gfx_pc.c
    ${SM64_ROOT}/src/pc/nametags.c
    ${SM64_ROOT}/src/pc/loading.c
    ${SM64_ROOT}/src/pc/mixer.c
    ${SM64_ROOT}/src/pc/thread.c
    ${SM64_ROOT}/src/pc/audio/audio_null.c
    ${SM64_ROOT}/src/pc/gfx/gfx_dummy.c
    ${SM64_ROOT}/src/pc/network/sync_object.c
    quest_android_shims.c
    quest_audio.c
    quest_game_runtime.c
    quest_speech_input.c
    quest_vr_bridge.c
    ${SM64_ROOT}/src/pc/rom_assets.c
    ${SM64_ROOT}/src/pc/ultra_reimplementation.c
    ${SM64_ROOT}/src/pc/vr/vr.c
)
list(APPEND SM64_QUEST_NATIVE_TARGETS sm64_game_support)

target_include_directories(
    sm64_game_renderer
    PRIVATE
    ${SM64_ROOT}
    ${SM64_ROOT}/include
    ${SM64_ROOT}/src
    ${SM64_ROOT}/lib/lua/include
)

target_compile_definitions(
    sm64_game_renderer
    PRIVATE
    __ANDROID__
    NON_MATCHING
    AVOID_UB
    TARGET_LINUX
    ENABLE_OPENGL
    F3DEX_GBI_2E
    _LANGUAGE_C
    NO_SEGMENTED_MEMORY
    VERSION_US
    STDC_HEADERS
    USE_GLES
    RAPI_GL
    NO_BZERO_BCOPY
)

target_include_directories(
    sm64_game_support PRIVATE
    ${SM64_ROOT} ${SM64_ROOT}/include ${SM64_ROOT}/src
    ${SM64_ROOT}/sound ${LUA_SOURCE_ROOT} ${SM64_ROOT}/lib/lua/include
    ${SM64_ROOT}/lib/coopnet/include
    ${SM64_GENERATED_ROOT} ${SM64_GENERATED_ROOT}/include
)
target_compile_definitions(
    sm64_game_support PRIVATE
    __ANDROID__ NON_MATCHING AVOID_UB TARGET_LINUX ENABLE_OPENGL F3DEX_GBI_2E
    _LANGUAGE_C NO_SEGMENTED_MEMORY VERSION_US STDC_HEADERS USE_GLES RAPI_GL
    NO_BZERO_BCOPY COOPNET
)
target_compile_features(sm64_game_support PRIVATE cxx_std_17)
target_compile_options(sm64_game_support PRIVATE -Wno-error=format-security)

target_include_directories(
    sm64_game_core PRIVATE
    ${SM64_ROOT} ${SM64_ROOT}/include ${SM64_ROOT}/src
    ${SM64_ROOT}/sound ${SM64_ROOT}/lib/lua/include
    ${SM64_ROOT}/lib/coopnet/include
    ${SM64_GENERATED_ROOT} ${SM64_GENERATED_ROOT}/include
)
target_compile_definitions(
    sm64_game_core PRIVATE
    __ANDROID__ NON_MATCHING AVOID_UB TARGET_LINUX ENABLE_OPENGL F3DEX_GBI_2E
    _LANGUAGE_C NO_SEGMENTED_MEMORY VERSION_US STDC_HEADERS USE_GLES RAPI_GL
    NO_BZERO_BCOPY
)

add_library(
    android_native_app_glue
    STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(
    android_native_app_glue
    PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

add_library(
    sm64coopdxvr
    SHARED
    quest_openxr_bootstrap.c
    quest_openxr_input.c
    quest_rom.c
    $<TARGET_OBJECTS:sm64_game_core>
    $<TARGET_OBJECTS:sm64_game_renderer>
    $<TARGET_OBJECTS:sm64_game_support>
)
list(APPEND SM64_QUEST_NATIVE_TARGETS sm64coopdxvr)

foreach(target IN LISTS SM64_QUEST_NATIVE_TARGETS)
    target_compile_options(
        ${target}
        PRIVATE
        $<$<CONFIG:Debug>:-O2>
    )
endforeach()

add_dependencies(sm64coopdxvr sm64_game_renderer sm64_game_core sm64_game_support)

target_include_directories(sm64coopdxvr PRIVATE
    ${SM64_ROOT} ${SM64_ROOT}/include ${SM64_ROOT}/src)

target_compile_definitions(
    sm64coopdxvr
    PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
)

target_compile_options(
    sm64coopdxvr
    PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(
    sm64coopdxvr
    PRIVATE
    android_native_app_glue
    OpenXR::openxr_loader
    EGL
    GLESv3
    android
    dl
    log
    z
    coopnet_android
    juice_android
)

target_link_options(sm64coopdxvr PRIVATE "-uANativeActivity_onCreate")
