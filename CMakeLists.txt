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

# neuiplusplus - the C++20 layer over neui's C API, and what both programs here
# are written against. Its own library with its own CMakeLists
# (libs/neuiplusplus), consumed the way any outside project would consume it; it
# hard-errors if the neui targets are not already defined, hence the order.
add_subdirectory(libs/neuiplusplus)

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

# neuiplusplus carries neui and every host static lib that this neui build
# produced, so neui_get_api() here can hand back any of:
#   neui-macoshost -> "neui.host.macos"          (native AppKit / NSView tree)
#   neui-win32host -> "neui.host.win32"          (native HWND tree, Direct2D)
#   neui-xplhost   -> "neui.host.crossplatform"  (one surface, neui draws it all)
# There is no native Linux host, so there the xpl host is the only one - which
# is also what main.cpp asks for as its "native" id on this platform.
target_link_libraries(neui-noodle PRIVATE neuiplusplus)

if(MSVC)
  # /W4 is the level neui itself builds at. CMP0092 (NEW at our 3.21 floor)
  # already keeps /W3 out of the default flags, so there is nothing to strip.
  # /utf-8 makes source and execution charsets UTF-8, matching the manifest's
  # UTF-8 ACP declaration. /w14263 is the MSVC spelling of -Wsuggest-override.
  target_compile_options(neui-noodle PRIVATE /W4 /utf-8 /w14263)
  # link.exe auto-generates a manifest resource that collides with the one
  # win/neui-noodle.rc embeds ("duplicate resource. type:MANIFEST, name:1").
  # Turn the generated one off and let the .rc win.
  target_link_options(neui-noodle PRIVATE "/MANIFEST:NO")
  set_property(TARGET neui-noodle APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/win/neui-noodle.manifest")
else()
  # -Wsuggest-override closes the one hole in the capability-interface design:
  # an override written without the keyword. -Wl,-no_warn_duplicate_libraries
  # comes through neuiplusplus as an INTERFACE link option.
  target_compile_options(neui-noodle PRIVATE -Wall -Wextra -Wsuggest-override)
endif()

# The second program: a knob, a slider and a file picker, exercising more of the
# library than the noodle sandbox does.
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

if(MSVC)
  target_compile_options(plusplusdemo PRIVATE /W4 /utf-8 /w14263)
else()
  # -Wsuggest-override closes the one hole in the capability-interface design:
  # an override written without the keyword. The library builds itself with it;
  # component code has to opt in for itself, which is what this line is.
  target_compile_options(plusplusdemo PRIVATE -Wall -Wextra -Wsuggest-override)
  # -Wl,-no_warn_duplicate_libraries comes through neuiplusplus as an INTERFACE
  # link option, so there is nothing to add here.
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
