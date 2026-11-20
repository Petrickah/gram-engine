cmake_minimum_required(VERSION 3.20)
project(Runtime)

# Pinned explicitly: Apple clang still defaults to C++14. C++20 for designated initializers,
# the style the Khronos Vulkan tutorial uses.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- gram dependencies (managed, do not edit by hand) ---
# gram:dep:GramEngine begin
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../GramEngine ${CMAKE_CURRENT_BINARY_DIR}/GramEngine)
# gram:dep:GramEngine end
# --- end gram dependencies ---

add_executable(Runtime src/main.cpp src/TrianglePass.cpp src/RayTracePass.cpp src/BVHTree.cpp)
# Content/ holds the game's generated component header (Components.generated.hpp).
target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Content)
target_precompile_headers(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../GramEngine/include/Gram/Application/Common.hpp)

# Every shader has a .pass.ron, so gram lists them all in Content/Shaders.generated.cmake (written by
# `gram build` before CMake runs).
include(${CMAKE_CURRENT_SOURCE_DIR}/../Content/Shaders.generated.cmake)
gram_add_content_shaders(Runtime)

gram_add_texture(Runtime ${CMAKE_CURRENT_SOURCE_DIR}/../Content/textures/texture.jpg)
gram_add_texture(Runtime ${CMAKE_CURRENT_SOURCE_DIR}/../Content/textures/autumn_field_puresky_4k.hdr)

# gram:link:GramEngine begin
target_link_libraries(Runtime PRIVATE GramEngine)
# gram:link:GramEngine end
