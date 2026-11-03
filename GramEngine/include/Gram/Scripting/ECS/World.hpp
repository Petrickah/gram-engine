#pragma once

GRAM_NAMESPACE_BEGIN

class GLFWApplication;

class World : public Layer<GLFWApplication>
{
public:
    struct Slot { uint32_t Generation = 0; bool Alive = false; };
    struct ComponentStorage
    {
        const ComponentDescriptor*             Descriptor;
        std::vector<std::byte>                 Data;     // all the components one after another, at Descriptor->Size octets each
        std::vector<uint32_t>                  Owners;   // Owners[i] = index of the entity that owns this component
        std::unordered_map<uint32_t, uint32_t> SlotOf;   // map between the entity index and position i
    };
    
public:
    ObjectID CreateEntity();
    void DestroyEntity(const ObjectID& entity);
    bool IsAlive(const ObjectID& entity) const;

    void* AddComponent(ObjectID entity, const ComponentDescriptor* desc, const void* data);
    void* GetComponent(ObjectID entity, ComponentID component);

    // Calls f(entity) for every live entity, in slot order.
    template<typename Function>
    void ForEachEntity(Function&& f) const
    {
        for (uint32_t index = 0; index < m_Slots.size(); ++index)
            if (m_Slots[index].Alive) f(ObjectID { .Index = index, .Generation = m_Slots[index].Generation });
    }

    // Calls f(descriptor, data) for every component of `entity` (none if it's dead). Like
    // GetComponent, the addresses are only valid during the call.
    template<typename Function>
    void ForEachComponent(ObjectID entity, Function&& f)
    {
        if (!IsAlive(entity)) return;
        for (auto& [id, storage] : m_Components)
        {
            auto slot = storage.SlotOf.find(entity.Index);
            if (slot != storage.SlotOf.end())
                f(*storage.Descriptor, storage.Data.data() + slot->second * storage.Descriptor->Size);
        }
    }
    
private:
    static void RemoveComponent(ComponentStorage& storage, uint32_t entityIndex);

    std::vector<Slot>     m_Slots     = { }; // indexed directly by ObjectID::Index; every slot ever used, alive or free
    std::vector<uint32_t> m_FreeSlots = { }; // free slot indexes ready to use by CreateEntity
    std::unordered_map<ComponentID, ComponentStorage> m_Components; // one storage per component type
};

GRAM_NAMESPACE_END