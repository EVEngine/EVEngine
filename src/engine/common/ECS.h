#pragma once

#include <map>
#include <vector>
#include <cstdint>
#include <typeindex>
#include <string>

namespace ssq
{
    class Table;
} // namespace ssq


namespace eve {

// only record a id, it has a map to all components and subentities
class Entity {
public:
    Entity() {}

};

class IComponentRegister {
public:
    virtual ~IComponentRegister() {};
};


template<typename T>
class ComponentRegister;

class ComponentManager {
public:
    static void expose(ssq::Table& table);
    static ComponentManager& inst();

    std::map<std::type_index, IComponentRegister*> components;
    std::map<std::string, IComponentRegister*> script_components;
};

template<typename T>
class Component {
public:
    Component(Entity& entity) {
        id = getReg()->add();
    }

    T& operator* () {
        return getReg()->get(id);
    }

    T* operator-> () {
        return &getReg()->get(id);
    }

protected:
    uint32_t id;

    ComponentRegister<T>* getReg() {
        auto& cm = ComponentManager::inst();
        auto* reg = cm.components[std::type_index(typeid(T))];
        return dynamic_cast<ComponentRegister<T>*>(reg);
    }
};



template<typename T>
class ComponentRegister : public IComponentRegister {
public:
    ComponentRegister() {
        ComponentManager::inst().components[std::type_index(typeid(T))] = this; }

    uint32_t add() { uint32_t id = container.size(); container.push_back(T()); return id; }
    T& get(uint32_t id) { return container[id]; }
protected:
    std::vector<T> container;
};


} // namespace eve



