cmake_minimum_required(VERSION 3.20)
project(GramEngine)

# Apple clang still defaults to C++14 (GCC defaults to gnu++17), so std::optional & co.
# need the standard pinned explicitly.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# --- end gram dependencies ---

add_library(GramGLFW STATIC
    src/Gram/GLFWApplication.cpp
)
target_include_directories(GramGLFW PUBLIC include)
target_link_libraries(GramGLFW PUBLIC glfw)

add_library(GramRenderer STATIC
    src/Gram/Renderer/VulkanDevice.cpp
    src/Gram/Renderer/VulkanInstance.cpp
    src/Gram/Renderer/VulkanSurface.cpp
    src/Gram/Renderer/VulkanSwapchain.cpp
    src/Gram/Renderer/VulkanShader.cpp
)
target_include_directories(GramRenderer PUBLIC include)
target_link_libraries(GramRenderer PUBLIC Vulkan::Vulkan glm::glm)

add_executable(GramEngine src/main.cpp)
target_include_directories(GramEngine PRIVATE include)
target_link_libraries(GramEngine PRIVATE GramGLFW GramRenderer)
