#include "common/ECS.h"
#include <simplesquirrel/simplesquirrel.hpp>
namespace eve
{

ComponentManager& ComponentManager::inst() {
    static ComponentManager instance;
    return instance;
}

class ScriptComponentRegister : public IComponentRegister {
public:
    ScriptComponentRegister(ssq::Class cls) : data(cls.getHandle()) {
        cls.addFunc("new", [](){
        });
    }

    ssq::Array data;
};

void ComponentManager::expose(ssq::Table& table) {
    table.addFunc("component", [](std::string name, ssq::Class cls) {
        auto& c = inst().script_components;
        if (c.find(name) == c.end()) {
            c.insert(std::make_pair(name, new ScriptComponentRegister(cls)));
        }
    });
}



} // namespace eve
