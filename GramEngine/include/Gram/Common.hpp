#pragma once

// Everything a Gram source file usually needs in one include: the foundation plus
// the engine's base types. The headers below never includes this file:
// Common.hpp includes them, so including it back would make a cycle that only compiles
// when Common.hpp happens to be the first include.

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#if !defined(GRAM_CONFIG_RELEASE) && !defined(GRAM_CONFIG_FINAL)
#define GRAM_CONFIG_DEBUG
#endif // #if !defined(GRAM_CONFIG_RELEASE) && !defined(GRAM_CONFIG_FINAL)

#ifdef GRAM_CONFIG_DEBUG
#define GRAM_VULKAN_VALIDATION
#define GramValidationOnly(...) __VA_ARGS__
#else
#define GramValidationOnly(...)
#endif // #ifdef GRAM_CONFIG_DEBUG

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
#include <vulkan/vulkan.hpp>
#endif // #ifndef GRAM_VULKAN_RENDERER

#ifdef GRAM_VULKAN_RENDERER
#define GramVulkanOnly(...) __VA_ARGS__
#else
#define GramVulkanOnly(...)
#endif // #ifndef GRAM_VULKAN_RENDERER

#ifdef GRAM_VULKAN_RENDERER
// GLFW_INCLUDE_VULKAN only adds <vulkan/vulkan.h>; it does not stop glfw3.h
// from also pulling in <GL/gl.h>. GLFW_INCLUDE_NONE does.
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif // #ifdef GRAM_VULKAN_RENDERER

#ifndef GRAM_PLATFORM_APPLE
#ifndef GRAM_GEOMETRY_SHADERS
#define GRAM_GEOMETRY_SHADERS
#endif // #ifndef GRAM_GEOMETRY_SHADERS
#endif // #ifndef GRAM_PLATFORM_APPLE

// GRAM_IMGUI_ENABLED allows to render ImGui panels using the Vulkan Renderer;
#if defined(GRAM_VULKAN_RENDERER) && !defined(GRAM_CONFIG_FINAL)
#define GRAM_IMGUI_ENABLED
#endif // #if defined(GRAM_VULKAN_RENDERER) && !defined(GRAM_CONFIG_FINAL)

#ifdef GRAM_IMGUI_ENABLED
#include <imgui.h>
#define GramImGuiOnly(...) __VA_ARGS__
#else
#define GramImGuiOnly(...)
#endif // #ifdef GRAM_IMGUI_ENABLED

#ifndef GRAM_LOGGER_ENABLED
#define GRAM_LOGGER_ENABLED
#endif // #ifndef GRAM_LOGGER_ENABLED

#include <glm/glm.hpp>

#define GRAM_NAMESPACE_BEGIN namespace Gram {
#define GRAM_NAMESPACE_END }

GRAM_NAMESPACE_BEGIN
// Owns Vulkan resources at a fixed address (members of VulkanRenderer): neither copyable nor
// movable. Declaring the copy operations, even deleted, also suppresses the implicit moves.
struct UniqueObject
{
    UniqueObject() = default;
    UniqueObject(const UniqueObject&) = delete;
    UniqueObject& operator=(const UniqueObject&) = delete;
};

// Move-only. The defaulted moves are only safe when every member owns itself (vk::Unique*
// handles, std containers) — a raw vk:: handle would be copied and then destroyed twice.
// Don't combine with UniqueObject: a derived class can only move if all its bases can.
struct MoveObject
{
    MoveObject() = default;
    MoveObject(const MoveObject&) = delete;
    MoveObject& operator=(const MoveObject&) = delete;
    MoveObject(MoveObject&&) noexcept = default;
    MoveObject& operator=(MoveObject&&) noexcept = default;
};

GRAM_NAMESPACE_END

#include "Gram/Application.hpp"
#include "Gram/Layer.hpp"
#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Platform.hpp"
#include "Gram/Renderer.hpp"

GRAM_NAMESPACE_BEGIN
#ifdef GRAM_IMGUI_ENABLED
struct ImGuiApplicationLayer : public Gram::Layer<ImGuiApplicationLayer> {};
#endif // #ifdef GRAM_IMGUI_ENABLED
GRAM_NAMESPACE_END