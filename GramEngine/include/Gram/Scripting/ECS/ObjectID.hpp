#pragma once

GRAM_NAMESPACE_BEGIN

struct ObjectID
{
    uint32_t Index;
    uint32_t Generation;
};

static_assert(sizeof(ObjectID) == 8);

inline constexpr ObjectID InvalidObjectID { .Index = UINT32_MAX, .Generation = UINT32_MAX };

GRAM_NAMESPACE_END