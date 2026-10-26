#pragma once

GRAM_NAMESPACE_BEGIN

class Renderer
{
public:
    Renderer() = default;
    virtual ~Renderer() {};

public:
    // The window's framebuffer changed size (including to or from zero when minimized).
    virtual void OnResize() { }
};

GRAM_NAMESPACE_END
