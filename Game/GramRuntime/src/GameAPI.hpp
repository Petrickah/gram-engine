#pragma once

GRAM_NAMESPACE_BEGIN

struct Greeting;

struct GameAPI
{
    uint32_t Version;
    const GameAPI* (*OnLoad)(const Gram::HostAPI* host);
    void (*OnUnload)();
    void (*Greet)(Greeting* greeting);
};

inline constexpr uint32_t GameAPIVersion = 1;
using GetGameAPIFunction = const GameAPI* (*)();

GRAM_NAMESPACE_END