#pragma once

GRAM_NAMESPACE_BEGIN

class Application
{
public:
    Application() = default;
    virtual bool Run() = 0;
};

GRAM_NAMESPACE_END