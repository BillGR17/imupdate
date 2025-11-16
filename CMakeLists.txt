# Set minimum version and project details
cmake_minimum_required(VERSION 3.16)
project(ArchUpdater LANGUAGES CXX)

# --- Configuration ---

# Use C++ 20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set compilers to Clang (as requested)
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")

# Set output directory for executable (build/bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- Dependencies ---

# 1. Find system libraries
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# 2. Fetch Dear ImGui using FetchContent
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.4
)
FetchContent_MakeAvailable(imgui)

# --- Target: Executable ---

# Add our executable from the src/ directory
add_executable(imupdate
  src/main.cpp
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
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${GLFW_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(imupdate PRIVATE
  glfw
  OpenGL
)
