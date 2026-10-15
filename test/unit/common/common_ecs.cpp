#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <ECS.hpp>
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <type_traits>
#include <vector>
#include "common/ECS.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// ECS.cpp
namespace {
namespace ECS {
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
    int  n    = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

TEST_CASE("ECS.basicCreateAndComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node*    a = Node::create();
    Movable* b = Movable::create();

    a->position()->x = 10;
    a->position()->y = 0;

    b->position()->x  = 0;
    b->position()->y  = 10;
    b->velocity()->dx = 2;
    b->velocity()->dy = 2;

    CHECK(std::abs(a->position()->x - 10.f) < 1e-5f);
    CHECK(std::abs(b->position()->y - 10.f) < 1e-5f);
    CHECK(std::abs(b->velocity()->dx - 2.f) < 1e-5f);

    a->release();
    b->release();
}

TEST_CASE("ECS.viewIncludesSubclass") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node::create();
    Movable::create();

    int  count = 0;
    auto view  = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        pos->x     = 0;
        pos->y     = 0;
        ++count;
    }
    CHECK_EQ(count, 2);
}

TEST_CASE("ECS.dynamicCreateDestroy") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node*    a       = Node::create();
    Movable* b       = Movable::create();
    a->position()->x = 3.f;
    b->position()->x = 7.f;
    CHECK(ecs::is_entity_visible(a));
    CHECK(ecs::is_entity_visible(b));
    CHECK_EQ(countNodes(), 2);

    ecs::EntityHandle ha     = ecs::handle_of(a);
    const uint32_t    oldGen = a->generation;
    a->release();
    CHECK(!ecs::is_entity_visible(a));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK_EQ(countNodes(), 1);

    Node* c          = Node::create();
    c->position()->x = 11.f;
    CHECK(ecs::is_entity_visible(c));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(c)) == c);
    CHECK_EQ(countNodes(), 2);
    if (c == a) CHECK(c->generation != oldGen);

    b->release();
    CHECK_EQ(countNodes(), 1);
    c->release();
    CHECK_EQ(countNodes(), 0);
    CHECK(ecs::try_get(ecs::handle_of(c)) == nullptr);
}

TEST_CASE("ECS.deferredCreateDestroyDuringView") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node* a          = Node::create();
    a->position()->x = 1.f;
    CHECK_EQ(countNodes(), 1);

    {
        ecs::ScopedDefer defer;
        Node*            spawned = Node::create();
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
        ecs::IComponentManager& cm  = table->getOrCreateManager<T>();
        auto*                   reg = cm.getOrCreateRegistryComponentBuffer<T>();
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
            if (buf->children != nullptr) stack.push_back(buf->children);
            if (buf->next != nullptr) stack.push_back(buf->next);
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

        auto movableCls =
            eve.addClass("Movable", std::function<Movable*()>([]() -> Movable* { return Movable::create(); }), false);
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
        auto posCls = eve.addClass(
            "Position", std::function<Node::Position*()>([]() -> Node::Position* { return nullptr; }), false);
        posCls.addFunc("getX", [](Node::Position* self) { return self->x; });
        posCls.addFunc("getY", [](Node::Position* self) { return self->y; });
        posCls.addFunc("setX", [](Node::Position* self, float v) { self->x = v; });
        posCls.addFunc("setY", [](Node::Position* self, float v) { self->y = v; });
    }

    ssq::VM vm;
};

}  // namespace


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

TEST_CASE("ECS.moveOnlyEntityRootAndMoveOnlyComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    // A record whose constructor needs an argument, with copy deleted.
    StrictNode* strict = StrictNode::create(42);
    CHECK_EQ(strict->seed(), 42);

    // The existing first-touch idiom still materialises a component, even
    // though that component is move-only.
    MoveOnlyHolder* holder = MoveOnlyHolder::create();
    holder->moveOnly()->x  = 3.f;
    CHECK(std::abs(holder->moveOnly()->x - 3.f) < 1e-5f);

    strict->release();
    holder->release();
}

TEST_CASE("ECS.nonDefaultConstructibleComponentNeedsExplicitConstruction") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    SeededHolder* seeded = SeededHolder::create();
    SeededHolder* empty  = SeededHolder::create();

    CHECK(!seeded->seeded().has());
    CHECK(!empty->seeded().has());

    seeded->seeded().emplace(7);
    CHECK(seeded->seeded().has());
    CHECK_EQ(seeded->seeded()->value, 7);

    // Nothing may conjure a value for a component that has no default
    // constructor, so the other entity stays empty.
    CHECK(!empty->seeded().has());

    // A View over such a component therefore yields only the entity that
    // really holds a value, rather than materialising one per entity.
    int  seen = 0;
    auto view = ecs::View<SeededHolder, SeededComponent>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [component] = *it;
        CHECK(component != nullptr);
        CHECK_EQ(component->value, 7);
        ++seen;
    }
    CHECK_EQ(seen, 1);

    seeded->release();
    empty->release();
}

TEST_CASE("ECS.deferredPublishTransfersNonDefaultComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    {
        ecs::ScopedDefer defer;
        SeededHolder*    staged = SeededHolder::create();
        staged->seeded().emplace(11);
        CHECK((staged->flags & ecs::kEntityStaging) != 0);
        CHECK_EQ(staged->seeded()->value, 11);
    }

    // The record and its component survived publish_from, which is the only
    // caller of transfer_slot.
    int  found = 0;
    auto view  = ecs::View<SeededHolder, SeededComponent>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [component] = *it;
        if (component->value == 11) ++found;
    }
    CHECK_EQ(found, 1);
}
}  // namespace ECS
}  // namespace

// ecs_container_instances.cpp
namespace {
namespace ecs_container_instances {
namespace {
class ContainerBase : public ecs::Entity {
public:
    ENTITY(ContainerBase, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    struct Position {
        int value = 0;
    };
    COMPONENT(Position, position)
};

class ContainerDerived : public ContainerBase {
public:
    ENTITY(ContainerDerived, ContainerBase)
    struct Detail {
        int value = 0;
    };
    COMPONENT(Detail, detail)
};
}  // namespace

TEST_CASE("ecs.containers_keep_table_isolation_and_stale_handle_detection") {
    ecs::Table first;
    ecs::Table second;
    auto*      a         = ContainerDerived::create(first);
    auto*      b         = ContainerDerived::create(second);
    a->position()->value = 7;
    b->position()->value = 9;
    const auto handle    = ecs::handle_of(a);
    CHECK(ecs::try_get(handle) == a);
    CHECK(first.getManager(typeid(ContainerDerived)) != second.getManager(typeid(ContainerDerived)));
    {
        ecs::ScopedTable scope(second);
        CHECK_EQ(a->position()->value, 7);
        CHECK_EQ(b->position()->value, 9);
    }
    ecs::DestroyEntity(a);
    CHECK(ecs::try_get(handle) == nullptr);
    auto* replacement = ContainerDerived::create(first);
    CHECK_EQ(replacement->id, handle.id);
    CHECK(replacement->generation != handle.generation);
    CHECK(ecs::try_get(ecs::handle_of(replacement)) == replacement);
}

TEST_CASE("ecs.containers_publish_deferred_inherited_components") {
    ecs::Table table;
    {
        ecs::ScopedDefer scope(table);
        auto*            entity   = ContainerDerived::create(table);
        entity->position()->value = 4;
        entity->detail()->value   = 8;
        int visible               = 0;
        for (auto [position] : ecs::View<ContainerBase, ContainerBase::Position>(table)) {
            (void)position;
            ++visible;
        }
        CHECK_EQ(visible, 0);
    }
    int visible = 0;
    for (auto [position, detail] :
         ecs::View<ContainerDerived, ContainerBase::Position, ContainerDerived::Detail>(table)) {
        CHECK_EQ(position->value, 4);
        CHECK_EQ(detail->value, 8);
        ++visible;
    }
    CHECK_EQ(visible, 1);
}

TEST_CASE("ecs.handle_containers_preserve_generation_and_table_projection") {
    static_assert(std::is_aggregate_v<ecs::EntityHandle>);
    ecs::Table                     first;
    ecs::Table                     second;
    auto*                          entity   = ContainerDerived::create(first);
    const auto                     original = ecs::handle_of(entity);
    std::vector<ecs::EntityHandle> handles(64, original);
    auto                           copied = handles;
    handles.front()                       = {&second, std::type_index(typeid(ContainerDerived)), 99, 88};
    CHECK(copied.front().table == &first);
    CHECK(copied.front().id == original.id);
    CHECK(copied.front().generation == original.generation);
    CHECK(ecs::try_get(copied.front()) == entity);
    auto moved = std::move(copied);
    copied.clear();
    copied.push_back({});
    CHECK(copied.front().table == nullptr);
    std::vector<ecs::EntityHandle> assigned;
    assigned = handles;
    CHECK(assigned.front().table == &second);
    assigned = std::move(moved);
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().type == std::type_index(typeid(ContainerDerived)));
    ecs::DestroyEntity(entity);
    CHECK(ecs::try_get(assigned.front()) == nullptr);
    auto* replacement = ContainerDerived::create(first);
    CHECK(replacement->id == original.id);
    CHECK(replacement->generation != assigned.front().generation);
    CHECK(ecs::try_get(assigned.front()) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(replacement)) == replacement);
}
}  // namespace ecs_container_instances
}  // namespace
