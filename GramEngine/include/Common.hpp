#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef GRAM_PLATFORM_APPLE
#ifdef __APPLE__
#define GRAM_PLATFORM_APPLE
#endif // #ifdef __APPLE__
#endif // #ifndef GRAM_PLATFORM_APPLE

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

#include <glm/vec2.hpp>

#define GRAM_NAMESPACE_BEGIN namespace Gram {
#define GRAM_NAMESPACE_END }
