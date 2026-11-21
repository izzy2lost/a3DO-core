cmake_minimum_required(VERSION 3.16)
project(retro3do_core CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tests are built when this is the project being built and not when it is
# somebody's subdirectory: an application consuming the core wants the library,
# and its own test run should not be lengthened by ours.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RETRO3DO_CORE_TESTS_DEFAULT ON)
else()
    set(RETRO3DO_CORE_TESTS_DEFAULT OFF)
endif()
option(RETRO3DO_CORE_TESTS "Build the core's test suite"
       ${RETRO3DO_CORE_TESTS_DEFAULT})
option(RETRO3DO_TRACING "Compile the core's instruction/bus tracing hooks" OFF)
option(RETRO3DO_CORE_TOOLS "Build the headless measuring tools"
       ${RETRO3DO_CORE_TESTS_DEFAULT})

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
    core/console.cpp
    core/demo_rom.cpp
    core/disc.cpp
    core/dsp.cpp
    core/emulator_thread.cpp
    core/frame_mailbox.cpp
    core/hires.cpp
    core/madam.cpp
    core/pad.cpp
    core/pbus.cpp
    core/sport.cpp
    core/state.cpp
    core/vdlp.cpp
    core/xbus.cpp
    abi/retro3do.cpp
)
add_library(retro3do::core ALIAS retro3do_core)

# The boundary, expressed in the build rather than in a document nobody reads.
#
# include/ is PUBLIC: an application gets retro3do.h and nothing else, so it
# cannot reach a machine header even by accident, and a change to the shape of
# the emulator cannot break it. The repository root is PRIVATE: the ABI
# implementation and the core's own tests spell their includes "core/bus.h",
# which is the spelling those files have always used.
target_include_directories(retro3do_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(retro3do_core PRIVATE chdr-static)

# The emulator thread lives in here now, so the core brings its own threading
# requirement rather than every application remembering to add one.
find_package(Threads REQUIRED)
target_link_libraries(retro3do_core PUBLIC Threads::Threads)

if(RETRO3DO_TRACING)
    target_compile_definitions(retro3do_core PRIVATE RETRO3DO_TRACING=1)
endif()

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

if(RETRO3DO_CORE_TESTS)
    enable_testing()
    add_executable(retro3do_core_tests
        tests/test_main.cpp
        tests/test_abi.cpp
        tests/test_arm60.cpp
        tests/test_audio_pad.cpp
        tests/test_bus.cpp
        tests/test_chd.cpp
        tests/test_clio.cpp
        tests/test_disc.cpp
        tests/test_dsp.cpp
        tests/test_frame_mailbox.cpp
        tests/test_hires.cpp
        tests/test_madam.cpp
        tests/test_sport.cpp
        tests/test_state.cpp
        tests/test_vdlp.cpp
    )
    # The machine's own tests are allowed inside it; they are what the internals
    # exist to be checked by. test_abi.cpp deliberately includes only the public
    # header, so it fails the same way an application would.
    target_include_directories(retro3do_core_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(retro3do_core_tests PRIVATE retro3do_core)
    add_test(NAME retro3do_core_tests COMMAND retro3do_core_tests)

    # The smallest application that can exist, in C rather than C++.
    #
    # It gets no include directory beyond the one the core makes public, so it
    # stops building the moment the header stops being self-sufficient, and it
    # is compiled as C so a C++-ism that would break Swift's module-map import
    # is caught here rather than on somebody's Mac.
    add_executable(retro3do_abi_c_consumer tests/abi_c_consumer.c)
    set_target_properties(retro3do_abi_c_consumer PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(retro3do_abi_c_consumer PRIVATE retro3do_core)
    if(NOT MSVC)
        target_compile_options(retro3do_abi_c_consumer PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME retro3do_abi_c_consumer COMMAND retro3do_abi_c_consumer)
endif()

# ---------------------------------------------------------------------------
# Measuring tools
#
# gamecheck boots a whole library headless and reports what each title actually
# did - cels drawn, screens produced, CD commands answered. It needs discs,
# which are not in the repository, so it is an instrument rather than a test:
# point it at a library and it becomes one.
#
# These live here rather than in an application because they read the machine's
# internals, which is what makes them useful and what no front end is allowed to
# do. The Opera oracle in tools/opera_oracle is built by its own script and is
# deliberately not a target here - nothing of it is linked into the core.
# ---------------------------------------------------------------------------
if(RETRO3DO_CORE_TOOLS)
    add_executable(gamecheck tools/gamecheck/gamecheck.cpp)
    target_include_directories(gamecheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(gamecheck PRIVATE retro3do_core)
endif()
