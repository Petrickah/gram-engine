#pragma once

#include "Gram/Common.hpp"

GRAM_NAMESPACE_BEGIN

class Renderer
{
public:
    Renderer() = default;
    virtual ~Renderer() {};

public:
    virtual bool DrawFrame() = 0;
    virtual bool Finish() = 0;

    // The window's framebuffer changed size (including to or from zero when minimized).
    virtual void OnResize() {}
};

GRAM_NAMESPACE_END
