#pragma once

GRAM_NAMESPACE_BEGIN

struct Greeting
{
    char Text[64];
};

static_assert(sizeof(Greeting) == 64);
extern "C" void gram_core_greet(Greeting* greeting);
extern "C" const ComponentDescriptor GreetingDescriptor;

GRAM_NAMESPACE_END