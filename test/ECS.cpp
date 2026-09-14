#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "common/ECS.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <functional>
#include <vector>

class Node : public ecs::Entity {
public:
    ENTITY(Node, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)
};

class Movable : public Node {
public:
    ENTITY(Movable, Node)

    void release() override { ecs::DestroyEntity(this); }

    struct Velocity {
        float dx = 1;
        float dy = 1;
    };
    COMPONENT(Velocity, velocity)
};

static int countNodes() {
    int n = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it)
        ++n;
    return n;
}

TEST_CASE("ECS.basicCreateAndComponent") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    Movable* b = Movable::create();

    a->position()->x = 10;
    a->position()->y = 0;

    b->position()->x = 0;
    b->position()->y = 10;
    b->velocity()->dx = 2;
    b->velocity()->dy = 2;

    CHECK(std::abs(a->position()->x - 10.f) < 1e-5f);
    CHECK(std::abs(b->position()->y - 10.f) < 1e-5f);
    CHECK(std::abs(b->velocity()->dx - 2.f) < 1e-5f);

    a->release();
    b->release();
}

TEST_CASE("ECS.viewIncludesSubclass") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node::create();
    Movable::create();

    int count = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        pos->x = 0;
        pos->y = 0;
        ++count;
    }
    CHECK_EQ(count, 2);
}

TEST_CASE("ECS.dynamicCreateDestroy") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    Movable* b = Movable::create();
    a->position()->x = 3.f;
    b->position()->x = 7.f;
    CHECK(ecs::is_entity_visible(a));
    CHECK(ecs::is_entity_visible(b));
    CHECK_EQ(countNodes(), 2);

    ecs::EntityHandle ha = ecs::handle_of(a);
    const uint32_t oldGen = a->generation;
    a->release();
    CHECK(!ecs::is_entity_visible(a));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK_EQ(countNodes(), 1);

    Node* c = Node::create();
    c->position()->x = 11.f;
    CHECK(ecs::is_entity_visible(c));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(c)) == c);
    CHECK_EQ(countNodes(), 2);
    if (c == a)
        CHECK(c->generation != oldGen);

    b->release();
    CHECK_EQ(countNodes(), 1);
    c->release();
    CHECK_EQ(countNodes(), 0);
    CHECK(ecs::try_get(ecs::handle_of(c)) == nullptr);
}

TEST_CASE("ECS.deferredCreateDestroyDuringView") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    a->position()->x = 1.f;
    CHECK_EQ(countNodes(), 1);

    {
        ecs::ScopedDefer defer;
        Node* spawned = Node::create();
        CHECK((spawned->flags & ecs::kEntityStaging) != 0);
        CHECK_EQ(countNodes(), 1);

        a->release();
        CHECK((a->flags & ecs::kEntityDead) != 0);
        CHECK_EQ(countNodes(), 0);
        // spawned is in the staging table; do not keep the pointer after commit.
    }

    CHECK_EQ(countNodes(), 1);
}

// ---------------------------------------------------------------------------
// Script side: extend C++ entity classes from Squirrel, drive them with a
// script System, and have eve.view() return the C++ entities.
// ---------------------------------------------------------------------------

static const char* kEcsScriptBridgeContent = R"SQ(
function testInheritCppEntityAndAccess() {
    class Enemy extends eve.Node {}
    local e = Enemy()
    if (e == null) return false
    if (!e.isAlive()) return false
    // ids are 0-based registry indices; every live entity has a stable id.
    if (e.getId() < 0) return false

    e.getPos().setX(10.0)
    e.getPos().setY(20.0)
    if (e.getPosX() != 10.0) return false
    if (e.getPosY() != 20.0) return false

    e.setPosX(30.0)
    if (e.getPosX() != 30.0) return false
    return true
}

function testSystemOverCppEntities() {
    class Enemy extends eve.Movable {}
    class MoveSys extends eve.System {
        constructor() { base.constructor(Enemy) }
        function update(dt) {
            foreach (e in entities()) {
                e.setPosX(e.getPosX() + e.getVelX() * dt)
                e.setPosY(e.getPosY() + e.getVelY() * dt)
            }
        }
    }

    local a = Enemy()
    local b = Enemy()
    a.setPosX(0.0)
    a.setVelX(1.0)
    b.setPosX(10.0)
    b.setVelX(2.0)

    local sys = MoveSys()
    sys.update(2.0)
    if (a.getPosX() != 2.0) return false
    if (b.getPosX() != 14.0) return false
    return true
}

function testCppCreatedEntitiesVisible() {
    class Enemy extends eve.Node {}
    local all = eve.view(Enemy)
    if (all.len() < 2) return false
    local found42 = false
    foreach (e in all) {
        if (e.getPosX() == 42.0) found42 = true
        e.setPosX(e.getPosX() + 1.0)
    }
    if (!found42) return false
    return true
}

function testDestroyFromScript() {
    class Enemy extends eve.Node {}
    local e = Enemy()
    if (!e.isAlive()) return false
    e.destroy()
    if (e.isAlive()) return false
    return true
}
)SQ";

namespace {

// Register a C++ ECS entity type so eve.view() (and script Systems built on it)
// can enumerate live instances and hand them to scripts as wrapped instances.
template <typename T>
void registerCppEntityClassForScript(const ssq::Class& cls) {
    eve::registerCppEntityView(cls, [](ssq::Array& out) {
        HSQUIRRELVM vm = out.getHandle();
        sq_pushobject(vm, out.getRaw());
        ecs::Table* table = ecs::current();
        if (table == nullptr) {
            sq_pop(vm, 1);
            return;
        }
        ecs::IComponentManager& cm = table->getOrCreateManager<T>();
        auto* reg = cm.getOrCreateRegistryComponentBuffer<T>();
        // Depth-first walk of the registry and its subclass/sibling registries,
        // matching ecs::View<T> semantics.
        std::vector<ecs::IComponentBuffer*> stack;
        stack.push_back(reg);
        while (!stack.empty()) {
            ecs::IComponentBuffer* buf = stack.back();
            stack.pop_back();
            auto* r = dynamic_cast<ecs::IRegistryComponentBuffer*>(buf);
            if (r != nullptr) {
                for (uint32_t i = 0; i < r->entity_count(); ++i) {
                    ecs::Entity* ent = r->entity_at(i);
                    if (ent != nullptr && ecs::is_entity_visible(ent)) {
                        ssq::detail::pushByPtr<T>(vm, static_cast<T*>(ent));
                        sq_arrayappend(vm, -2);
                    }
                }
            }
            if (buf->children != nullptr)
                stack.push_back(buf->children);
            if (buf->next != nullptr)
                stack.push_back(buf->next);
        }
        sq_pop(vm, 1);
    });
}

class ScriptEcsBridgeFixture {
public:
    ScriptEcsBridgeFixture() : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        registerCppBindings();
        ssq::Script s = vm.compileSource(kEcsScriptBridgeContent);
        vm.run(s);
    }

protected:
    // Register C++ Node/Movable as inheritable script classes and expose
    // component accessors so scripts can read/write the C++ entities.
    void registerCppBindings() {
        ssq::Table eve(vm.find("eve"));

        auto nodeCls = eve.addClass("Node", std::function<Node*()>([]() -> Node* { return Node::create(); }), false);
        registerCppEntityClassForScript<Node>(nodeCls);
        nodeCls.addFunc("getId", [](Node* self) { return int(self->id); });
        nodeCls.addFunc("isAlive", [](Node* self) { return ecs::is_entity_visible(self); });
        nodeCls.addFunc("destroy", [](Node* self) { self->release(); });
        nodeCls.addFunc("getPosX", [](Node* self) { return self->position()->x; });
        nodeCls.addFunc("getPosY", [](Node* self) { return self->position()->y; });
        nodeCls.addFunc("setPosX", [](Node* self, float v) { self->position()->x = v; });
        nodeCls.addFunc("setPosY", [](Node* self, float v) { self->position()->y = v; });
        nodeCls.addFunc("getPos", [](Node* self) -> Node::Position* { return &*self->position(); });

        auto movableCls = eve.addClass("Movable", std::function<Movable*()>([]() -> Movable* { return Movable::create(); }), false);
        registerCppEntityClassForScript<Movable>(movableCls);
        movableCls.addFunc("getId", [](Movable* self) { return int(self->id); });
        movableCls.addFunc("isAlive", [](Movable* self) { return ecs::is_entity_visible(self); });
        movableCls.addFunc("destroy", [](Movable* self) { self->release(); });
        movableCls.addFunc("getPosX", [](Movable* self) { return self->position()->x; });
        movableCls.addFunc("getPosY", [](Movable* self) { return self->position()->y; });
        movableCls.addFunc("setPosX", [](Movable* self, float v) { self->position()->x = v; });
        movableCls.addFunc("setPosY", [](Movable* self, float v) { self->position()->y = v; });
        movableCls.addFunc("getVelX", [](Movable* self) { return self->velocity()->dx; });
        movableCls.addFunc("getVelY", [](Movable* self) { return self->velocity()->dy; });
        movableCls.addFunc("setVelX", [](Movable* self, float v) { self->velocity()->dx = v; });
        movableCls.addFunc("setVelY", [](Movable* self, float v) { self->velocity()->dy = v; });

        // Component structs exposed as script classes so getPos() returns a
        // live view of the C++ component storage.
        auto posCls = eve.addClass("Position",
            std::function<Node::Position*()>([]() -> Node::Position* { return nullptr; }), false);
        posCls.addFunc("getX", [](Node::Position* self) { return self->x; });
        posCls.addFunc("getY", [](Node::Position* self) { return self->y; });
        posCls.addFunc("setX", [](Node::Position* self, float v) { self->x = v; });
        posCls.addFunc("setY", [](Node::Position* self, float v) { self->y = v; });
    }

    ssq::VM vm;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.inheritCppEntityAndAccess") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    CHECK(vm.callFunc(vm.findFunc("testInheritCppEntityAndAccess"), vm).toBool());

    int found = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        if (std::abs(pos->x - 30.f) < 1e-5f)
            ++found;
    }
    CHECK_EQ(found, 1);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.systemOverCppEntities") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    CHECK(vm.callFunc(vm.findFunc("testSystemOverCppEntities"), vm).toBool());

    int atTwo = 0, atFourteen = 0;
    auto view = ecs::View<Movable, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        if (std::abs(pos->x - 2.f) < 1e-5f)
            ++atTwo;
        if (std::abs(pos->x - 14.f) < 1e-5f)
            ++atFourteen;
    }
    CHECK_EQ(atTwo, 1);
    CHECK_EQ(atFourteen, 1);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.cppCreatedEntitiesVisible") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* n1 = Node::create();
    Node* n2 = Node::create();
    n1->position()->x = 42.f;

    CHECK(vm.callFunc(vm.findFunc("testCppCreatedEntitiesVisible"), vm).toBool());

    // Script view() wrapped the C++ entities and mutated their components.
    CHECK(std::abs(n1->position()->x - 43.f) < 1e-5f);
    CHECK(std::abs(n2->position()->x - 1.f) < 1e-5f);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.destroyFromScript") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    CHECK(vm.callFunc(vm.findFunc("testDestroyFromScript"), vm).toBool());

    int live = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it)
        ++live;
    CHECK_EQ(live, 0);
}
