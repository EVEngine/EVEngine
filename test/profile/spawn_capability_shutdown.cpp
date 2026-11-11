// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"common"}
// A process-static Table must destroy its component even when borrowed capability storage has already shut down.
#include <cstdlib>
#include "common/Capability.h"
#include "common/ECS.h"
namespace {
int destroyed = 0;
struct Interface {
    static constexpr const char* capabilityName = "test.spawn.shutdown.listener";
};
struct Component : Interface {
    Component() {
        eve::cap::provide<Interface>(this);
        eve::cap::addListener<Interface>(this);
    }
    ~Component() {
        eve::cap::revoke<Interface>(this);
        eve::cap::removeListener<Interface>(this);
        eve::cap::provide<Interface>(nullptr);
        if (eve::cap::query<Interface>() || eve::cap::listenerCount<Interface>() || eve::cap::listenerAt<Interface>(0))
            std::abort();
        ++destroyed;
    }
};
struct Root : ecs::Entity {
    ENTITY(Root, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
struct CheckAfterTable {
    ~CheckAfterTable() {
        if (destroyed != 1) std::abort();
    }
};
}  // namespace
int main() {
    static CheckAfterTable last;
    auto*                  root = Root::create();
    ecs::ComponentRef<Component>(root).emplace();
    return eve::cap::query<Interface>() && eve::cap::listenerCount<Interface>() == 1 ? 0 : 1;
}
