#pragma once

#include "Gram/Scripting/ECS/Component.hpp"
#include "Gram/Scripting/ECS/System.hpp"
#include "Gram/Scripting/ECS/ObjectID.hpp"

GRAM_NAMESPACE_BEGIN

struct HostAPI
{
    void *Host;
    void (*Log)(LogLevel level, const char* message);
    void (*RegisterSystem)(void* host, const System::Descriptor* desc);
    void *(*GetComponent)(void* host, ObjectID entity, ComponentID component);
};

GRAM_NAMESPACE_END