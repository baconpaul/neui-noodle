cmake_minimum_required(VERSION 3.21)
project(neuiplusplus LANGUAGES CXX)

# neuiplusplus - a C++20 skin over the neui C API.
#
# Consumed with add_subdirectory(). It does NOT fetch or configure neui itself:
# the neui targets must already exist, because which HOSTS are compiled in is
# the application's decision, not this library's.
#
#   set(NEUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
#   set(NEUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
#   add_subdirectory(libs/neui)
#   add_subdirectory(libs/neuiplusplus)
#   target_link_libraries(my-ui PRIVATE neuiplusplus)
if(NOT TARGET neui)
  message(FATAL_ERROR
    "neuiplusplus needs the neui targets, and none are defined.\n"
    "Add neui to the build BEFORE this directory:\n"
    "    add_subdirectory(<path-to>/neui)\n"
    "    add_subdirectory(<path-to>/neuiplusplus)")
endif()

add_library(neuiplusplus STATIC
  src/Component.cpp
  src/Session.cpp)

target_include_directories(neuiplusplus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(neuiplusplus PUBLIC cxx_std_20)
target_link_libraries(neuiplusplus PUBLIC neui)

# neui splits into the registry/core lib plus one static lib per host, and
# neui_init() only registers hosts that were compiled in. Link whichever the
# neui build produced, so a consumer's neui_get_api() can hand them back:
#   neui-xplhost   -> "neui.host.crossplatform"  (one surface, neui draws it all)
#   neui-macoshost -> "neui.host.macos"          (native AppKit / NSView tree)
#   neui-win32host -> "neui.host.win32"          (native HWND tree, Direct2D)
# The crossplatform host is the one this library is written against: it is the
# only host implementing cursors, the relative pointer, timers, embedding and
# accessibility.
foreach(host neui-xplhost neui-macoshost neui-win32host)
  if(TARGET ${host})
    target_link_libraries(neuiplusplus PUBLIC ${host})
  endif()
endforeach()

if(MSVC)
  # /W4 is the level neui itself builds at. /w14263 is the MSVC spelling of
  # -Wsuggest-override; see below for why it matters here.
  target_compile_options(neuiplusplus PRIVATE /W4 /utf-8 /w14263)
else()
  # -Wsuggest-override closes the one hole in the capability-interface design:
  # a handler written without the keyword compiles clean and silently never
  # fires. Consumers should turn it on for their own component code too - this
  # library cannot do it for them without dictating their warning flags.
  target_compile_options(neuiplusplus PRIVATE -Wall -Wextra -Wsuggest-override)
endif()

if(APPLE)
  # neui and its host libs reference each other's symbols, so libneui.a
  # legitimately lands on a link line twice. Silence ld_prime's cosmetic warning
  # about it for anything linking us. Apple-linker-only spelling - GNU ld / lld
  # reject the flag outright, and neither warns about this anyway.
  target_link_options(neuiplusplus INTERFACE "-Wl,-no_warn_duplicate_libraries")
endif()
