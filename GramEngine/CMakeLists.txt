cmake_minimum_required(VERSION 3.20)
project(GramEngine)

include(${CMAKE_CURRENT_LIST_DIR}/GramShaders.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/GramTextures.cmake)

# Pinned explicitly: Apple clang still defaults to C++14. C++20 for designated initializers
# (vk::...CreateInfo{ .field = ... }), the style the Khronos Vulkan tutorial uses.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# How vulkan.hpp is compiled, for every translation unit that includes it: the engine's and
# whatever links it (the game's Runtime includes engine headers with inline Vulkan code). Set on
# the targets as PUBLIC below, not with add_compile_definitions, which only reaches targets in this
# directory: the game, added from Game/Runtime, compiled vulkan.hpp without them.
# - VULKAN_HPP_NO_CONSTRUCTORS: vk:: structs become aggregates, so designated initializers work.
# - VULKAN_HPP_NO_EXCEPTIONS: errors come back as vk::Result; the enhanced calls (createShaderModuleUnique
#   & co., mapMemory) return a vk::ResultValue{ result, value } to check.
set(GRAM_VULKAN_HPP_DEFINITIONS
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)

set(GRAM_CONFIG_DEFINITIONS
    $<$<CONFIG:Debug>:GRAM_CONFIG_DEBUG>
    $<$<CONFIG:Release>:GRAM_CONFIG_RELEASE>
    $<$<CONFIG:Final>:GRAM_CONFIG_FINAL>)

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
# gram:dep:stb begin
find_package(Stb REQUIRED)
# gram:dep:stb end
# --- end gram dependencies ---

# --- Gram Engine ---
add_library(GramRenderer STATIC
    src/Gram/Renderer/VulkanBuffer.cpp
    src/Gram/Renderer/VulkanDevice.cpp
    src/Gram/Renderer/VulkanFrame.cpp
    src/Gram/Renderer/VulkanImage.cpp
    src/Gram/Renderer/VulkanImGui.cpp
    src/Gram/Renderer/VulkanInstance.cpp
    src/Gram/Renderer/VulkanPipeline.cpp
    src/Gram/Renderer/VulkanPresentPass.cpp
    src/Gram/Renderer/VulkanRenderer.cpp
    src/Gram/Renderer/VulkanRenderTarget.cpp
    src/Gram/Renderer/VulkanShader.cpp
    src/Gram/Renderer/VulkanSurface.cpp
    src/Gram/Renderer/VulkanSwapchain.cpp
)
target_include_directories(GramRenderer PUBLIC include)
# stb_image is an implementation detail of VulkanImage.cpp (its only user): kept out of the public headers.
target_include_directories(GramRenderer PRIVATE ${Stb_INCLUDE_DIR})
target_compile_definitions(GramRenderer PUBLIC ${GRAM_VULKAN_HPP_DEFINITIONS} ${GRAM_CONFIG_DEFINITIONS})
target_link_libraries(GramRenderer PUBLIC glfw Vulkan::Vulkan glm::glm imgui::imgui spdlog::spdlog)
target_precompile_headers(GramRenderer PUBLIC include/Gram/Renderer/Common.hpp)

add_library(GramScripting STATIC
    src/Gram/Scripting/ECS/Component.cpp
    src/Gram/Scripting/ECS/System.cpp
    src/Gram/Scripting/ECS/World.cpp
    src/Gram/Scripting/HostAPI.cpp
    src/Gram/Scripting/GameAPI.cpp
)
target_include_directories(GramScripting PUBLIC include)
target_compile_definitions(GramScripting PUBLIC ${GRAM_VULKAN_HPP_DEFINITIONS} ${GRAM_CONFIG_DEFINITIONS})
target_link_libraries(GramScripting PUBLIC glm::glm imgui::imgui spdlog::spdlog)
target_precompile_headers(GramScripting PUBLIC include/Gram/Scripting/Common.hpp)

# The engine as one target for runtime/editor modules: PUBLIC so whatever links GramEngine also
# gets its headers and the component libraries (the game's Runtime/src/main.cpp includes
# Gram/EntryPoint.hpp, GLFWApplication and VulkanRenderer directly).
add_library(GramEngine STATIC
    src/Gram/Application/GLFWApplication.cpp
    src/Gram/Editor/Editor.cpp
    src/Gram/Editor/EditorToolbar.cpp
    src/Gram/Editor/InspectorPanel.cpp
    src/Gram/Editor/LoggerPanel.cpp
    src/Gram/Editor/OutlinerPanel.cpp
    src/Gram/Editor/SystemsPanel.cpp
    src/Gram/Editor/ViewportPanel.cpp
    src/Gram/Logging/GramLogger.cpp
    src/Gram/Platform.cpp
)
target_include_directories(GramEngine PUBLIC include)
target_compile_definitions(GramEngine PUBLIC ${GRAM_VULKAN_HPP_DEFINITIONS} ${GRAM_CONFIG_DEFINITIONS})
target_link_libraries(GramEngine PUBLIC GramScripting GramRenderer)
target_precompile_headers(GramEngine PUBLIC include/Gram/Common.hpp)