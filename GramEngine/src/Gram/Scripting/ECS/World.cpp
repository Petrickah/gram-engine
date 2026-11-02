#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/ECS/World.hpp"

GRAM_NAMESPACE_BEGIN

ObjectID World::CreateEntity()
{
    if (!m_FreeSlots.empty())
    {
        uint32_t index = m_FreeSlots.back(); 
        m_FreeSlots.pop_back();
        m_Slots[index].Alive = true;

        return ObjectID { .Index = index, .Generation = m_Slots[index].Generation };
    }

    m_Slots.push_back(Slot { .Generation = 0, .Alive = true });
    uint32_t index = static_cast<uint32_t>(m_Slots.size() - 1);

    return ObjectID { .Index = index, .Generation = m_Slots[index].Generation };
}

void World::DestroyEntity(const ObjectID &entity)
{
    if (IsAlive(entity))
    {
        m_Slots[entity.Index].Alive = false;
        m_Slots[entity.Index].Generation++;
        m_FreeSlots.push_back(entity.Index);

        for (auto& [id, storage] : m_Components)
        {
            RemoveComponent(storage, entity.Index);
        }
    }
}

bool World::IsAlive(const ObjectID &entity) const
{
    return entity.Index < m_Slots.size()
        && m_Slots[entity.Index].Alive
        && m_Slots[entity.Index].Generation == entity.Generation;
}

void* World::AddComponent(ObjectID entity, const ComponentDescriptor *desc, const void *data)
{
    GramCheckNull(desc && data, "AddComponent: no descriptor or no data");
    GramCheckNull(IsAlive(entity), "AddComponent({}): the entity isn't alive", desc->Name);

    ComponentStorage& storage = m_Components[desc->ID];
    GramCheckNull(!storage.SlotOf.contains(entity.Index), "AddComponent({}): the entity already has one", desc->Name);

    storage.Descriptor = desc;
    const uint32_t position = static_cast<uint32_t>(storage.Owners.size());
    storage.Data.resize(storage.Data.size() + desc->Size);
    void* component = storage.Data.data() + position * desc->Size;
    std::memcpy(component, data, desc->Size);
    storage.Owners.push_back(entity.Index);
    storage.SlotOf[entity.Index] = position;

    return component;
}

void* World::GetComponent(ObjectID entity, ComponentID component)
{
    if (!IsAlive(entity)) return nullptr;

    auto storage = m_Components.find(component);
    if (storage == m_Components.end()) return nullptr;

    auto slot = storage->second.SlotOf.find(entity.Index);
    if (slot == storage->second.SlotOf.end()) return nullptr;

    return storage->second.Data.data() + slot->second * storage->second.Descriptor->Size;
}

// Swap-remove: the last component moves into the hole, so Data stays packed and only two
// entries change, however many components there are.
void World::RemoveComponent(ComponentStorage& storage, uint32_t entityIndex)
{
    auto slot = storage.SlotOf.find(entityIndex);
    if (slot == storage.SlotOf.end()) return;

    const size_t size     = storage.Descriptor->Size;
    const uint32_t hole   = slot->second;
    const uint32_t last   = static_cast<uint32_t>(storage.Owners.size() - 1);

    if (hole != last)
    {
        std::memcpy(storage.Data.data() + hole * size, storage.Data.data() + last * size, size);
        const uint32_t moved = storage.Owners[last];
        storage.Owners[hole] = moved;
        storage.SlotOf[moved] = hole;
    }

    storage.Data.resize(last * size);
    storage.Owners.pop_back();
    storage.SlotOf.erase(slot);
}

GRAM_NAMESPACE_END
