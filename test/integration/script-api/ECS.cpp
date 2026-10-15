#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "common/ECS.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <functional>
#include <type_traits>
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

[[maybe_unused]] static int countNodes() {
    int n = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it)
        ++n;
    return n;
}


// ---------------------------------------------------------------------------
// Script side: extend C++ entity classes from Squirrel, drive them with a
// script System, and have eve.view() return the C++ entities.
// ---------------------------------------------------------------------------

static const char* kEcsScriptBridgeContent = R"SQ(
[[maybe_unused]] function testInheritCppEntityAndAccess() {
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

[[maybe_unused]] function testSystemOverCppEntities() {
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

[[maybe_unused]] function testCppCreatedEntitiesVisible() {
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

[[maybe_unused]] function testDestroyFromScript() {
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
[[maybe_unused]] void registerCppEntityClassForScript(const ssq::Class& cls) {
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

// ---------------------------------------------------------------------------
// Storage contract.
//
// An entity record only has to be constructible from the arguments it is
// created with, and destructible. An attached component only has to be
// movable. Neither has to be copyable, and a component only has to be
// default constructible when its value is asked for without arguments.
//
// These types are deliberately minimal examples of each shape. They exist so a
// later change cannot quietly put the old requirements back: storing a
// component as std::optional<T>, or deriving the entity record buffer from the
// component slot buffer, both fail to compile here.
// ---------------------------------------------------------------------------

/** @brief Entity root with no default constructor and no copy. */
class StrictNode : public ecs::Entity {
public:
    ENTITY(StrictNode, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    explicit StrictNode(int seed) : seed_(seed) {}
    StrictNode(const StrictNode&)                = delete;
    StrictNode& operator=(const StrictNode&)     = delete;
    StrictNode(StrictNode&&) noexcept            = default;
    StrictNode& operator=(StrictNode&&) noexcept = default;

    /** @brief Value passed to create(), proving the ctor argument arrived. */
    [[nodiscard]] int seed() const { return seed_; }

private:
    int seed_ = 0;
};

static_assert(!std::is_default_constructible_v<StrictNode>, "entity root must stay non-default-constructible");
static_assert(!std::is_copy_constructible_v<StrictNode>, "entity root must stay move-only");

/** @brief Component that is move-only but still default constructible. */
struct MoveOnlyComponent {
    float x = 0.f;

    MoveOnlyComponent()                                        = default;
    MoveOnlyComponent(const MoveOnlyComponent&)                = delete;
    MoveOnlyComponent& operator=(const MoveOnlyComponent&)     = delete;
    MoveOnlyComponent(MoveOnlyComponent&&) noexcept            = default;
    MoveOnlyComponent& operator=(MoveOnlyComponent&&) noexcept = default;
};

static_assert(!std::is_copy_constructible_v<MoveOnlyComponent>, "component must stay move-only");

/** @brief Entity root carrying a move-only component. */
class MoveOnlyHolder : public ecs::Entity {
public:
    ENTITY(MoveOnlyHolder, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(MoveOnlyComponent, moveOnly)
};

/** @brief Component with no default constructor and no copy. */
struct SeededComponent {
    int value = 0;

    explicit SeededComponent(int v) : value(v) {}
    SeededComponent()                                      = delete;
    SeededComponent(const SeededComponent&)                = delete;
    SeededComponent& operator=(const SeededComponent&)     = delete;
    SeededComponent(SeededComponent&&) noexcept            = default;
    SeededComponent& operator=(SeededComponent&&) noexcept = default;
};

static_assert(!std::is_default_constructible_v<SeededComponent>, "component must stay non-default-constructible");

/** @brief Entity root carrying a component that must be constructed with an argument. */
class SeededHolder : public ecs::Entity {
public:
    ENTITY(SeededHolder, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(SeededComponent, seeded)
};
