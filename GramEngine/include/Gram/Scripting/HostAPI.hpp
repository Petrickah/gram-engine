#pragma once

#include "Gram/Scripting/ECS/System.hpp"

GRAM_NAMESPACE_BEGIN

struct HostAPI
{
    void *Host;
    void (*Log)(LogLevel level, const char* message);
    void (*RegisterSystem)(void* host, const System::Descriptor* desc);
};

GRAM_NAMESPACE_END