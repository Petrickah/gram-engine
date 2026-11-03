#pragma once

GRAM_NAMESPACE_BEGIN

using ComponentID = uint64_t;
static_assert(sizeof(ComponentID) == 8);

// A component's ID is the 64-bit FNV-1a hash of its name: stable (it doesn't depend on the order
// components are registered in, so saved data keeps referring to the right type) and computed
// the same way, at compile time, on the Rust side.
constexpr ComponentID ComponentIDFromName(std::string_view name)
{
    ComponentID hash = 0xcbf29ce484222325ull;
    for (const char c : name)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static_assert(ComponentIDFromName("Greeting") == 0xe18e41722408a7d6ull);

struct FieldDescriptor
{
public:
    enum class FieldType
    {
        Str, F32, I32, U32, Bool
    };

public:
    char Name[64];
    FieldType Type;
    size_t Offset;
    size_t Size;
};

struct ComponentDescriptor
{
    char Name[64];
    ComponentID ID;
    const FieldDescriptor* Fields;
    size_t Count;
    size_t Size;
};

GRAM_NAMESPACE_END