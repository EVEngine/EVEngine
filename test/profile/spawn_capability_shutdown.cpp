// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"common"}
// A process-static Table must destroy its component even when borrowed capability storage has already shut down.
#include <cstdio>
#include <cstdlib>
#include "common/Capability.h"
#include "common/CrashHandler.h"
#include "common/ECS.h"
namespace {
int destroyed = 0;
void stage(const char* name) {
    std::fprintf(stderr, "spawn capability shutdown: %s (destroyed=%d)\n", name, destroyed);
    std::fflush(stderr);
}
struct Interface {
    static constexpr const char* capabilityName = "test.spawn.shutdown.listener";
};
struct Component : Interface {
    Component() {
        eve::cap::provide<Interface>(this);
        eve::cap::addListener<Interface>(this);
    }
    ~Component() {
        stage("component revoke");
        eve::cap::revoke<Interface>(this);
        stage("component remove listener");
        eve::cap::removeListener<Interface>(this);
        stage("component clear publication");
        eve::cap::provide<Interface>(nullptr);
        if (eve::cap::query<Interface>() || eve::cap::listenerCount<Interface>() || eve::cap::listenerAt<Interface>(0))
            std::abort();
        ++destroyed;
        stage("component destruction complete");
    }
};
struct Root : ecs::Entity {
    ENTITY(Root, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
struct CheckAfterTable {
    ~CheckAfterTable() {
        stage("after-table destruction check");
        if (destroyed != 1) std::abort();
    }
};
}  // namespace
int main() {
#if defined(_WIN32)
    eve::installCrashHandler();
#endif
    stage("main entered");
    static CheckAfterTable last;
    auto*                  root = Root::create();
    stage("default-table root created");
    ecs::ComponentRef<Component>(root).emplace();
    stage("component publication complete; returning from main");
    return eve::cap::query<Interface>() && eve::cap::listenerCount<Interface>() == 1 ? 0 : 1;
}
