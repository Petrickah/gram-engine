#pragma once

GRAM_NAMESPACE_BEGIN

struct Component {};

struct FieldDescriptor
{
public:
    enum class FieldType
    {
        Unknown = 0,
        Str,
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
    const FieldDescriptor* Fields;
    size_t Count;
    size_t Size;
};

GRAM_NAMESPACE_END