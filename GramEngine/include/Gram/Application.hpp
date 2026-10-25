#pragma once

#include "Gram/Logging/GramLogger.hpp"
#include "Gram/Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class Application
{
public:
    Application() {};
    
    // Derived classes delete the renderer in their own destructor, before the window goes away.
    // virtual ~Application() { delete m_Renderer; };

    // Takes ownership of the renderer.
    // void SetRenderer(Renderer* renderer) { m_Renderer = renderer; }

protected:
    // Renderer* m_Renderer = nullptr;
};

GRAM_NAMESPACE_END