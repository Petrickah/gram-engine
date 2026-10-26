#pragma once

// Two separate jobs live in this header, kept apart on purpose:
//  1. Logging (GramInfo, GramVulkanError, ...) only prints. It never changes control flow,
//     and compiles to nothing without GRAM_LOGGER_ENABLED.
//  2. Checks (GramCheck, GramVulkanCheck) return false from the enclosing function on
//     failure. They always exist: turning logging off must not change what the engine does.

// Variadic so the argument may carry its own trailing comma, which a designated
// initializer or an initializer list needs to disappear with it: GramLoggerOnly(.x = 1,)
#ifdef GRAM_LOGGER_ENABLED
#define GramLoggerOnly(...) __VA_ARGS__
#else
#define GramLoggerOnly(...)
#endif // #ifdef GRAM_LOGGER_ENABLED

#ifdef GRAM_LOGGER_ENABLED
#include <spdlog/spdlog.h>

GRAM_NAMESPACE_BEGIN

// Creates the loggers; with loggingEnabled = false they exist but drop everything.
// Logging before this runs (or without it) is safe and prints nothing.
void LoggerEntryPoint(bool loggingEnabled = false);

// nullptr until LoggerEntryPoint has run.
spdlog::logger* GetGramLogger();
spdlog::logger* GetVulkanLogger();
spdlog::logger* GetVulkanListLogger();

GRAM_NAMESPACE_END

// With arguments, the message is an fmt format string and must be a literal:
// GramInfo("window {}x{}", width, height). A single argument is printed as is, unformatted,
// so a runtime string works on its own: GramInfo(text).
#define GramLog_(getLogger, level, ...) do { if (spdlog::logger* gramLogger_ = Gram::getLogger()) gramLogger_->level(__VA_ARGS__); } while (0)
#else
#define GramLog_(getLogger, level, ...) do { } while (0)
#endif // #ifdef GRAM_LOGGER_ENABLED

#define GramTrace(...)         GramLog_(GetGramLogger, trace, __VA_ARGS__)
#define GramInfo(...)          GramLog_(GetGramLogger, info,  __VA_ARGS__)
#define GramWarning(...)       GramLog_(GetGramLogger, warn,  __VA_ARGS__)
#define GramError(...)         GramLog_(GetGramLogger, error, __VA_ARGS__)

#define GramVulkanTrace(...)   GramLog_(GetVulkanLogger, trace, __VA_ARGS__)
#define GramVulkanInfo(...)    GramLog_(GetVulkanLogger, info,  __VA_ARGS__)
#define GramVulkanWarning(...) GramLog_(GetVulkanLogger, warn,  __VA_ARGS__)
#define GramVulkanError(...)   GramLog_(GetVulkanLogger, error, __VA_ARGS__)

// When expression isn't true, logs message plus any arguments and leaves the enclosing function.
// The expression is evaluated exactly once. The variants differ only in what they return,
// matching the enclosing function's return type
#define GramCheck_(expression, onFailure, ...) do { \
    if (!(expression))                              \
    {                                               \
        GramError(__VA_ARGS__);                     \
        onFailure;                                  \
    }                                               \
} while (0)
// Returns false from the enclosing function when expression is false.
#define GramCheck(expression, ...)     GramCheck_(expression, return false, __VA_ARGS__)
// Returns nothing from the enclosing function when expression is false.
#define GramCheckNull(expression, ...) GramCheck_(expression, return nullptr, __VA_ARGS__)
// Returns nothing from the enclosing function when expression is false.
#define GramCheckVoid(expression, ...) GramCheck_(expression, return, __VA_ARGS__)

#ifdef GRAM_VULKAN_RENDERER
#include <vulkan/vulkan.hpp>

// List items, named by what the item is; the level behind each one picks its marker.
// Introduce the list with a regular GramVulkanInfo line. Items of a nested list name their
// parent in the message instead of being indented: GramVulkanListSelected("{}: {}", device, ext)
#define GramVulkanListAvailable(...) GramLog_(GetVulkanListLogger, trace, __VA_ARGS__) // - [-] offered, not chosen
#define GramVulkanListAdded(...)     GramLog_(GetVulkanListLogger, debug, __VA_ARGS__) // - [+] enabled without being requested
#define GramVulkanListSelected(...)  GramLog_(GetVulkanListLogger, info,  __VA_ARGS__) // - [X] requested / chosen
#define GramVulkanListMissing(...)   GramLog_(GetVulkanListLogger, warn,  __VA_ARGS__) // - [!] requested, not offered

// The common two-state item: "- [X]" when selected is true, "- [-]" otherwise.
// selected is evaluated once; only one branch runs, so the message is formatted once too.
#define GramVulkanListItem(selected, ...) do {                                      \
    if (selected) GramVulkanListSelected(__VA_ARGS__);                              \
    else          GramVulkanListAvailable(__VA_ARGS__);                             \
} while (0)

// When expression (a vk::Result) isn't eSuccess, logs message plus the result's name and leaves
// the enclosing function. The expression is evaluated exactly once. The variants differ only in
// what they return, matching the enclosing function's return type.
#define GramVulkanCheck_(expression, message, onFailure) do {                       \
    const vk::Result gramResult_ = (expression);                                    \
    if (gramResult_ != vk::Result::eSuccess)                                        \
    {                                                                               \
        GramVulkanError("{}: {}", message, vk::to_string(gramResult_));             \
        onFailure;                                                                  \
    }                                                                               \
} while (0)

// bool functions: returns false.
#define GramVulkanCheck(expression, message)       GramVulkanCheck_(expression, message, return false)
// Constructors and void functions: plain return.
#define GramVulkanCheckVoid(expression, message)   GramVulkanCheck_(expression, message, return)
// vk::Result functions: returns the failing result, so the caller sees the real error.
#define GramVulkanCheckResult(expression, message) GramVulkanCheck_(expression, message, return gramResult_)
#endif // #ifdef GRAM_VULKAN_RENDERER
