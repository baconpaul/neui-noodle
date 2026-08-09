cmake_minimum_required(VERSION 3.21)
project(neui-noodle LANGUAGES C CXX)

# Sandbox for evaluating the neui framework (libs/neui, a git submodule).
# macOS only for now - the neui tree also builds on Windows / Linux, but this
# noodle project only wires up the mac hosts.
if(NOT APPLE)
  message(FATAL_ERROR "neui-noodle is currently set up for macOS only")
endif()

# We drive neui's C ABI from C++20. neui itself is C++17 internally; it sets
# CMAKE_CXX_STANDARD in its own directory scope, so it does not leak up here.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# neui's own examples + tests default OFF when it is consumed via
# add_subdirectory(); make that explicit so a stray cache entry can't turn
# them on and slow this build down.
set(NEUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NEUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/neui)

# MACOSX_BUNDLE: the native AppKit host wants a real .app so NSApp can install
# a menubar and activate the process normally.
add_executable(neui-noodle MACOSX_BUNDLE src/main.cpp)
target_compile_features(neui-noodle PRIVATE cxx_std_20)

# neui splits into: the registry/core lib (neui) + one static lib per host.
# neui_init() only registers hosts that were compiled in, so the set linked
# here is what neui_get_api() can hand back:
#   neui-macoshost -> "neui.host.macos"          (native AppKit / NSView tree)
#   neui-xplhost   -> "neui.host.crossplatform"  (one surface, neui draws it all)
target_link_libraries(neui-noodle PRIVATE neui neui-xplhost neui-macoshost)

target_compile_options(neui-noodle PRIVATE -Wall -Wextra)

# neui and its host libs reference each other's symbols, so libneui.a legitimately
# lands on the link line twice. Silence ld_prime's cosmetic warning about it
# (neui does the same inside its own subtree).
target_link_options(neui-noodle PRIVATE "-Wl,-no_warn_duplicate_libraries")

# `cmake --build build --target run` - the bundle's inner binary, so stdout
# still lands in the terminal.
add_custom_target(run
  COMMAND "$<TARGET_BUNDLE_CONTENT_DIR:neui-noodle>/MacOS/neui-noodle"
  DEPENDS neui-noodle
  USES_TERMINAL)
