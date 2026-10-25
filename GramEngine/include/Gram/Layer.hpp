#pragma once

#include "Gram/Common.hpp"
#include "Gram/Logging/GramLogger.hpp"

#include <concepts>

GRAM_NAMESPACE_BEGIN

template<class GramObject>
class Layer: private UniqueObject
{
public:
    virtual ~Layer() = default;
    Layer() = default;

    virtual bool OnStart() = 0;
    virtual bool OnDrawFrame() = 0;
    virtual bool OnFinish() = 0;
    virtual bool OnStop() = 0;
};

template<class LayerType, class GramObject>
concept LayerClass = std::derived_from<LayerType, Layer<GramObject>> && (!std::same_as<LayerType, Layer<GramObject>>);

template<class GramObject>
class LayerOwner: private UniqueObject
{
public:
    LayerOwner() = default;
    virtual ~LayerOwner();
    virtual bool Run() = 0;
    
    bool DrawFrame();
    bool Finish();
    
    template<class LayerType> requires LayerClass<LayerType, GramObject>
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
    
    template<class LayerType, class... Args> requires LayerClass<LayerType, GramObject>
    bool RegisterLayer(Args&&... args)
    {
        GramCheck(Get<LayerType>() == nullptr, "layer already registered");

        auto layer = std::make_shared<LayerType>(std::forward<Args>(args)...);
        if (layer->OnStart())
        {
            m_Layers.push_back(layer);
            return true;
        }

        return false;
    }
    
    template<class LayerType> requires LayerClass<LayerType, GramObject>
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
    void UnregisterAll();

private:
    std::vector<std::shared_ptr<Layer<GramObject>>> m_Layers;
};

GRAM_NAMESPACE_END

template<class GramObject>
inline Gram::LayerOwner<GramObject>::~LayerOwner()
{
    UnregisterAll();
}

template<class GramObject>
inline void Gram::LayerOwner<GramObject>::UnregisterAll()
{
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (*it) (*it)->OnStop();
    }

    m_Layers.clear();
}

template<class GramObject>
inline bool Gram::LayerOwner<GramObject>::DrawFrame()
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

template<class GramObject>
inline bool Gram::LayerOwner<GramObject>::Finish()
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