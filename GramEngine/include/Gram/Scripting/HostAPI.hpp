#pragma once

#include "Gram/Scripting/ECS/Component.hpp"
#include "Gram/Scripting/ECS/System.hpp"
#include "Gram/Scripting/ECS/ObjectID.hpp"

GRAM_NAMESPACE_BEGIN

// What the engine offers a loaded game, filled by GameAPILoader. Written twice by hand (here and in
// gram-core's interop/host.rs): field order is the binary contract, checked by the size asserts.
struct HostAPI
{
    void *Host;
    void (*Log)(LogLevel level, const char* message);
    void (*RegisterSystem)(void* host, const System::Descriptor* desc);
    void *(*GetComponent)(void* host, ObjectID entity, ComponentID component);
    // Iteration over every component of one type, by position in its storage (0 .. count - 1, storage
    // order): GetComponentAt returns its address and writes its entity to *entity, nullptr past the end.
    // Like GetComponent, an address is only valid during the call that asked for it.
    uint32_t (*GetComponentCount)(void* host, ComponentID component);
    void *(*GetComponentAt)(void* host, ComponentID component, uint32_t position, ObjectID* entity);
};

static_assert(sizeof(HostAPI) == 6 * sizeof(void*));

GRAM_NAMESPACE_END