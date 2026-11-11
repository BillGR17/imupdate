# Set minimum version and project details
cmake_minimum_required(VERSION 3.16)

# Set compilers to Clang (as requested). This must be set before project()
# because project() runs compiler detection and ignores any compiler set after it.
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")

project(ArchUpdater LANGUAGES CXX)

# --- Configuration ---

# Use C++ 23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Set output directory for executable (build/bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- Dependencies ---

# 1. Find system libraries
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# Find GTK3 + libayatana-appindicator for the system tray.
# libayatana-appindicator implements StatusNotifierItem (SNI) over D-Bus,
# which is the only tray protocol that works on Wayland (Hyprland + Waybar).
# Plain GtkStatusIcon is X11-only.
find_package(PkgConfig REQUIRED)
pkg_check_modules(TRAY REQUIRED ayatana-appindicator3-0.1 gtk+-3.0)

# 2. Fetch Dear ImGui using FetchContent
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.5
)
FetchContent_MakeAvailable(imgui)

# --- Target: Executable ---

# Add our executable from the src/ directory
add_executable(imupdate
  src/main.cpp
  src/Utils.cpp
  src/Updates.cpp
  src/UI.cpp
)

# Add the ImGui source files directly to our target
target_sources(imupdate PRIVATE
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Include directories
target_include_directories(imupdate PUBLIC
  src
  src/include
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${GLFW_INCLUDE_DIRS}
  ${TRAY_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(imupdate PRIVATE
  glfw
  OpenGL
  ${TRAY_LIBRARIES}
)

target_compile_definitions(imupdate PRIVATE TRAY_APPINDICATOR=1)
