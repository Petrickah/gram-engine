#pragma once

#include "Gram/Logging/GramLogger.hpp"

GRAM_NAMESPACE_BEGIN

template<class GramObject>
class Layer: private UniqueObject
{
public:
    virtual ~Layer() = default;
    Layer() = default;

    virtual bool OnStart() { return true; };
    virtual bool OnDrawFrame() { return true; };
    virtual bool OnFinish() { return true; };
    virtual bool OnStop() { return true; };
};

template<class GramObject>
class LayerOwner: private UniqueObject
{
public:
    LayerOwner() = default;
    virtual ~LayerOwner()
    {
        UnregisterAll();
    }
    
    virtual bool DrawFrame()
    {
        for (std::shared_ptr<Layer<GramObject>>& layer: m_Layers)
        {
            if (!layer || !layer->OnDrawFrame())
            {
                return false;
            }
        }

        return true;
    }

    virtual bool Finish()
    {
        for (std::shared_ptr<Layer<GramObject>>& layer: m_Layers)
        {
            if (!layer || !layer->OnFinish())
            {
                return false;
            }
        }

        return true;
    }
    
    template<class LayerType> 
        requires std::derived_from<LayerType, Layer<GramObject>>
        && (!std::same_as<LayerType, Layer<GramObject>>)
    std::shared_ptr<LayerType> Get()
    {
        for (std::shared_ptr<Layer<GramObject>>& layer: m_Layers)
        {
            if (auto typed = std::dynamic_pointer_cast<LayerType, Layer<GramObject>>(layer))
            {
                return typed;
            }
        }
        
        return nullptr;
    }
    
    template<class LayerType, class... Args>
        requires std::derived_from<LayerType, Layer<GramObject>>
        && (!std::same_as<LayerType, Layer<GramObject>>)
    bool RegisterLayer(std::shared_ptr<LayerType>& layer, Args&&... args)
    {
        GramCheck(Get<LayerType>() == nullptr, "layer already registered");

        layer = std::make_shared<LayerType>(std::forward<Args>(args)...);
        if (layer->OnStart())
        {
            m_Layers.push_back(layer);
            return true;
        }

        return false;
    }
    
    template<class LayerType>
        requires std::derived_from<LayerType, Layer<GramObject>>
        && (!std::same_as<LayerType, Layer<GramObject>>)
    bool UnregisterLayer()
    {
        for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
        {
            if (auto typed = std::dynamic_pointer_cast<LayerType, Layer<GramObject>>(*it))
            {
                m_Layers.erase(std::next(it).base());
                return typed->OnStop();
            }
        }

        return false;
    }

protected:
    void UnregisterAll()
    {
        for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
        {
            if (*it) (*it)->OnStop();
        }

        m_Layers.clear();
    }

private:
    std::vector<std::shared_ptr<Layer<GramObject>>> m_Layers;
};

GRAM_NAMESPACE_END