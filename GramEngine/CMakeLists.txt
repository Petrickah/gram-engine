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
# --- end gram dependencies ---

add_executable(GramEngine src/main.cpp)
target_link_libraries(GramEngine PRIVATE Vulkan::Vulkan glfw)
