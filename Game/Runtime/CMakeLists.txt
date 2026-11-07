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
# gram:dep:stb begin
find_package(Stb REQUIRED)
# gram:dep:stb end
# --- end gram dependencies ---

add_executable(Runtime src/main.cpp src/TrianglePass.cpp)
# Content/ holds the game's generated component header (Components.generated.hpp).
target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Content ${Stb_INCLUDE_DIR})
target_precompile_headers(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../GramEngine/include/Gram/Application/Common.hpp)

gram_add_hlsl_raster_shader(Runtime ${CMAKE_CURRENT_SOURCE_DIR}/../Content/shaders/Default.hlsl)
gram_add_hlsl_compute_shader(Runtime ${CMAKE_CURRENT_SOURCE_DIR}/../Content/shaders/RayTracer.hlsl)

gram_add_texture(Runtime ${CMAKE_CURRENT_SOURCE_DIR}/../Content/textures/texture.jpg)

# gram:link:GramEngine begin
target_link_libraries(Runtime PRIVATE GramEngine)
# gram:link:GramEngine end
