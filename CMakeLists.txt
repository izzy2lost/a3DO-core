cmake_minimum_required(VERSION 3.16)
project(retro3do_core CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RETRO3DO_TRACING "Compile the core's instruction/bus tracing hooks" OFF)

# libchdr - reading CHD, which is how nearly every 3DO dump in the wild is
# stored. BSD-3-Clause, and self-contained: it brings its own LZMA, miniz and
# zstd, so there is nothing to find on the host and nothing extra to cross
# compile for Android or iOS. Vendored rather than submoduled because the core
# needs it built a particular way and nothing else in the estate uses it.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "" FORCE)
# Not optional: libchdr's own CMakeLists defaults CHDR_WANT_TESTS to ON and
# then add_subdirectory(tests) on a tests/ directory the vendored copy does not
# carry, so a configure fails outright without this.
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
# Let the library reassemble whole frames, sync and ECC and subcode included.
#
# Turning these off does not merely change where a sector's payload starts - it
# stops the CD codecs reassembling a raw sector at all, and every disc dumped
# as MODE1_RAW fails to decompress its very first hunk. That is most 3DO dumps
# in circulation: with them off, one disc in a twelve-title library worked and
# the other eleven looked like emulator bugs.
#
# Where the payload sits inside a frame is then this project's problem, and is
# handled per track type in core/disc.cpp.
set(CHDR_WANT_RAW_DATA_SECTOR ON CACHE BOOL "" FORCE)
set(CHDR_WANT_SUBCODE ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/libchdr EXCLUDE_FROM_ALL)

add_library(retro3do_core STATIC
    core/arm60.cpp
    core/audio_ring.cpp
    core/bus.cpp
    core/chd.cpp
    core/clio.cpp
    core/dsp.cpp
    core/pbus.cpp
    core/console.cpp
    core/disc.cpp
    core/demo_rom.cpp
    core/frame_mailbox.cpp
    core/madam.cpp
    core/pad.cpp
    core/pad_layout.cpp
    core/path.cpp
    core/settings.cpp
    core/sport.cpp
    core/vdlp.cpp
    core/xbus.cpp
)

if(RETRO3DO_TRACING)
    target_compile_definitions(retro3do_core PUBLIC RETRO3DO_TRACING=1)
endif()

# The repository root, so a consumer writes #include "core/bus.h" -- the same
# spelling the sources used when core/ lived at src/core/ inside the app.
target_include_directories(retro3do_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(retro3do_core PRIVATE chdr-static)

# The core is a software emulator: it is the one thing that must be optimised in
# every configuration, or a debug build is too slow to run a game at all. A
# consuming app and its UI stay fully debuggable.
if(NOT MSVC)
    target_compile_options(retro3do_core PRIVATE
        $<$<CONFIG:Debug>:-O2>
        -fno-math-errno
        -Wall -Wextra -Wno-unused-parameter
    )
endif()
