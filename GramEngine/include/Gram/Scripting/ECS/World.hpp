#pragma once

#include <cstddef>
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
    struct Snapshot {
        std::vector<Slot>     Slots     = { }; // indexed directly by ObjectID::Index; every slot ever used, alive or free
        std::vector<uint32_t> FreeSlots = { }; // free slot indexes ready to use by CreateEntity
        std::unordered_map<ComponentID, ComponentStorage> Components; // one storage per component type
    };
    
public:
    ObjectID CreateEntity();
    void DestroyEntity(const ObjectID& entity);
    bool IsAlive(const ObjectID& entity) const;

    void* AddComponent(ObjectID entity, const ComponentDescriptor* desc, const void* data);
    void* GetComponent(ObjectID entity, ComponentID component);

    // Iteration by position, for callers that only have a ComponentID (HostAPI): how many components of
    // that type exist, and the one at `position` (0 .. count - 1, storage order) with its entity written
    // to *entity when it's not null; nullptr past the end.
    uint32_t ComponentCount(ComponentID component) const;
    void*    ComponentAt(ComponentID component, uint32_t position, ObjectID* entity);

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

    template<typename T> uint32_t Count() const { return ComponentCount(T::ID); }
    template<typename T> T* Get(ObjectID entity)                 { return static_cast<T*>(GetComponent(entity, T::ID)); }
    template<typename T> T* Add(ObjectID entity, const T& value) { return static_cast<T*>(AddComponent(entity, &T::Descriptor(), &value)); }
    template<typename T, typename Function> void Each(Function&& f)
    {
        auto it = m_Components.find(T::ID);
        if (it == m_Components.end()) return;
        ComponentStorage& storage = it->second;

        for (uint32_t position = 0; position < storage.Owners.size(); position++)
        {
            const uint32_t index  = storage.Owners[position];
            const ObjectID entity = { .Index = index, .Generation = m_Slots[index].Generation };
            T& component = *reinterpret_cast<T*>(storage.Data.data() + position * sizeof(T));

            f(entity, component);
        }
    }

    Snapshot TakeSnapshot() const;
	void RestoreSnapshot(const Snapshot& snapshot);
    
private:
    static void RemoveComponent(ComponentStorage& storage, uint32_t entityIndex);

    std::vector<Slot>     m_Slots     = { }; // indexed directly by ObjectID::Index; every slot ever used, alive or free
    std::vector<uint32_t> m_FreeSlots = { }; // free slot indexes ready to use by CreateEntity
    std::unordered_map<ComponentID, ComponentStorage> m_Components; // one storage per component type
};

GRAM_NAMESPACE_END