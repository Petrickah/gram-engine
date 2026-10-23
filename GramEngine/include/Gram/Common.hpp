#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef __APPLE__
#ifndef GRAM_PLATFORM_APPLE
#define GRAM_PLATFORM_APPLE
#endif // #ifndef GRAM_PLATFORM_APPLE
#endif // #ifdef __APPLE__

// Variadic so the argument may carry its own trailing comma, which an initializer list
// needs to disappear with it: { "a", GramAppleOnly("b",) }
#ifdef GRAM_PLATFORM_APPLE
#define GramAppleOnly(...) __VA_ARGS__
#else
#define GramAppleOnly(...)
#endif // #ifdef GRAM_PLATFORM_APPLE

#ifndef GRAM_VULKAN_RENDERER
#define GRAM_VULKAN_RENDERER
#endif // #ifndef GRAM_VULKAN_RENDERER

#ifdef GRAM_VULKAN_RENDERER
// GLFW_INCLUDE_VULKAN only adds <vulkan/vulkan.h>; it does not stop glfw3.h
// from also pulling in <GL/gl.h>. GLFW_INCLUDE_NONE does.
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#endif // #ifdef GRAM_VULKAN_RENDERER

#ifndef GRAM_PLATFORM_APPLE
#ifndef GRAM_GEOMETRY_SHADERS
#define GRAM_GEOMETRY_SHADERS
#endif // #ifndef GRAM_GEOMETRY_SHADERS
#endif // #ifndef GRAM_PLATFORM_APPLE

#ifdef GRAM_VULKAN_RENDERER
// GRAM_IMGUI_ENABLED allows to render ImGui panels using the Vulkan Renderer;
#ifndef GRAM_IMGUI_ENABLED
#define GRAM_IMGUI_ENABLED
#endif // #ifndef GRAM_IMGUI_ENABLED
#endif // #ifdef GRAM_VULKAN_RENDERER

#ifndef GRAM_LOGGER_ENABLED
#define GRAM_LOGGER_ENABLED
#endif // #ifndef GRAM_LOGGER_ENABLED

#include <glm/vec2.hpp>

#define GRAM_NAMESPACE_BEGIN namespace Gram {
#define GRAM_NAMESPACE_END }
