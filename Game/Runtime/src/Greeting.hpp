#pragma once

GRAM_NAMESPACE_BEGIN

struct Greeting : public Gram::Component
{
    char Text[64];
};

static_assert(sizeof(Greeting) == 64);

extern "C" const ComponentDescriptor GreetingDescriptor;

GRAM_NAMESPACE_END