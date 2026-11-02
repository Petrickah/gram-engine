#pragma once

#include "Gram/Scripting/ECS/ObjectID.hpp"

GRAM_NAMESPACE_BEGIN

class System
{
public:
    enum class Stage : uint32_t
    {
        Update = 0,
        Event  = 1
    };

    struct Descriptor
    {
        char Name[64];
        System::Stage Stage;
        char Event[64];
        uint32_t Index;
    };

    struct Context
    {
        float DeltaTime;
        double Time;
        ObjectID EventTarget;
    };
};

static_assert(sizeof(System::Stage) == 4);
static_assert(sizeof(System::Descriptor) == 136);
static_assert(sizeof(System::Context) == 24);

GRAM_NAMESPACE_END
