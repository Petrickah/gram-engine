#pragma once

#ifdef GRAM_GENERATED_CODE
GRAM_NAMESPACE_BEGIN

struct Greeting {
    char Text[64];
};

static_assert(sizeof(Greeting) == 64);
static_assert(offsetof(Greeting, Text) == 0);

inline const Gram::FieldDescriptor GreetingFields[] =
{
    { "Text", Gram::FieldDescriptor::FieldType::Str, offsetof(Greeting, Text), sizeof(Greeting::Text) }
};

inline const ComponentDescriptor GreetingDescriptor =
{
    .Name   = "Greeting",
    .ID     = ComponentIDFromName("Greeting"),
    .Fields = GreetingFields,
    .Count  = std::size(GreetingFields),
    .Size   = sizeof(Greeting)
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_GENERATED_CODE