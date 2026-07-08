# CMake build system

cmake_minimum_required(VERSION 3.10)
project(zeebo_libretro LANGUAGES C)

option(ZEEBO_BUILD_SMOKE_TEST "Build host smoke test" OFF)
option(ZEEBO_STRICT_WARNINGS "Treat warnings as errors on supported compilers" OFF)

set(ZEEBO_CORE_SOURCES
    src/audio/audio.c
    src/audio/mixer.c
    src/audio/pcm.c
    src/brew/boot.c
    src/brew/brew.c
    src/brew/helpers.c
    src/brew/ibitmap.c
    src/brew/idisplay.c
    src/brew/idisplay_real.c
    src/brew/ifile.c
    src/brew/imemory.c
    src/brew/ishell.c
    src/brew/isound.c
    src/core/libretro_core.c
    src/cpu/cpu.c
    src/cpu/decode.c
    src/cpu/execute_arm.c
    src/cpu/execute_thumb.c
    src/cpu/flags.c
    src/debug/disasm.c
    src/debug/log.c
    src/debug/trace.c
    src/gpu/draw.c
    src/gpu/egl_gl.c
    src/gpu/framebuffer.c
    src/input/input.c
    src/loader/bar_parser.c
    src/loader/mif_parser.c
    src/loader/mod_loader.c
    src/memory/heap.c
    src/memory/memory.c
)

add_library(zeebo_libretro SHARED ${ZEEBO_CORE_SOURCES})

set_target_properties(zeebo_libretro PROPERTIES
    OUTPUT_NAME zeebo_libretro
    PREFIX ""
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ZEEBO_ARCH_64BIT 1)
    set(ZEEBO_ARCH_32BIT 0)
else()
    set(ZEEBO_ARCH_64BIT 0)
    set(ZEEBO_ARCH_32BIT 1)
endif()

set(ZEEBO_PLATFORM_DESKTOP 0)
set(ZEEBO_PLATFORM_MOBILE 0)
set(ZEEBO_PLATFORM_CONSOLE 0)
set(ZEEBO_PLATFORM_WEB 0)

if(EMSCRIPTEN)
    set(ZEEBO_PLATFORM_WEB 1)
elseif(ANDROID OR IOS)
    set(ZEEBO_PLATFORM_MOBILE 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    set(ZEEBO_PLATFORM_CONSOLE 1)
elseif(CMAKE_SYSTEM_NAME MATCHES "Nintendo|Orbis|Prospero|Vita|PSP|PlayStation|Durango|XBOX|Xbox|Switch")
    set(ZEEBO_PLATFORM_CONSOLE 1)
else()
    set(ZEEBO_PLATFORM_DESKTOP 1)
endif()

if(WIN32)
    set_target_properties(zeebo_libretro PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(zeebo_libretro PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(zeebo_libretro PROPERTIES SUFFIX ".so")
endif()

if(MSVC)
    target_compile_definitions(zeebo_libretro PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_definitions(zeebo_libretro PRIVATE
    ZEEBO_CORE_BUILD=1
    ZEEBO_ARCH_32BIT=${ZEEBO_ARCH_32BIT}
    ZEEBO_ARCH_64BIT=${ZEEBO_ARCH_64BIT}
    ZEEBO_PLATFORM_DESKTOP=${ZEEBO_PLATFORM_DESKTOP}
    ZEEBO_PLATFORM_MOBILE=${ZEEBO_PLATFORM_MOBILE}
    ZEEBO_PLATFORM_CONSOLE=${ZEEBO_PLATFORM_CONSOLE}
    ZEEBO_PLATFORM_WEB=${ZEEBO_PLATFORM_WEB}
)

target_include_directories(zeebo_libretro PRIVATE src src/core)

if(MSVC)
    target_compile_options(zeebo_libretro PRIVATE /W3)
    if(ZEEBO_STRICT_WARNINGS)
        target_compile_options(zeebo_libretro PRIVATE /WX)
    endif()
else()
    target_compile_options(zeebo_libretro PRIVATE -Wall -Wextra)
    if(ZEEBO_STRICT_WARNINGS)
        target_compile_options(zeebo_libretro PRIVATE -Werror)
    endif()
endif()

if(ZEEBO_BUILD_SMOKE_TEST AND NOT ZEEBO_PLATFORM_CONSOLE)
    add_executable(libretro_smoke tests/libretro_smoke.c)
    set_target_properties(libretro_smoke PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(libretro_smoke PRIVATE src src/core)
    target_link_libraries(libretro_smoke PRIVATE ${CMAKE_DL_LIBS})
    if(MSVC)
        target_compile_definitions(libretro_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(libretro_smoke PRIVATE /W3)
        if(ZEEBO_STRICT_WARNINGS)
            target_compile_options(libretro_smoke PRIVATE /WX)
        endif()
    else()
        target_compile_options(libretro_smoke PRIVATE -Wall -Wextra)
        if(ZEEBO_STRICT_WARNINGS)
            target_compile_options(libretro_smoke PRIVATE -Werror)
        endif()
    endif()
endif()
