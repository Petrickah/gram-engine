#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/ECS/Component.hpp"

#include "Greeting.hpp"

GRAM_NAMESPACE_BEGIN

const Gram::FieldDescriptor GreetingFields[] =
{
    { "Text", Gram::FieldDescriptor::FieldType::Str, offsetof(Greeting, Text), sizeof(Greeting::Text) }
};

const ComponentDescriptor GreetingDescriptor =
{
    "Greeting", GreetingFields, std::size(GreetingFields), sizeof(Greeting)
};

GRAM_NAMESPACE_END