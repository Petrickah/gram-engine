cmake_minimum_required(VERSION 3.20)
project(GramEngine)

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
add_library(GramLogger STATIC
    src/Gram/Logging/GramLogger.cpp
)
target_include_directories(GramLogger PUBLIC include)
target_link_libraries(GramLogger PUBLIC Vulkan::Vulkan spdlog::spdlog glm::glm)
target_precompile_headers(GramLogger PUBLIC include/Gram/Common.hpp)

add_library(GramGLFW STATIC
    src/Gram/Application/GLFWApplication.cpp
)
target_include_directories(GramGLFW PUBLIC include)
target_link_libraries(GramGLFW PUBLIC glfw GramLogger)
target_precompile_headers(GramGLFW PUBLIC include/Gram/Common.hpp)

add_library(GramRenderer STATIC
    src/Gram/Renderer/VulkanRenderer.cpp
    src/Gram/Renderer/VulkanDevice.cpp
    src/Gram/Renderer/VulkanInstance.cpp
    src/Gram/Renderer/VulkanSurface.cpp
    src/Gram/Renderer/VulkanSwapchain.cpp
    src/Gram/Renderer/VulkanPipeline.cpp
    src/Gram/Renderer/VulkanShader.cpp
    src/Gram/Renderer/VulkanFrame.cpp
    src/Gram/Renderer/VulkanImGui.cpp
)
target_include_directories(GramRenderer PUBLIC include)
target_link_libraries(GramRenderer PUBLIC Vulkan::Vulkan glm::glm imgui::imgui GramLogger)
target_precompile_headers(GramRenderer PUBLIC include/Gram/Renderer/Common.hpp)

# The engine as one target for runtime/editor modules: PUBLIC so whatever links GramEngine also
# gets its headers and the component libraries (GramRuntime's main.cpp includes
# Gram/EntryPoint.hpp, GLFWApplication and VulkanRenderer directly).
add_library(GramEngine STATIC
    src/Gram/Platform.cpp
)
target_include_directories(GramEngine PUBLIC include)
target_link_libraries(GramEngine PUBLIC GramGLFW GramRenderer GramLogger)
target_precompile_headers(GramEngine PUBLIC include/Gram/Common.hpp)

# --- shaders ---
# HLSL -> SPIR-V at build time with the Vulkan SDK's dxc (the SDK ships it on Windows, Linux
# and macOS; vcpkg's directx-dxc port has no macOS support). SPIR-V is platform-independent, so
# the host's dxc is the right one even when cross-compiling for Windows. Output lands in
# <top-level build dir>/shaders/, which gram copies next to the built executable.
# Stopgap until gram grows a dedicated shader module kind.
# Plain find_program instead of FindVulkan's dxc component: that one also requires the dxcompiler
# library for the *target* platform, which a mingw cross-build doesn't have (and doesn't need).
# NO_CMAKE_FIND_ROOT_PATH keeps the search on the host when cross-compiling.
find_program(GRAM_DXC_EXECUTABLE dxc
    HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)

# CMAKE_BINARY_DIR, not CMAKE_CURRENT_BINARY_DIR: when a runtime module pulls GramEngine in with
# add_subdirectory, the shaders must still land in the *top-level* build dir, where gram looks.
set(GRAM_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
set(GRAM_SHADER_OUTPUTS)

# One .hlsl file holds both stages (VSMain/PSMain), so it compiles to <name>.vert.spv and <name>.frag.spv.
function(gram_add_hlsl_shader name)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.hlsl)
    set(vert ${GRAM_SHADER_DIR}/${name}.vert.spv)
    set(frag ${GRAM_SHADER_DIR}/${name}.frag.spv)
    add_custom_command(
        OUTPUT ${vert} ${frag}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GRAM_SHADER_DIR}
        COMMAND ${GRAM_DXC_EXECUTABLE} -spirv -T vs_6_0 -E VSMain ${source} -Fo ${vert}
        COMMAND ${GRAM_DXC_EXECUTABLE} -spirv -T ps_6_0 -E PSMain ${source} -Fo ${frag}
        DEPENDS ${source}
        COMMENT "Compiling shader ${name}.hlsl to SPIR-V"
        VERBATIM
    )
    set(GRAM_SHADER_OUTPUTS ${GRAM_SHADER_OUTPUTS} ${vert} ${frag} PARENT_SCOPE)
endfunction()

gram_add_hlsl_shader(Default)

add_custom_target(GramShaders DEPENDS ${GRAM_SHADER_OUTPUTS})
add_dependencies(GramEngine GramShaders)
