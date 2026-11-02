cmake_minimum_required(VERSION 3.20)
project(GramEngine)

include(${CMAKE_CURRENT_LIST_DIR}/GramShaders.cmake)

# Pinned explicitly: Apple clang still defaults to C++14. C++20 for designated initializers
# (vk::...CreateInfo{ .field = ... }), the style the Khronos Vulkan tutorial uses.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vulkan-hpp structs only become aggregates (so designated initializers work) without their
# constructors. Must be identical in every translation unit that includes vulkan.hpp, hence global.
add_compile_definitions(VULKAN_HPP_NO_CONSTRUCTORS)

# Errors are reported through vk::Result instead of vk::SystemError exceptions: the enhanced
# calls (createShaderModuleUnique & co.) return a vk::ResultValue{ result, value } to check.
add_compile_definitions(VULKAN_HPP_NO_EXCEPTIONS)

# Disable ImGui Panels
set(GRAM_IMGUI_ENABLED)

include(FetchContent)

# --- gram dependencies (managed, do not edit by hand) ---
# gram:dep:vulkan begin
find_package(Vulkan REQUIRED)
# gram:dep:vulkan end
# gram:dep:glfw3 begin
find_package(glfw3 CONFIG REQUIRED)
# gram:dep:glfw3 end
# gram:dep:glm begin
find_package(glm CONFIG REQUIRED)
# gram:dep:glm end
# gram:dep:imgui begin
find_package(imgui CONFIG REQUIRED)
# gram:dep:imgui end
# gram:dep:spdlog begin
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)
# gram:dep:spdlog end
# --- end gram dependencies ---

# --- Gram Engine ---
add_library(GramRenderer STATIC
    src/Gram/Renderer/VulkanDevice.cpp
    src/Gram/Renderer/VulkanFrame.cpp
    src/Gram/Renderer/VulkanImGui.cpp
    src/Gram/Renderer/VulkanInstance.cpp
    src/Gram/Renderer/VulkanPipeline.cpp
    src/Gram/Renderer/VulkanRenderer.cpp
    src/Gram/Renderer/VulkanShader.cpp
    src/Gram/Renderer/VulkanSurface.cpp
    src/Gram/Renderer/VulkanSwapchain.cpp
)
target_include_directories(GramRenderer PUBLIC include)
target_link_libraries(GramRenderer PUBLIC glfw Vulkan::Vulkan glm::glm imgui::imgui spdlog::spdlog)
target_precompile_headers(GramRenderer PUBLIC include/Gram/Renderer/Common.hpp)

add_library(GramScripting STATIC
    src/Gram/Scripting/ECS/Component.cpp
    src/Gram/Scripting/HostAPI.cpp
)
target_include_directories(GramScripting PUBLIC include)
target_link_libraries(GramScripting PUBLIC glm::glm imgui::imgui spdlog::spdlog)
target_precompile_headers(GramScripting PUBLIC include/Gram/Scripting/Common.hpp)

# The engine as one target for runtime/editor modules: PUBLIC so whatever links GramEngine also
# gets its headers and the component libraries (the game's Runtime/src/main.cpp includes
# Gram/EntryPoint.hpp, GLFWApplication and VulkanRenderer directly).
add_library(GramEngine STATIC
    src/Gram/Application/GLFWApplication.cpp
    src/Gram/Logging/GramLogger.cpp
    src/Gram/Platform.cpp
)
target_include_directories(GramEngine PUBLIC include)
target_link_libraries(GramEngine PUBLIC GramScripting GramRenderer)
target_precompile_headers(GramEngine PUBLIC include/Gram/Common.hpp)