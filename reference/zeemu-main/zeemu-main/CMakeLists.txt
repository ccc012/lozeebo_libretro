cmake_minimum_required(VERSION 3.10)
project(Zeemu)

set(CMAKE_CXX_STANDARD 17)

# GCC on Windows can crash with @response-file mechanism — disable it globally
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES  OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES    OFF)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS   OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_OBJECTS     OFF)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_LIBRARIES OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_LIBRARIES   OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(SDL3_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

# Avoid building SDL3.dll with w64devkit in CLion: GCC/ld may fail on TEMP @files.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)

# stb_image.h — single-header image decoder (JPEG + BMP) for MIF icon display
set(STB_IMAGE_H "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_image.h")
if(NOT EXISTS "${STB_IMAGE_H}")
    message(STATUS "Downloading stb_image.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${STB_IMAGE_H}"
        STATUS _stb_status
    )
    list(GET _stb_status 0 _stb_code)
    if(_stb_code)
        message(WARNING "stb_image.h download failed (code ${_stb_code}) — MIF icons disabled")
    else()
        message(STATUS "stb_image.h downloaded")
    endif()
endif()

add_subdirectory(${SDL3_DIR})

# ImGui backend sources
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)
set(IMGUI_SRCS
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

if(CMAKE_COMPILER_IS_GNUCC)
    add_definitions(-Wall -Wextra)
    set_source_files_properties(
        third_party/sqlite/sqlite3.c
        PROPERTIES
        COMPILE_FLAGS
        "-Wno-cast-function-type -Wno-unused-parameter -Wno-unused-but-set-variable"
    )
endif(CMAKE_COMPILER_IS_GNUCC)

include_directories(.)

add_library(ZeemuCompression STATIC
        util/Compression.cpp
        third_party/StbImage.cpp
)
target_include_directories(ZeemuCompression
        PUBLIC
        ${PROJECT_SOURCE_DIR}
)
set_target_properties(ZeemuCompression PROPERTIES POSITION_INDEPENDENT_CODE ON)

# higan ARM7TDMI — unity-build: arm7tdmi.cpp #includes all its own .cpp files
set(ARM7TDMI_SRCS
        cpu/arm7tdmi/arm7tdmi.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
            cpu/arm7tdmi/arm7tdmi.cpp
            cpu/core/CPU.cpp
            cpu/memory/EndianMemory.cpp
            cpu/memory/VirtualMemory.cpp
            PROPERTIES
            COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>"
    )
endif()

set(CORE_SRCS
        brew/BrewLoader.cpp
        brew/BrewShell.cpp
        brew/BrewThreadScheduler.cpp
        brew/BrewAEEHelpers.cpp
        brew/BrewHID.cpp
        brew/BrewHash.cpp
        brew/BrewCipher.cpp
        brew/BrewRandom.cpp
        brew/BrewAppHistory.cpp
        brew/BrewMenuCtl.cpp
        brew/BrewSourceUtil.cpp
        brew/BrewNet.cpp
        brew/BrewZWheelOem.cpp
        brew/BrewAppUI.cpp
        brew/BrewMicro3D.cpp
        brew/BrewFlash.cpp
        brew/BrewShellClasses.cpp
        brew/BrewShellResources.cpp
        brew/BrewFileMgr.cpp
        brew/GgzArchive.cpp
        brew/BrewDisplay.cpp
        brew/BrewFont.cpp
        brew/BrewImage.cpp
        brew/BrewImageDecoder.cpp
        brew/BrewBitmap.cpp
        brew/BrewBitmapBlit.cpp
        brew/BrewBitmapColor.cpp
        brew/BrewBitmapInfo.cpp
        brew/BrewBitmapPixels.cpp
        brew/BrewBitmapTransform.cpp
        brew/BrewApplet.cpp
        brew/BrewAppletCtl.cpp
        brew/BrewHeap.cpp
        brew/BrewGraphics.cpp
        brew/Brew3D.cpp
        brew/Brew3DModel.cpp
        brew/Brew3DUtil.cpp
        brew/BrewEGL.cpp
        brew/BrewGL.cpp
        brew/BrewQXGL.cpp
        brew/BrewQXGLDispatch.cpp
        brew/BrewQXGLDraw.cpp
        brew/BrewDatabase.cpp
        brew/BrewAudio.cpp
        brew/BrewSoundPlayer.cpp
        brew/BrewSound.cpp
        brew/BrewMediaPCM.cpp
        brew/BrewMediaUtil.cpp
        brew/BrewMemCache.cpp
        brew/BrewMemAStream.cpp
        brew/BrewUnzipStream.cpp
        third_party/sqlite/sqlite3.c
        cpu/disassembler/Disassembler.cpp
        cpu/memory/VirtualMemory.cpp
        cpu/memory/EndianMemory.cpp
        cpu/memory/DecoratorMemory.cpp
        cpu/memory/PagedMemory.cpp
        cpu/platform/Time.cpp
        cpu/core/CPU.cpp
        cpu/cpu.h
        ${ARM7TDMI_SRCS}
        vfs/VirtualFileSystem.cpp
)

ADD_LIBRARY(Zeemu SHARED ${CORE_SRCS})
target_include_directories(Zeemu
        PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/cpu
        ${PROJECT_SOURCE_DIR}/cpu/arm7tdmi
        ${PROJECT_SOURCE_DIR}/third_party/sqlite
        ${PROJECT_SOURCE_DIR}/third_party/minimp3
)
target_link_libraries(Zeemu ZeemuCompression SDL3::SDL3)

option(ZEEMU_USE_FFMPEG "Enable FFmpeg-backed media decode paths such as QCP/PureVoice" ON)
set(FFMPEG_ROOT "" CACHE PATH "Optional FFmpeg SDK root containing include/ and lib/")
if(ZEEMU_USE_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libavutil)
    endif()

    if(NOT FFMPEG_FOUND AND FFMPEG_ROOT)
        find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
                PATHS "${FFMPEG_ROOT}/include"
                NO_DEFAULT_PATH)
        find_library(FFMPEG_AVFORMAT_LIBRARY avformat
                PATHS "${FFMPEG_ROOT}/lib"
                NO_DEFAULT_PATH)
        find_library(FFMPEG_AVCODEC_LIBRARY avcodec
                PATHS "${FFMPEG_ROOT}/lib"
                NO_DEFAULT_PATH)
        find_library(FFMPEG_AVUTIL_LIBRARY avutil
                PATHS "${FFMPEG_ROOT}/lib"
                NO_DEFAULT_PATH)
        if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVFORMAT_LIBRARY AND FFMPEG_AVCODEC_LIBRARY AND FFMPEG_AVUTIL_LIBRARY)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_RUNTIME_DIR "${FFMPEG_ROOT}/bin")
        endif()
    endif()

    if(FFMPEG_FOUND)
        target_compile_definitions(Zeemu PRIVATE ZEEMU_USE_FFMPEG=1)
        if(TARGET PkgConfig::FFMPEG)
            target_link_libraries(Zeemu PkgConfig::FFMPEG)
        else()
            target_include_directories(Zeemu PRIVATE "${FFMPEG_INCLUDE_DIR}")
            target_link_libraries(Zeemu
                    "${FFMPEG_AVFORMAT_LIBRARY}"
                    "${FFMPEG_AVCODEC_LIBRARY}"
                    "${FFMPEG_AVUTIL_LIBRARY}")
        endif()
        message(STATUS "FFmpeg media backend enabled")
    else()
        message(STATUS "FFmpeg media backend disabled: libavformat/libavcodec/libavutil not found")
    endif()
endif()

add_executable(ZeemuApp
        main.cpp
        runtime/AppRunner.cpp
        runtime/AEEHelperTable.cpp
        runtime/GuestCallRunner.cpp
        runtime/CliCommands.cpp
        frontend/Launcher.cpp
        graphics/RenderBackend.cpp
        ${IMGUI_SRCS}
        loaders/ELFLoader.cpp
        loaders/PELoader.cpp
        tools/app_inspector/AppPackageInspector.cpp
        tools/firmware_inspector/FirmwareInspector.cpp
        tools/firmware_inspector/SplitFirmwareInspector.cpp
)
target_link_libraries(ZeemuApp Zeemu SDL3::SDL3)
set_target_properties(ZeemuApp PROPERTIES OUTPUT_NAME Zeemu)
if(FFMPEG_FOUND AND FFMPEG_RUNTIME_DIR AND EXISTS "${FFMPEG_RUNTIME_DIR}")
    file(GLOB FFMPEG_RUNTIME_DLLS CONFIGURE_DEPENDS
            "${FFMPEG_RUNTIME_DIR}/*.dll")
    foreach(FFMPEG_RUNTIME_DLL IN LISTS FFMPEG_RUNTIME_DLLS)
        add_custom_command(TARGET ZeemuApp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FFMPEG_RUNTIME_DLL}" "$<TARGET_FILE_DIR:ZeemuApp>")
    endforeach()
endif()
