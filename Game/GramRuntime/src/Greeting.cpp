#include "Gram/Common.hpp"
#include "Greeting.hpp"

GRAM_NAMESPACE_BEGIN

const FieldDescriptor GreetingFields[] =
{
    { "Text", FieldDescriptor::FieldType::Str, offsetof(Greeting, Text), sizeof(Greeting::Text) }
};

const ComponentDescriptor GreetingDescriptor =
{
    "Greeting", GreetingFields, std::size(GreetingFields), sizeof(Greeting)
};

GRAM_NAMESPACE_END