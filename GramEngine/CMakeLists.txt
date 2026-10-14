cmake_minimum_required(VERSION 3.20)
project(GramEngine)

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

add_executable(GramEngine src/main.cpp src/Gram/GLFWApplication.cpp src/Gram/VulkanRenderer.cpp)
target_include_directories(GramEngine PRIVATE include)
target_link_libraries(GramEngine PRIVATE Vulkan::Vulkan glm::glm glfw)
