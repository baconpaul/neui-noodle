cmake_minimum_required(VERSION 3.21)
project(neui-noodle LANGUAGES C CXX)

# Sandbox for evaluating the neui framework (libs/neui, a git submodule).
# macOS, Windows and Linux (X11) are all wired up.
if(APPLE)
  set(NOODLE_LINUX OFF)
elseif(WIN32)
  # Windows: native win32 host (HWND per widget, Direct2D) + the crossplatform
  # host. Needs the RC language for the application manifest below.
  set(NOODLE_LINUX OFF)
  enable_language(RC)
elseif(UNIX)
  # Linux / X11: neui has no native host here, only the crossplatform one
  # (Cairo software rendering blitted into an X window). Needs the X11 + Cairo
  # dev packages - see libs/neui/CLAUDE.md for the apt/dnf list.
  set(NOODLE_LINUX ON)
else()
  message(FATAL_ERROR "neui-noodle is set up for macOS, Windows and Linux (X11) only")
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

# On Windows the app carries a manifest resource (per-monitor v2 DPI, UTF-8
# ACP, and the supportedOS GUIDs that gate Win10/11 dark mode). Empty
# elsewhere.
set(NOODLE_WIN_SOURCES)
if(WIN32)
  set(NOODLE_WIN_SOURCES win/neui-noodle.rc)
  # Rebuild the .res when the manifest it embeds changes.
  set_property(SOURCE win/neui-noodle.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/win/neui-noodle.manifest")
endif()

# Both keywords are no-ops off their own platform, so one add_executable covers
# all three:
#   MACOSX_BUNDLE - the native AppKit host wants a real .app so NSApp can
#                   install a menubar and activate the process normally.
#   WIN32         - /SUBSYSTEM:WINDOWS. Required, not cosmetic: the win32 host
#                   supplies WinMain itself (hosts/win32/window.cpp) and calls
#                   our main() from it after setting DPI awareness, COM, the
#                   HINSTANCE and the window classes. Building for the console
#                   subsystem would bypass all of that.
# On Linux a plain ELF binary comes out.
add_executable(neui-noodle WIN32 MACOSX_BUNDLE src/main.cpp ${NOODLE_WIN_SOURCES})
target_compile_features(neui-noodle PRIVATE cxx_std_20)

# neui splits into: the registry/core lib (neui) + one static lib per host.
# neui_init() only registers hosts that were compiled in, so the set linked
# here is what neui_get_api() can hand back:
#   neui-macoshost -> "neui.host.macos"          (native AppKit / NSView tree)
#   neui-win32host -> "neui.host.win32"          (native HWND tree, Direct2D)
#   neui-xplhost   -> "neui.host.crossplatform"  (one surface, neui draws it all)
# There is no native Linux host, so there the xpl host is the only one - which
# is also what main.cpp asks for as its "native" id on this platform.
target_link_libraries(neui-noodle PRIVATE neui neui-xplhost)
if(APPLE)
  target_link_libraries(neui-noodle PRIVATE neui-macoshost)
endif()
if(WIN32)
  target_link_libraries(neui-noodle PRIVATE neui-win32host)
endif()

if(MSVC)
  # /W4 is the level neui itself builds at. CMP0092 (NEW at our 3.21 floor)
  # already keeps /W3 out of the default flags, so there is nothing to strip.
  # /utf-8 makes source and execution charsets UTF-8, matching the manifest's
  # UTF-8 ACP declaration.
  target_compile_options(neui-noodle PRIVATE /W4 /utf-8)
  # link.exe auto-generates a manifest resource that collides with the one
  # win/neui-noodle.rc embeds ("duplicate resource. type:MANIFEST, name:1").
  # Turn the generated one off and let the .rc win.
  target_link_options(neui-noodle PRIVATE "/MANIFEST:NO")
  set_property(TARGET neui-noodle APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/win/neui-noodle.manifest")
else()
  target_compile_options(neui-noodle PRIVATE -Wall -Wextra)
endif()

# neui and its host libs reference each other's symbols, so libneui.a legitimately
# lands on the link line twice. Silence ld_prime's cosmetic warning about it
# (neui does the same inside its own subtree). Apple-linker-only spelling - GNU
# ld / lld reject the flag outright, and they don't warn about this anyway.
if(APPLE)
  target_link_options(neui-noodle PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# neuiplusplus - the C++20 layer over neui's C API, plus a demo exercising it.
# Header-only apart from one implementation TU (the dispatch trampoline and the
# design-unit boundary).
add_library(neuiplusplus STATIC src/neuiplusplus.cpp)
target_include_directories(neuiplusplus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(neuiplusplus PUBLIC cxx_std_20)
target_link_libraries(neuiplusplus PUBLIC neui neui-xplhost)
if(APPLE)
  target_link_libraries(neuiplusplus PUBLIC neui-macoshost)
endif()
if(WIN32)
  target_link_libraries(neuiplusplus PUBLIC neui-win32host)
endif()

add_executable(plusplusdemo WIN32 MACOSX_BUNDLE src/plusplusdemo.cpp ${NOODLE_WIN_SOURCES})
target_link_libraries(plusplusdemo PRIVATE neuiplusplus)

if(APPLE)
  # A real CFBundleIdentifier is REQUIRED for the file dialogs, not cosmetic.
  # NSOpenPanel / NSSavePanel run out of process via
  # com.apple.appkit.xpc.openAndSavePanelService; with the empty identifier
  # CMake writes by default, that XPC connection fails and runModal returns
  # immediately having shown nothing - indistinguishable from the user
  # cancelling. NSAlert runs in-process, so message boxes work regardless,
  # which makes this a confusing one to diagnose.
  set_target_properties(plusplusdemo PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.baconpaul.neui-noodle.plusplusdemo
    MACOSX_BUNDLE_BUNDLE_NAME plusplusdemo)
  set_target_properties(neui-noodle PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.baconpaul.neui-noodle
    MACOSX_BUNDLE_BUNDLE_NAME neui-noodle)
endif()

foreach(t neuiplusplus plusplusdemo)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /utf-8)
  else()
    # -Wsuggest-override closes the one hole in the capability-interface design:
    # an override written without the keyword.
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wsuggest-override)
  endif()
endforeach()
if(APPLE)
  target_link_options(plusplusdemo PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()
if(MSVC)
  target_link_options(plusplusdemo PRIVATE "/MANIFEST:NO")
endif()

# `cmake --build build --target run`. TARGET_FILE resolves to the bundle's inner
# binary on macOS (so stdout still lands in the terminal) and to the plain
# executable everywhere else. On Windows the app is a GUI-subsystem binary, so
# it detaches from the console and prints nothing there - that is expected.
add_custom_target(run
  COMMAND "$<TARGET_FILE:neui-noodle>"
  DEPENDS neui-noodle
  USES_TERMINAL)
