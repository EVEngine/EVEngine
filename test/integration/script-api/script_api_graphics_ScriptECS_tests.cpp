#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_DatabasePanel)
#include "PrivateTestScope.h"
#include <string>
#include "common/Runtime.h"
#include "ui/DatabasePanel.h"
#include "ui/ObjectRegistry.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// DatabasePanel.cpp
namespace test_DatabasePanel {
using namespace eve;
using namespace eve::ui;

namespace {

const char* kDatabaseScript = R"SQ(
class CharacterData {
    name = "Hero"
    hp = 100.0
    alive = true
    </ editor = "combo", options = "warrior,mage,rogue" />
    job = "warrior"
    skills = []
    function update(dt) { hp += dt }
}
)SQ";

eve::OptionalRef<UINode> nodeById(UIHost& host, const std::string& id) { return host.findById(id); }

}  // namespace

TEST_CASE("database.registryCreatesRegistersAndLists") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database.nut");

    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();

    CHECK_EQ(static_cast<int>(registry.unregister(ObjectHandle::invalid()).code()),
             static_cast<int>(eve::StatusCode::Rejected));

    auto firstResult = registry.create("CharacterData");
    REQUIRE(firstResult.ok());
    const ObjectHandle first = firstResult.value();
    CHECK_EQ(registry.count("CharacterData"), size_t(1));

    ssq::Object hero         = runtime.createInstance("CharacterData");
    auto        secondResult = registry.registerObject("CharacterData", hero);
    REQUIRE(secondResult.ok());
    const ObjectHandle second = secondResult.value();
    CHECK_EQ(registry.count("CharacterData"), size_t(2));

    const std::vector<ObjectEntry> entries = registry.entries("CharacterData");
    REQUIRE_EQ(entries.size(), size_t(2));
    CHECK_EQ(entries[0].handle, first);
    CHECK_EQ(entries[1].handle, second);
    CHECK(!entries[1].label.empty());

    CHECK(registry.unregister(first).ok());
    CHECK_EQ(registry.count("CharacterData"), size_t(1));
    CHECK(!registry.unregister(first).ok());  // already removed

    registry.clearAll();
    CHECK_EQ(registry.count("CharacterData"), size_t(0));
}

TEST_CASE("database.panelBuildsGridAndBindsCells") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database.nut");
    ObjectRegistry::instance().clearAll();

    DatabasePanel panel;
    panel.open();
    REQUIRE(UIHost::resolve(panel.host()).has_value());
    CHECK(panel.selectClass("CharacterData"));
    auto idResult = panel.createInstance();
    REQUIRE(idResult.ok());
    const ObjectHandle id = idResult.value();
    CHECK_EQ(panel.selectedClass(), std::string("CharacterData"));

    auto resolvedHost = UIHost::resolve(panel.host());
    REQUIRE(resolvedHost.has_value());
    UIHost& host = resolvedHost->get();
    // Header cells for reflected members.
    REQUIRE(nodeById(host, "db_hdr_name").has_value());
    REQUIRE(nodeById(host, "db_hdr_hp").has_value());
    REQUIRE(nodeById(host, "db_hdr_alive").has_value());
    REQUIRE(nodeById(host, "db_hdr_job").has_value());
    // Row cells + delete button.
    const std::string row = "db_row_" + std::to_string(id.packed());
    REQUIRE(nodeById(host, row).has_value());
    auto nameCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_name");
    REQUIRE(nameCell.has_value());
    CHECK_EQ(nameCell->get().valueText, std::string("Hero"));
    REQUIRE(nodeById(host, "cell_" + std::to_string(id.packed()) + "_alive").has_value());
    REQUIRE(nodeById(host, "db_del_" + std::to_string(id.packed())).has_value());

    // View → model: typing into a cell writes back to the live instance.
    const ObjectEntry* entry = ObjectRegistry::instance().entry(id);
    REQUIRE(entry != nullptr);
    auto tree = host.tree();
    REQUIRE_GE(nameCell->get().handlerText, 1u);
    tree->textHandlers[size_t(nameCell->get().handlerText - 1)]("Axe");
    CHECK_EQ(runtime.readProperty(entry->object, "name").asString(), std::string("Axe"));

    auto aliveCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_alive");
    REQUIRE(aliveCell.has_value());
    REQUIRE_GE(aliveCell->get().handlerToggle, 1u);
    tree->toggleHandlers[size_t(aliveCell->get().handlerToggle - 1)](false);
    CHECK(!runtime.readProperty(entry->object, "alive").asBool());

    // Model → view: external change is pulled into the grid by sync().
    ReflectedValue value;
    value.kind     = ReflectedValueKind::Float;
    value.floating = 250.0;
    CHECK(runtime.writeProperty(entry->object, "hp", value));
    panel.sync();
    auto hpCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_hp");
    REQUIRE(hpCell.has_value());
    CHECK_EQ(hpCell->get().valueText, std::string("250"));

    // Deleting the row removes it from the grid.
    CHECK(panel.unregister(id).ok());
    CHECK(!nodeById(host, row).has_value());

    panel.close();
}

TEST_CASE("database.registryHandlesRejectStaleAndReuseSlots") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database_handles.nut");

    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();

    auto firstResult = registry.create("CharacterData");
    REQUIRE(firstResult.ok());
    const ObjectHandle first = firstResult.value();
    REQUIRE(registry.unregister(first).ok());
    CHECK(registry.isStale(first));
    CHECK(registry.entry(first) == nullptr);

    auto secondResult = registry.create("CharacterData");
    REQUIRE(secondResult.ok());
    const ObjectHandle second = secondResult.value();
    CHECK_EQ(second.index(), first.index());
    CHECK_EQ(second.generation(), first.generation() + 1u);
    CHECK(registry.entry(first) == nullptr);
    CHECK_EQ(static_cast<int>(registry.unregister(first).code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(static_cast<int>(registry.unregister(second).code()), static_cast<int>(eve::StatusCode::Applied));
}

TEST_CASE("database.registryReleasesRootsBeforeRuntimeShutdown") {
    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();
    {
        Runtime runtime(512, ssq::Libs::ALL);
        runtime.initialize();
        runtime.runSource(kDatabaseScript, "database_shutdown.nut");
        REQUIRE(registry.create("CharacterData").ok());
        CHECK_EQ(registry.count("CharacterData"), size_t(1));
    }
    CHECK_EQ(registry.count("CharacterData"), size_t(0));
}
}  // namespace test_DatabasePanel
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ECS)
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <type_traits>
#include <vector>
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ECS.cpp
namespace test_ECS {
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

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.inheritCppEntityAndAccess") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    REQUIRE(vm.callFunc(vm.findFunc("testInheritCppEntityAndAccess"), vm).toBool());

    int  found = 0;
    auto view  = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        if (std::abs(pos->x - 30.f) < 1e-5f) ++found;
    }
    CHECK_EQ(found, 1);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.systemOverCppEntities") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    REQUIRE(vm.callFunc(vm.findFunc("testSystemOverCppEntities"), vm).toBool());

    int  atTwo = 0, atFourteen = 0;
    auto view = ecs::View<Movable, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        if (std::abs(pos->x - 2.f) < 1e-5f) ++atTwo;
        if (std::abs(pos->x - 14.f) < 1e-5f) ++atFourteen;
    }
    CHECK_EQ(atTwo, 1);
    CHECK_EQ(atFourteen, 1);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.cppCreatedEntitiesVisible") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node* n1          = Node::create();
    Node* n2          = Node::create();
    n1->position()->x = 42.f;

    REQUIRE(vm.callFunc(vm.findFunc("testCppCreatedEntitiesVisible"), vm).toBool());

    // Script view() wrapped the C++ entities and mutated their components.
    CHECK(std::abs(n1->position()->x - 43.f) < 1e-5f);
    CHECK(std::abs(n2->position()->x - 1.f) < 1e-5f);
}

TEST_CASE_FIXTURE(ScriptEcsBridgeFixture, "ECS.script.destroyFromScript") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    REQUIRE(vm.callFunc(vm.findFunc("testDestroyFromScript"), vm).toBool());

    int  live = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) ++live;
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
}  // namespace test_ECS
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_EditorShell)
#include "PrivateTestScope.h"
#include "common/Runtime.h"
#include "ui/DatabasePanel.h"
#include "ui/EditorShell.h"
#include "ui/Inspector.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// EditorShell.cpp
namespace test_EditorShell {
using namespace eve;
using namespace eve::ui;

namespace {

eve::OptionalRef<UINode> nodeById(UIHost& host, const std::string& id) { return host.findById(id); }

}  // namespace

TEST_CASE("editorShell.menuDocksAndSwitchesPanels") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource("class ShellHero { name = \"hero\" }", "shell.nut");

    Inspector inspector;
    inspector.refresh();
    CHECK(inspector.selectClass("ShellHero"));
    inspector.open();

    DatabasePanel database;
    database.open();
    CHECK(database.selectClass("ShellHero"));

    EditorShell shell;
    shell.open(inspector.host(), database.host(), {});
    auto resolvedMenu = UIHost::resolve(shell.host());
    REQUIRE(resolvedMenu.has_value());
    UIHost& menu = resolvedMenu->get();
    REQUIRE(nodeById(menu, "menu_inspector").has_value());
    REQUIRE(nodeById(menu, "menu_database").has_value());
    REQUIRE(nodeById(menu, "menu_scene").has_value());
    REQUIRE(nodeById(menu, "menu_close").has_value());

    auto resolvedInspector = UIHost::resolve(inspector.host());
    auto resolvedDatabase  = UIHost::resolve(database.host());
    REQUIRE(resolvedInspector.has_value());
    REQUIRE(resolvedDatabase.has_value());
    UIHost& inspectorHost = resolvedInspector->get();
    UIHost& databaseHost  = resolvedDatabase->get();
    CHECK(inspectorHost.meta()->visible);
    CHECK(databaseHost.meta()->visible);
    CHECK(!inspectorHost.meta()->lockPos);
    CHECK(!inspectorHost.meta()->lockSize);
    CHECK(inspectorHost.meta()->percentW == 0.3f);
    auto toolbar = nodeById(menu, "editor_toolbar");
    REQUIRE(toolbar.has_value());
    CHECK(int(toolbar->get().type) == int(NodeType::Toolbar));

    auto tree           = menu.tree();
    auto databaseButton = nodeById(menu, "menu_database");
    REQUIRE(databaseButton.has_value());
    REQUIRE_GE(databaseButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(databaseButton->get().handlerClick - 1)]();
    CHECK(!databaseHost.meta()->visible);
    CHECK(inspectorHost.meta()->visible);
    tree->clickHandlers[size_t(databaseButton->get().handlerClick - 1)]();
    CHECK(databaseHost.meta()->visible);

    CHECK(shell.selectPanel("inspector"));
    CHECK(inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);

    auto inspectorButton = nodeById(menu, "menu_inspector");
    REQUIRE(inspectorButton.has_value());
    REQUIRE_GE(inspectorButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(inspectorButton->get().handlerClick - 1)]();
    CHECK(!inspectorHost.meta()->visible);

    auto closeButton = nodeById(menu, "menu_close");
    REQUIRE(closeButton.has_value());
    REQUIRE_GE(closeButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(closeButton->get().handlerClick - 1)]();
    CHECK(!inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);

    shell.close();
    CHECK(!shell.isOpen());
    CHECK(!inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);
}
}  // namespace test_EditorShell
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Inspector)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "common/Runtime.h"
#include "ui/Inspector.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Inspector.cpp
namespace test_Inspector {
using namespace eve;
using namespace eve::ui;

namespace {

const char* kInspectorScript = R"SQ(
class InspectorBase {
    baseName = "base"
    baseLevel = 1
}

class InspectorHero extends InspectorBase {
    </ editor = "slider", min = 0, max = 100 />
    hp = 100.0
    name = "Hero"
    alive = true
    </ editor = "combo", options = "warrior,mage,rogue" />
    job = "warrior"
    function update(dt) { baseLevel += 1 }
}
)SQ";

UINode* nodeById(UIHost* host, const std::string& id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

UIHost* resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

}  // namespace

TEST_CASE("runtime.createInstanceReflectsAndWritesProperties") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kInspectorScript, "inspector.nut");

    const ReflectedClass* cls = runtime.reflectedClass("InspectorHero");
    REQUIRE(cls != nullptr);
    CHECK_EQ(cls->base, std::string("InspectorBase"));

    // Member metadata + Squirrel attribute helpers.
    auto hp =
        std::find_if(cls->members.begin(), cls->members.end(), [](const ReflectedMember& m) { return m.name == "hp"; });
    REQUIRE(hp != cls->members.end());
    CHECK_EQ(hp->attrFloat("min"), 0.f);
    CHECK_EQ(hp->attrFloat("max"), 100.f);
    auto job = std::find_if(cls->members.begin(), cls->members.end(),
                            [](const ReflectedMember& m) { return m.name == "job"; });
    REQUIRE(job != cls->members.end());
    CHECK(job->attrOptions("options") == std::vector<std::string>({"warrior", "mage", "rogue"}));

    // Instance creation runs the default constructor and roots the object.
    ssq::Object hero = runtime.createInstance("InspectorHero");
    CHECK(static_cast<int>(hero.getType()) == static_cast<int>(ssq::Type::INSTANCE));
    CHECK_EQ(runtime.classNameOf(hero), std::string("InspectorHero"));
    CHECK_EQ(runtime.readProperty(hero, "name").asString(), std::string("Hero"));

    // reflectInstance merges own + inherited members with live values.
    const std::vector<ReflectedMember> members = runtime.reflectInstance(hero);
    auto                               byName  = [&](const std::string& name) -> const ReflectedMember* {
        auto it =
            std::find_if(members.begin(), members.end(), [&](const ReflectedMember& m) { return m.name == name; });
        return it == members.end() ? nullptr : &*it;
    };
    const ReflectedMember* baseLevel = byName("baseLevel");
    REQUIRE(baseLevel != nullptr);
    CHECK(static_cast<int>(baseLevel->value.kind) == static_cast<int>(ReflectedValueKind::Integer));
    CHECK_EQ(baseLevel->value.integer, int64_t(1));
    const ReflectedMember* update = byName("update");
    REQUIRE(update != nullptr);
    CHECK(update->method);

    // Typed writes preserve the script slot's type.
    ReflectedValue damage;
    damage.kind     = ReflectedValueKind::Float;
    damage.floating = 4.5;
    CHECK(runtime.writeProperty(hero, "hp", damage));
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 4.5);

    ReflectedValue level;
    level.kind     = ReflectedValueKind::Float;  // float in, integer slot stays integer
    level.floating = 7.0;
    CHECK(runtime.writeProperty(hero, "baseLevel", level));
    const ReflectedValue levelAfter = runtime.readProperty(hero, "baseLevel");
    CHECK(static_cast<int>(levelAfter.kind) == static_cast<int>(ReflectedValueKind::Integer));
    CHECK_EQ(levelAfter.integer, int64_t(7));

    ReflectedValue name;
    name.kind = ReflectedValueKind::String;
    name.text = "Axe";
    CHECK(runtime.writeProperty(hero, "name", name));
    CHECK_EQ(runtime.readProperty(hero, "name").asString(), std::string("Axe"));

    ReflectedValue off;
    off.kind = ReflectedValueKind::Bool;
    CHECK(runtime.writeProperty(hero, "alive", off));
    CHECK(!runtime.readProperty(hero, "alive").asBool());

    // Methods and missing slots are not writable.
    ReflectedValue value;
    value.kind    = ReflectedValueKind::Integer;
    value.integer = 1;
    CHECK(!runtime.writeProperty(hero, "update", value));
    CHECK(!runtime.writeProperty(hero, "missing", value));

    // Constructor side effects run on createInstance.
    runtime.runSource("class InspectorCtor { x = 0\n constructor() { x = 42 } }", "ctor.nut");
    ssq::Object ctorInstance = runtime.createInstance("InspectorCtor");
    CHECK_EQ(runtime.readProperty(ctorInstance, "x").asInt(), int64_t(42));

    // Classes defined outside the Runtime API (dofile/compilestring path) are
    // picked up by scanClasses(); redefining a name refreshes the reflection.
    const auto runRaw = [&](const char* source) {
        auto            scope = runtime.enter();
        auto            stack = runtime.guard();
        HSQUIRRELVM     vm    = runtime.handle();
        const SQInteger top   = sq_gettop(vm);
        REQUIRE(SQ_SUCCEEDED(
            sq_compilebuffer(vm, source, static_cast<SQInteger>(std::strlen(source)), "external.nut", SQTrue)));
        sq_pushroottable(vm);
        REQUIRE(SQ_SUCCEEDED(sq_call(vm, 1, SQFalse, SQTrue)));
        sq_settop(vm, top);
    };
    runRaw("class ExternalScanned { speed = 2.5 }");
    CHECK(runtime.reflectedClass("ExternalScanned") == nullptr);
    CHECK(runtime.scanClasses() >= 1);
    const ReflectedClass* external = runtime.reflectedClass("ExternalScanned");
    REQUIRE(external != nullptr);
    REQUIRE(!external->members.empty());
    CHECK_EQ(runtime.scanClasses(), size_t(0));  // unchanged classes are skipped

    runRaw("class ExternalScanned { speed = 9.0\n label = \"new\" }");
    runtime.scanClasses();
    const ReflectedClass* refreshed = runtime.reflectedClass("ExternalScanned");
    REQUIRE(refreshed != nullptr);
    bool hasLabel = false;
    for (const ReflectedMember& member : refreshed->members)
        if (member.name == "label") hasLabel = true;
    CHECK(hasLabel);
}

TEST_CASE("inspector.buildsEditablePanelAndBindsTwoWay") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kInspectorScript, "inspector.nut");

    Inspector inspector;
    inspector.refresh();
    CHECK(inspector.selectClass("InspectorHero"));
    inspector.open();
    UIHost* host = resolveHost(inspector.host());
    REQUIRE(host != nullptr);

    // Class + instance selectors exist.
    REQUIRE(nodeById(host, "class") != nullptr);
    REQUIRE(nodeById(host, "instance") != nullptr);
    REQUIRE(nodeById(host, "add_instance") != nullptr);

    // Own + inherited property widgets were generated.
    UINode* hp = nodeById(host, "prop_hp");
    REQUIRE(hp != nullptr);
    CHECK(static_cast<int>(hp->type) == static_cast<int>(NodeType::Slider));
    CHECK_EQ(hp->value, 100.f);
    UINode* name = nodeById(host, "prop_name");
    REQUIRE(name != nullptr);
    CHECK(static_cast<int>(name->type) == static_cast<int>(NodeType::InputText));
    CHECK_EQ(name->valueText, std::string("Hero"));
    UINode* alive = nodeById(host, "prop_alive");
    REQUIRE(alive != nullptr);
    CHECK(static_cast<int>(alive->type) == static_cast<int>(NodeType::Checkbox));
    CHECK(alive->checked);
    UINode* job = nodeById(host, "prop_job");
    REQUIRE(job != nullptr);
    CHECK(static_cast<int>(job->type) == static_cast<int>(NodeType::Combo));
    // Base-class properties are grouped under their own header.
    REQUIRE(nodeById(host, "prop_baseLevel") != nullptr);
    REQUIRE(nodeById(host, "cls_InspectorBase") != nullptr);

    // View → model: fire the slider handler, then verify the script instance.
    const ssq::Object selected = inspector.selectedInstance();
    REQUIRE(static_cast<int>(selected.getType()) == static_cast<int>(ssq::Type::INSTANCE));
    auto tree = host->tree();
    REQUIRE_GE(hp->handlerValue, 1u);
    tree->valueHandlers[size_t(hp->handlerValue - 1)](42.f);
    CHECK_EQ(runtime.readProperty(selected, "hp").asFloat(), 42.0);

    // Model → view: external change is pulled into the tree by sync().
    ReflectedValue value;
    value.kind         = ReflectedValueKind::Float;
    value.floating     = 55.0;
    const bool written = runtime.writeProperty(selected, "hp", value);
    CHECK(written);
    inspector.sync();
    CHECK_EQ(nodeById(host, "prop_hp")->value, 55.f);

    // inspectObject() binds the panel to a caller-provided live object: edits
    // write straight back into that exact script instance.
    ssq::Object hero2 = runtime.createInstance("InspectorHero");
    CHECK(inspector.inspectObject(hero2));
    CHECK_EQ(inspector.selectedIndex(), 0);
    UINode* hp2 = nodeById(host, "prop_hp");
    REQUIRE(hp2 != nullptr);
    auto tree2 = host->tree();
    REQUIRE_GE(hp2->handlerValue, 1u);
    tree2->valueHandlers[size_t(hp2->handlerValue - 1)](88.f);
    CHECK_EQ(runtime.readProperty(hero2, "hp").asFloat(), 88.0);
}

TEST_CASE("runtime.arrayTableAndNestedInstanceEditing") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(R"SQ(
class SkillData { name = "slash" }
class NestedHolder {
    skills = ["slash", "bash"]
    buffs = { haste = 1.0 }
    pet = null
    constructor() {
        pet = SkillData()
        // Rebuild mutable defaults per instance (Squirrel shares class-field
        // table/array defaults between instances).
        skills = ["slash", "bash"]
        buffs = { haste = 1.0 }
    }
}
)SQ",
                      "nested.nut");

    ssq::Object holder = runtime.createInstance("NestedHolder");

    // Array member editing.
    CHECK_EQ(runtime.arraySize(holder, "skills"), size_t(2));
    CHECK_EQ(runtime.arrayGet(holder, "skills", 0).asString(), std::string("slash"));
    ReflectedValue element;
    element.kind = ReflectedValueKind::String;
    element.text = "kick";
    CHECK(runtime.arraySet(holder, "skills", 0, element));
    CHECK_EQ(runtime.arrayGet(holder, "skills", 0).asString(), std::string("kick"));
    ReflectedValue extra;
    extra.kind = ReflectedValueKind::String;
    extra.text = "jump";
    CHECK(runtime.arrayAppend(holder, "skills", extra));
    CHECK_EQ(runtime.arraySize(holder, "skills"), size_t(3));
    CHECK(runtime.arrayRemove(holder, "skills", 1));
    CHECK_EQ(runtime.arraySize(holder, "skills"), size_t(2));
    CHECK_EQ(runtime.arrayGet(holder, "skills", 1).asString(), std::string("jump"));

    // Table member editing.
    CHECK_EQ(runtime.tableKeys(holder, "buffs"), std::vector<std::string>({"haste"}));
    CHECK_EQ(runtime.tableGet(holder, "buffs", "haste").asFloat(), 1.0);
    ReflectedValue speed;
    speed.kind     = ReflectedValueKind::Float;
    speed.floating = 2.0;
    CHECK(runtime.tableSet(holder, "buffs", "speed", speed));
    CHECK_EQ(runtime.tableGet(holder, "buffs", "speed").asFloat(), 2.0);
    CHECK(runtime.tableRemove(holder, "buffs", "haste"));
    CHECK_EQ(runtime.tableKeys(holder, "buffs"), std::vector<std::string>({"speed"}));

    // Nested instance property.
    const ssq::Object pet = runtime.readObjectProperty(holder, "pet");
    CHECK(static_cast<int>(pet.getType()) == static_cast<int>(ssq::Type::INSTANCE));
    CHECK_EQ(runtime.classNameOf(pet), std::string("SkillData"));
    CHECK_EQ(runtime.readProperty(pet, "name").asString(), std::string("slash"));

    // Inspector: expanded array + table + nested navigation.
    Inspector inspector;
    inspector.refresh();
    CHECK(inspector.selectClass("NestedHolder"));
    inspector.open();
    UIHost* host = resolveHost(inspector.host());
    REQUIRE(host != nullptr);

    UINode* skill0 = nodeById(host, "prop_skills_0");
    REQUIRE(skill0 != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(skill0->handlerText, 1u);
    tree->textHandlers[size_t(skill0->handlerText - 1)]("spin");
    const ssq::Object selected = inspector.selectedInstance();
    CHECK_EQ(runtime.arrayGet(selected, "skills", 0).asString(), std::string("spin"));

    UINode* buffHaste = nodeById(host, "prop_buffs_haste");
    REQUIRE(buffHaste != nullptr);
    REQUIRE_GE(buffHaste->handlerText, 1u);
    tree->textHandlers[size_t(buffHaste->handlerText - 1)]("1.5");
    CHECK_EQ(runtime.tableGet(selected, "buffs", "haste").asFloat(), 1.5);

    // Nested navigation: open pet -> edit its property -> back.
    UINode* openPet = nodeById(host, "open_NestedHolder_pet");
    REQUIRE(openPet != nullptr);
    REQUIRE_GE(openPet->handlerClick, 1u);
    tree->clickHandlers[size_t(openPet->handlerClick - 1)]();
    UINode* petName = nodeById(host, "prop_name");
    REQUIRE(petName != nullptr);
    auto tree2 = host->tree();
    REQUIRE_GE(petName->handlerText, 1u);
    tree2->textHandlers[size_t(petName->handlerText - 1)]("cleave");
    CHECK_EQ(runtime.readProperty(inspector.selectedInstance(), "name").asString(), std::string("cleave"));

    UINode* back = nodeById(host, "inspector_back");
    REQUIRE(back != nullptr);
    REQUIRE_GE(back->handlerClick, 1u);
    tree2->clickHandlers[size_t(back->handlerClick - 1)]();
    REQUIRE(nodeById(host, "prop_skills_0") != nullptr);
    CHECK_EQ(runtime.classNameOf(inspector.selectedInstance()), std::string("NestedHolder"));
}

TEST_CASE("inspector.pickSceneInspectsPickedObject") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource("class PickedHero { name = \"picked\" }", "pick.nut");
    const ssq::Object picked = runtime.createInstance("PickedHero");

    Inspector inspector;
    inspector.refresh();
    inspector.setPickScene([picked]() { return picked; });
    inspector.open();
    UIHost* host = resolveHost(inspector.host());
    REQUIRE(host != nullptr);

    UINode* pick = nodeById(host, "inspector_pick");
    REQUIRE(pick != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(pick->handlerClick, 1u);
    tree->clickHandlers[size_t(pick->handlerClick - 1)]();

    CHECK_EQ(inspector.selectedClass(), std::string("PickedHero"));
    CHECK_EQ(inspector.instanceCount(), 1);
    CHECK_EQ(runtime.readProperty(inspector.selectedInstance(), "name").asString(), std::string("picked"));
}
}  // namespace test_Inspector
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ReflectScript)
#include "PrivateTestScope.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ReflectScript.cpp
namespace test_ReflectScript {
using namespace eve;

namespace {

const char* kReflectScript = R"SQ(
class ReflectScriptBase {
    label = "base"
}

class ReflectScriptHero extends ReflectScriptBase {
    </ editor = "slider", min = 0, max = 100 />
    hp = 100.0
    name = "Hero"
    alive = true
    tags = ["a", "b"]
    stats = { attack = 10 }
    function update(dt) { hp += dt }
}

class ReflectScriptNested {
    level = 7
}

class ReflectScriptHolder {
    nested = ReflectScriptNested()
}

function reflectScriptProbe() {
    local classes = eve.reflect.classes();
    if (!("ReflectScriptHero" in classes)) throw "classes() missing class";

    local info = eve.reflect.classInfo("ReflectScriptHero");
    if (info == null) throw "classInfo() null";
    if (info.base != "ReflectScriptBase") throw "classInfo().base mismatch";
    if (eve.reflect.classInfo("ReflectScriptMissing") != null)
        throw "classInfo() must return null for unknown class";

    local hero = eve.reflect.createInstance("ReflectScriptHero");
    if (eve.reflect.classNameOf(hero) != "ReflectScriptHero")
        throw "classNameOf() mismatch";

    local members = eve.reflect.inspect(hero);
    local hp = null;
    foreach (m in members) {
        if (m.name == "hp") hp = m;
        if (m.name == "label" && m.value != "base")
            throw "inspect() inherited member value missing";
        if (m.name == "update" && !m.method)
            throw "inspect() method flag missing";
    }
    if (hp == null) throw "inspect() missing hp";
    if (hp.attributes.editor != "slider") throw "inspect() attributes lost";
    if (hp.value != 100.0) throw "inspect() hp live value mismatch";

    if (!eve.reflect.write(hero, "hp", 42.5)) throw "write hp failed";
    if (eve.reflect.read(hero, "hp") != 42.5) throw "read hp mismatch";
    if (!eve.reflect.write(hero, "alive", false)) throw "write alive failed";
    if (eve.reflect.read(hero, "alive") != false) throw "read alive mismatch";
    if (eve.reflect.write(hero, "name", 123)) throw "typed write must convert";
    if (eve.reflect.read(hero, "name") != "123") throw "typed write conversion failed";
    if (eve.reflect.write(hero, "missing", 1))
        throw "write to missing member must fail";
    if (eve.reflect.write(hero, "update", 1))
        throw "write to method must fail";
    if (eve.reflect.write(hero, "hp", {}))
        throw "write of non-scalar must fail";

    if (eve.reflect.arraySize(hero, "tags") != 2) throw "arraySize mismatch";
    if (eve.reflect.arrayGet(hero, "tags", 0) != "a") throw "arrayGet mismatch";
    if (!eve.reflect.arrayAppend(hero, "tags", "c")) throw "arrayAppend failed";
    if (eve.reflect.arraySize(hero, "tags") != 3) throw "arrayAppend size mismatch";
    if (!eve.reflect.arraySet(hero, "tags", 2, "z")) throw "arraySet failed";
    if (eve.reflect.arrayGet(hero, "tags", 2) != "z") throw "arraySet value mismatch";
    if (!eve.reflect.arrayRemove(hero, "tags", 1)) throw "arrayRemove failed";
    if (eve.reflect.arrayGet(hero, "tags", 1) != "z")
        throw "arrayRemove shift mismatch";

    local keys = eve.reflect.tableKeys(hero, "stats");
    if (keys.len() != 1 || keys[0] != "attack") throw "tableKeys mismatch";
    if (eve.reflect.tableGet(hero, "stats", "attack") != 10)
        throw "tableGet mismatch";
    if (!eve.reflect.tableSet(hero, "stats", "defense", 5))
        throw "tableSet failed";
    if (eve.reflect.tableGet(hero, "stats", "defense") != 5)
        throw "tableSet value mismatch";
    if (!eve.reflect.tableRemove(hero, "stats", "attack"))
        throw "tableRemove failed";
    if ("attack" in eve.reflect.read(hero, "stats"))
        throw "tableRemove leftover";

    local holder = eve.reflect.createInstance("ReflectScriptHolder");
    local nested = eve.reflect.readObject(holder, "nested");
    if (nested == null) throw "readObject null";
    if (eve.reflect.classNameOf(nested) != "ReflectScriptNested")
        throw "readObject class mismatch";
    if (eve.reflect.read(nested, "level") != 7) throw "nested read mismatch";
    if (eve.reflect.read(hero, "missing") != null)
        throw "read of missing member must be null";

    // Classes defined outside the Runtime API are picked up by scan().
    compilestring("class ReflectScriptScanProbe { x = 1 }")();
    if (eve.reflect.scan() < 1) throw "scan() returned nothing";
    if (eve.reflect.classInfo("ReflectScriptScanProbe") == null)
        throw "scan() did not pick up class";

    local scripts = eve.reflect.scripts();
    if (scripts.len() == 0) throw "scripts() empty";
    if (scripts[0].source != "reflect-script-probe.nut")
        throw "scripts() source mismatch";
    if (scripts[0].state != "Loaded") throw "scripts() state mismatch";

    return true;
}
)SQ";

void requireRun(Runtime& runtime, const char* source, const char* name) {
    bool ok = false;
    try {
        runtime.runSource(source, name);
        ok = true;
    } catch (const ScriptException&) {
    }
    REQUIRE(ok);
}

}  // namespace

TEST_CASE("reflectScriptExposesRuntimeApi") {
    Runtime runtime(1024, ssq::Libs::ALL);
    runtime.initialize();
    requireRun(runtime, kReflectScript, "reflect-script-probe.nut");

    auto                scope = runtime.enter();
    const ssq::Function probe = runtime.vm().findFunc("reflectScriptProbe");
    CHECK(runtime.vm().callFunc(probe, runtime.root()).toBool());
}
}  // namespace test_ReflectScript
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ScriptECS)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include "ScriptTest.h"
#include "common/ECS.h"
#include "common/ScriptComponentSnapshot.h"
#include "common/ScriptComponentStore.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ScriptECS.cpp
namespace test_ScriptECS {
using namespace eve;


static const char* kScriptEcsContent = R"SQ(
function testEcsReady() {
    return eve.ecsReady()
}

function testDefineClassAndComponent() {
    class Position extends eve.Component {
        x = 0.0
        y = 0.0
    }
    class Velocity extends eve.Component {
        x = 1.0
        y = 2.0
    }
    class Moveable extends eve.Entity {
        pos = Position
        vel = Velocity
    }

    local e = Moveable.create()
    if (e == null) return false
    if (!e.isAlive()) return false
    if (e.getId() <= 0) return false
    if (e.pos.x != 0.0 || e.pos.y != 0.0) return false
    if (e.vel.x != 1.0 || e.vel.y != 2.0) return false

    e.pos.x = 10.0
    e.vel.x = 3.0
    if (e.pos.x != 10.0) return false
    if (e.getComponent(Position).x != 10.0) return false
    if (!e.hasComponent(Velocity)) return false
    return true
}

function testViewIncludesSubclass() {
    class Health extends eve.Component {
        hp = 100
    }
    class Actor extends eve.Entity {
        hp = Health
    }
    class Enemy extends Actor {
    }

    local a = Actor.create()
    local b = Enemy.create()
    a.hp.hp = 50
    b.hp.hp = 80

    local actors = eve.view(Actor)
    if (actors.len() < 2) return false

    local enemies = eve.view(Enemy)
    if (enemies.len() < 1) return false

    local foundEnemy = false
    foreach (e in actors) {
        if (e == b) foundEnemy = true
    }
    if (!foundEnemy) return false
    return true
}

function testSystemUpdate() {
    class Position extends eve.Component {
        x = 0.0
    }
    class Velocity extends eve.Component {
        x = 5.0
    }
    class Mover extends eve.Entity {
        pos = Position
        vel = Velocity
    }
    class MoveSys extends eve.System {
        constructor() {
            base.constructor(Mover)
        }
        function update(dt) {
            foreach (e in entities()) {
                e.pos.x += e.vel.x * dt
            }
        }
    }

    local e = Mover.create()
    local sys = MoveSys()
    sys.update(2.0)
    if (e.pos.x != 10.0) return false
    e.destroy()
    if (e.isAlive()) return false
    // destroyed entities leave the view
    foreach (ent in eve.view(Mover)) {
        if (ent == e) return false
    }
    return true
}

function testSystemDynamicSpawnDespawn() {
    class Position extends eve.Component { x = 0.0 }
    class Velocity extends eve.Component { x = 1.0 }
    class Mover extends eve.Entity {
        pos = Position
        vel = Velocity
    }
    class MoveSys extends eve.System {
        constructor() { base.constructor(Mover) }
        function update(dt) {
            local dying = []
            foreach (e in entities()) {
                e.pos.x += e.vel.x * dt
                if (e.pos.x >= 2.0) dying.push(e)
            }
            foreach (e in dying) e.destroy()
        }
    }

    local sys = MoveSys()
    local a = Mover.create()
    local b = Mover.create()
    a.vel.x = 1.0
    b.vel.x = 1.0
    if (eve.view(Mover).len() != 2) return false

    sys.update(1.0)
    if (a.pos.x != 1.0 || b.pos.x != 1.0) return false
    if (eve.view(Mover).len() != 2) return false

    sys.update(1.0)
    if (a.isAlive() || b.isAlive()) return false
    if (eve.view(Mover).len() != 0) return false

    local c = Mover.create()
    c.vel.x = 1.0
    sys.update(0.5)
    if (!c.isAlive() || c.pos.x != 0.5) return false
    if (eve.view(Mover).len() != 1) return false

    // A later entities() call in the same update sees newly created instances.
    class SpawnSys extends eve.System {
        snapshot = 0
        constructor() { base.constructor(Mover) }
        function update(dt) {
            snapshot = entities().len()
            local extra = Mover.create()
            extra.pos.x = 99.0
            if (entities().len() != snapshot + 1) return false
            foreach (e in entities()) {
                if (e.pos.x >= 2.0) e.destroy()
            }
            return true
        }
    }
    local spawnSys = SpawnSys()
    if (!spawnSys.update(0.0)) return false
    if (spawnSys.snapshot != 1) return false
    if (eve.view(Mover).len() != 1) return false
    foreach (e in eve.view(Mover)) {
        if (e != c) return false
        e.destroy()
    }
    return eve.view(Mover).len() == 0
}

function testSystemQueryArrayAndSubclass() {
    class Hp extends eve.Component { v = 1 }
    class Actor extends eve.Entity { hp = Hp }
    class Enemy extends Actor {}
    class Prop extends eve.Entity { hp = Hp }

    class TickSys extends eve.System {
        constructor() { base.constructor([Actor, Prop]) }
        function update(dt) {
            foreach (e in entities()) e.hp.v += 1
        }
    }

    local actor = Actor.create()
    local enemy = Enemy.create()
    local prop = Prop.create()
    local sys = TickSys()
    sys.update(0.0)
    if (actor.hp.v != 2 || enemy.hp.v != 2 || prop.hp.v != 2) return false

    sys.setQuery(Enemy)
    sys.update(0.0)
    if (actor.hp.v != 2 || enemy.hp.v != 3 || prop.hp.v != 2) return false

    local emptySys = eve.System()
    if (emptySys.entities().len() != 0) return false

    actor.destroy()
    enemy.destroy()
    prop.destroy()
    return true
}

function testTypeMarkersAndEntityContainer() {
    class Status extends eve.Component {
        alive = eve.Boolean
        name = eve.String
        score = eve.Number
    }
    class Thing extends eve.EntityContainer {
        status = Status
    }
    local t = Thing.create()
    if (t.status.alive != false) return false
    if (t.status.name != "") return false
    if (t.status.score != 0.0) return false
    t.status.alive = true
    t.status.name = "orb"
    t.status.score = 3.5
    if (!t.status.alive || t.status.name != "orb" || t.status.score != 3.5) return false
    return eve.EntityContainer == eve.Entity
}

function testStaticComponentsTable() {
    class Pos extends eve.Component {
        x = 1.0
    }
    class Node extends eve.Entity {
        static components = { pos = Pos }
        pos = null
    }
    local n = Node.create()
    if (n.pos == null) return false
    if (n.pos.x != 1.0) return false
    return true
}

function testShaderSystemClassExists() {
    // Feature-detect by touching the name, not with `"ShaderSystem" in eve`: the `in`
    // operator is _OP_EXISTS, which SQVM::Get evaluates with GET_FLAG_RAW, so it never
    // runs the engine's lazy _get binder and is false for every name that has not been
    // resolved yet (eve.moduleList is only visible to `in` because Run.cpp sets it
    // eagerly). Accessing a name that resolves to nothing raises, hence the try/catch.
    local shaderSystem = null
    try { shaderSystem = eve.ShaderSystem } catch (e) { return false }
    if (shaderSystem == null) return false
    // Construction without GPU is allowed; update() no-ops until setGpgpu.
    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Vel extends eve.Component { x = 1.0; y = 0.0 }
    class Mover extends eve.Entity { pos = Pos; vel = Vel }
    local sys = eve.ShaderSystem(Mover)
    sys.bindFields(0, "pos", ["x", "y"])
    sys.bindFields(1, "vel", ["x", "y"])
    sys.update(0.016)
    return true
}

function testShaderSystemGpuResidentTransfers() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class FakeBackend {
        dispatches = 0
        records = 0
        function ensureBuffer(binding, count) { return binding }
        function getBuffer(binding) { return binding }
        function dispatch(count, dt) { dispatches += 1 }
        function recordDispatch(sequence, count, dt) { records += 1 }
    }

    local uploads = 0
    local downloads = 0
    local uploadRange = null
    local downloadRange = null
    eve.packEcsFloats <- function(ents, slot, fields, buf) { uploads += 1 }
    eve.unpackEcsFloats <- function(ents, slot, fields, buf, count) { downloads += 1 }
    eve.packEcsFloatsRange <- function(ents, slot, fields, buf, first, count) {
        uploadRange = [first, count]
    }
    eve.unpackEcsFloatsRange <- function(ents, slot, fields, buf, first, count) {
        downloadRange = [first, count]
    }

    local node = Node.create()
    local second = Node.create()
    local third = Node.create()
    local sys = eve.ShaderSystem(Node)
    sys._gpu = true
    sys._backend = FakeBackend()
    sys.bindFields(0, "pos", ["x"], false, false)

    sys.update(0.016)
    sys.update(0.016)
    if (uploads != 1 || downloads != 0 || sys._backend.dispatches != 2) return false

    sys.requestUpload(0).requestReadback(0)
    sys.update(0.016)
    if (uploads != 2 || downloads != 1) return false

    sys.requestUploadRange(0, 1, 1).requestUploadRange(0, 2, 1)
    sys.requestReadbackRange(0, 1, 2)
    sys.update(0.016)
    if (uploadRange == null || uploadRange[0] != 1 || uploadRange[1] != 2) return false
    if (downloadRange == null || downloadRange[0] != 1 || downloadRange[1] != 2) return false
    if (uploads != 2 || downloads != 1) return false

    local batch = sys.record(true, 0.016)
    sys.completeRecorded(batch)
    if (sys._backend.records != 1) return false

    // Typed write-only schemas allocate resident output without packing stale CPU data.
    local outputSys = eve.ShaderSystem(Node)
    outputSys._gpu = true
    outputSys._backend = FakeBackend()
    outputSys.bindSchema(0, { slot = "pos", fields = ["x"], scalar = "f32",
                              access = "write" })
    outputSys.update(0.016)
    if (uploads != 2) return false
    local schema = outputSys.getBindingSchema(0)
    if (schema == null || schema.strideFloats != 1 || schema.access != "write") return false
    if (outputSys.shaderDeclarations("glsl") !=
        "layout(set = 0, binding = 0) writeonly buffer EcsBinding0 { float data[]; } ecs0;\n")
        return false
    if (outputSys.shaderDeclarations("wgsl") !=
        "@group(0) @binding(0) var<storage, read_write> ecs0 : array<f32>;\n")
        return false

    local rejected = false
    try { outputSys.bindSchema(1, { slot = "pos", fields = ["x"], scalar = "i32" }) }
    catch (e) { rejected = true }
    if (!rejected) return false

    // A structural revision invalidates resident packing even at the same capacity.
    local fourth = Node.create()
    sys.update(0.016)
    if (uploads != 3 || downloads != 1) return false
    node.destroy()
    second.destroy()
    third.destroy()
    fourth.destroy()
    return true
}

function testViewCacheStableAndInvalidated() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class Sub extends Node {}

    local a = Node.create()
    local b = Sub.create()

    // No changes between calls → same *content*. The returned array is fresh every
    // call on purpose: the cache stays internal, so a caller that sorts, pops or
    // appends to its view cannot change what the next caller sees. (The view cache
    // still saves the expensive part — walking every registered type.)
    local v1 = eve.view(Node)
    if (v1.len() != 2) return false
    local v2 = eve.view(Node)
    if (v1 == v2) return false
    if (v2.len() != 2) return false

    // Mutating a view is local to the caller and must not leak into the cache.
    v1.pop()
    if (v1.len() != 1) return false
    if (eve.view(Node).len() != 2) return false

    local sv1 = eve.view(Sub)
    if (sv1.len() != 1) return false

    // create() invalidates the cached view (and subclass views via the chain)
    local c = Node.create()
    if (eve.view(Node).len() != 3) return false
    if (eve.view(Sub).len() != 1) return false

    // destroy() invalidates too; dead entities leave the view immediately
    a.destroy()
    b.destroy()
    c.destroy()
    if (eve.view(Node).len() != 0) return false
    return true
}

function testViewCacheBaseClassIncludesSubclasses() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }
    class Sub extends Node {}

    local n = Node.create()
    local s = Sub.create()
    if (eve.view(Node).len() != 2) return false
    if (eve.view(eve.Entity).len() < 2) return false
    n.destroy()
    s.destroy()
    return true
}

function testComponentDefaultsAndSlotsCached() {
    class C extends eve.Component { v = eve.Number; s = eve.String }
    class E extends eve.Entity { c = C }

    local a = E.create()
    if (a.c.v != 0.0 || a.c.s != "") return false
    a.c.v = 5.0

    // Second instance gets fresh resolved defaults, not the mutated values.
    local b = E.create()
    if (b.c.v != 0.0 || b.c.s != "") return false
    if (a.c.v != 5.0) return false
    if (a.getComponent(C) != a.c) return false
    if (!a.hasComponent(C)) return false

    a.destroy()
    b.destroy()
    return true
}

function testDestroyClearsComponentState() {
    class Pos extends eve.Component { x = 0.0 }
    class Node extends eve.Entity { pos = Pos }

    local n = Node.create()
    local pos = n.getComponent(Pos)
    if (pos == null) return false
    if (!n.hasComponent(Pos)) return false
    if (pos.x != 0.0) return false
    pos.x = 5.0

    n.destroy()

    // Destroyed means gone: the component is unreachable and no longer silently
    // writable-but-ignored through either path.
    if (n.isAlive()) return false
    if (n.getComponent(Pos) != null) return false
    if (n.hasComponent(Pos)) return false
    // 字段槽位也必须清：只清 `_comps` 会让 `n.pos` 继续指向一个活着的组件实例，
    // 于是 `n.pos.x = 1` 静默改一个任何 view 都看不到的孤儿（§9.38 实测到的漏洞）。
    if (n.pos != null) return false
    if (eve.view(Node).len() != 0) return false

    // Destroying twice stays a no-op.
    n.destroy()
    if (n.getComponent(Pos) != null) return false
    if (n.pos != null) return false
    return true
}

function testFieldSlotIsTheSameComponentInstance() {
    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Node extends eve.Entity { pos = Pos }

    local a = Node.create()
    local b = Node.create()

    // 已声明槽位的字段与槽位表指向**同一个**组件实例（这曾是 §9.14.2 记错的地方：
    // 它断言 `e.pos` 读到的是组件类对象、写的是类级字段）。
    if (a.pos != a.getComponent(Pos)) return false
    if (typeof a.pos != "instance") return false

    // 逐实体隔离：如果写的是类级共享状态，新建实体会立刻看到 10.0。
    a.pos.x = 10.0
    if (a.getComponent(Pos).x != 10.0) return false
    if (b.pos.x != 0.0) return false
    if (b.getComponent(Pos).x != 0.0) return false

    // 反向：经槽位表写，字段路径要读得到。
    a.getComponent(Pos).y = 4.0
    if (a.pos.y != 4.0) return false

    // 清掉字段（`=` 对已声明成员是普通赋值，不抛）：槽位表仍持有实例，所以显式把两边
    // 都处理掉才是销毁该做的事——这里只钉住"字段可写"这条语义。
    local key = "pos"
    a[key] = null
    if (a.pos != null) return false
    return true
}

function testRegistryOnlySlotHasNoField() {
    class Pos extends eve.Component { x = 1.0 }
    class Node extends eve.Entity {
        static components = { pos = Pos }
    }

    local n = Node.create()
    // 只登记在 components 表、没有同名字段的槽位只能经 getComponent()/hasComponent() 访问。
    if (n.getComponent(Pos) == null) return false
    if (!n.hasComponent(Pos)) return false
    if (n.getComponent(Pos).x != 1.0) return false

    // Squirrel 拒绝对 class 实例新建槽位：未声明的键读不到，
    // `=`（_OP_SET）与 `<-`（new-slot 操作码）报的是**两句不同**的话。这三条语义是
    // create()/destroy() 分流实现的依据，所以钉在这里。
    local readFailed = false
    try { local ignored = n.pos } catch (ex) { readFailed = true }
    if (!readFailed) return false

    local setMessage = ""
    try { n.pos = Pos() } catch (ex) { setMessage = "" + ex }
    if (setMessage.find("does not exist") == null) return false

    local newSlotMessage = ""
    try { n.pos <- Pos() } catch (ex) { newSlotMessage = "" + ex }
    if (newSlotMessage.find("new slot operator") == null) return false
    return true
}

function testGpuDrivenBindingSurface() {
    local gpu
    try { gpu = eve.Gpgpu() } catch (e) { return false }
    local required = ["setGpuDrivenEnabled", "isGpuDrivenEnabled",
                      "gpuDrivenMeshRecord", "gpuDrivenMaterialRecord",
                      "gpuDrivenMaterialUsable", "getGpuDrivenInstanceStride",
                      "getGpuResidentOffsetAlignment", "writeGpuDrivenInstance",
                      "submitResidentInstances"]
    foreach (name in required) if (!(name in gpu)) return false
    // 208, not the old 160: GpuInstance grew again (dev's graphics-side fields), and
    // GpuDrivenTypes.h pins the new size with static_assert(sizeof(GpuInstance) == 208).
    // The offset alignment still comes from kGpuResidentStorageOffsetAlignment.
    return gpu.getGpuDrivenInstanceStride() == 208 &&
           gpu.getGpuResidentOffsetAlignment() == 256
}

function testGpgpuSurfaceWithoutAGraphicsDevice() {
    // 上面那条钉的是"模块面齐全"；这条钉的是**设备缺席**时的契约：本 fixture 有 gpgpu 模块类，
    // 但从未调用 `openGfxWindow`，所以 `isAvailable()` 必须为 false，而
    // `eve.ShaderSystem` 仍允许构造、`bindFields`/`update()` 是 no-op 且**不抛**
    // （`EcsGpu.h` 与 `docs/dev/2026-08-30-ecs-gpu-efficiency.md` 的声明）。
    // 实测（探针，跑完已撤）：`gpuNonnull=true isAvailable=false update=ok`。
    // "设备存在"半边由 `GpuDriven.squirrelResidentInstanceSubmit` 在真机上覆盖。
    local gpu = eve.Gpgpu()
    if (gpu == null) return false
    if (gpu.isAvailable()) return false

    class Pos extends eve.Component { x = 0.0; y = 0.0 }
    class Vel extends eve.Component { x = 1.0; y = 0.0 }
    class Mover extends eve.Entity { pos = Pos; vel = Vel }
    local sys = eve.ShaderSystem(Mover)
    sys.bindFields(0, "pos", ["x", "y"])
    sys.bindFields(1, "vel", ["x", "y"])
    sys.update(0.016)
    return true
}

// 视图顺序 = 实体 id 升序（= 创建顺序）。
//
// `_ecsUnregisterInstance` 用 swap-remove 保持 O(1) 销毁，数组本身在删除后会失序（实测
// [1,2,3,4] 删掉 2 得到 [1,4,3]），而 GPU System 按视图顺序打包成 buffer 的行、回读时又按
// 同一顺序写回，`requestUploadRange/ReadbackRange(first, count)` 直接按行号寻址——顺序一变，
// 回读的值会落到另一个实体上。所以这里把顺序钉死。
function testViewOrderIsEntityIdOrder() {
    class Node extends eve.Entity { }
    local a = Node.create()
    local b = Node.create()
    local c = Node.create()
    local d = Node.create()
    local removedId = b.getId()

    local seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 4) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false

    // 删掉中间一个：幸存者仍按 id 升序（改前这里是 1,4,3）
    b.destroy()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 3) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false
    if (seen.find(a.getId()) == null || seen.find(c.getId()) == null || seen.find(d.getId()) == null) return false

    // 新建实体排在最后；删除最新的一个不影响幸存者的相对顺序
    local e5 = Node.create()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 4 || seen[3] != e5.getId()) return false
    e5.destroy()
    seen = []
    foreach (e in eve.view(Node)) seen.push(e.getId())
    if (seen.len() != 3 || seen[2] != d.getId()) return false

    // id 不复用：这是"按 id 排序"能当稳定键的前提
    for (local i = 0; i < 8; ++i) if (Node.create().getId() == removedId) return false

    // 同一类两次取视图顺序一致
    local first = []
    local second = []
    foreach (e in eve.view(Node)) first.push(e.getId())
    foreach (e in eve.view(Node)) second.push(e.getId())
    if (first.len() != second.len()) return false
    for (local i = 0; i < first.len(); ++i) if (first[i] != second[i]) return false
    return true
}

// 跨子类的视图同样按创建顺序交错：此前按 `_ecsTypes` 的散列顺序分组，同类会聚在一起
// （例如 Left,Left,Right,Right 而不是创建顺序），于是 id 序列不是升序。
function testViewOrderSpansSubclassesByCreation() {
    class Base extends eve.Entity { }
    class Left extends Base { }
    class Right extends Base { }

    Left.create()
    Right.create()
    Left.create()
    Right.create()

    local seen = []
    foreach (e in eve.view(Base)) seen.push(e.getId())
    if (seen.len() != 4) return false
    for (local i = 1; i < seen.len(); ++i) if (seen[i] <= seen[i - 1]) return false
    return true
}
)SQ";

UnitSciptTest(ScriptEcsTest, kScriptEcsContent);

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.ready") { REQUIRE(callScript("testEcsReady")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.defineClassAndComponent") {
    REQUIRE(callScript("testDefineClassAndComponent"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewIncludesSubclass") { REQUIRE(callScript("testViewIncludesSubclass")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemUpdate") { REQUIRE(callScript("testSystemUpdate")); }

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemDynamicSpawnDespawn") {
    REQUIRE(callScript("testSystemDynamicSpawnDespawn"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.systemQueryArrayAndSubclass") {
    REQUIRE(callScript("testSystemQueryArrayAndSubclass"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.typeMarkersAndEntityContainer") {
    REQUIRE(callScript("testTypeMarkersAndEntityContainer"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.staticComponentsTable") {
    REQUIRE(callScript("testStaticComponentsTable"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.shaderSystemClassExists") {
    REQUIRE(callScript("testShaderSystemClassExists"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.shaderSystemGpuResidentTransfers") {
    REQUIRE(callScript("testShaderSystemGpuResidentTransfers"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewCacheStableAndInvalidated") {
    REQUIRE(callScript("testViewCacheStableAndInvalidated"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewCacheBaseClassIncludesSubclasses") {
    REQUIRE(callScript("testViewCacheBaseClassIncludesSubclasses"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.componentDefaultsAndSlotsCached") {
    REQUIRE(callScript("testComponentDefaultsAndSlotsCached"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.destroyClearsComponentState") {
    REQUIRE(callScript("testDestroyClearsComponentState"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.fieldSlotIsTheSameComponentInstance") {
    REQUIRE(callScript("testFieldSlotIsTheSameComponentInstance"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.registryOnlySlotHasNoField") {
    REQUIRE(callScript("testRegistryOnlySlotHasNoField"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.gpuDrivenBindingSurface") {
    REQUIRE(callScript("testGpuDrivenBindingSurface"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.gpgpuSurfaceWithoutAGraphicsDevice") {
    REQUIRE(callScript("testGpgpuSurfaceWithoutAGraphicsDevice"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewOrderIsEntityIdOrder") {
    REQUIRE(callScript("testViewOrderIsEntityIdOrder"));
}

TEST_CASE_FIXTURE(ScriptEcsTest, "ScriptECS.viewOrderSpansSubclassesByCreation") {
    REQUIRE(callScript("testViewOrderSpansSubclassesByCreation"));
}

static const char* kComponentRegistryScript = R"SQ(
function registerProbeComponent() {
    class Probe extends eve.Component { v = 0 }
    return eve.component("Probe", 1, Probe).ok
}

// 同一个 VM 内用同名同版本再注册一次：走"换一代"分支，此时旧引用属于**当前** VM，
// 必须照常释放——跨 VM 的修复不能把这条正常路径也一起关掉。
function registerProbeComponentAgain() {
    class Probe extends eve.Component { v = 1 }
    return eve.component("Probe", 1, Probe).ok
}
)SQ";

// `callScript` 是 protected，而本用例需要在同一个 TEST_CASE 里先后构造两个 VM。
class ComponentRegistryTwiceTest : public ScriptTest {
public:
    ComponentRegistryTwiceTest() : ScriptTest(kComponentRegistryScript) {}
    bool registerProbeComponent() { return callScript("registerProbeComponent"); }
    bool registerProbeComponentAgain() { return callScript("registerProbeComponentAgain"); }
};

TEST_CASE("ScriptECS.componentRegistrationCrossesVmsSafely") {
    {
        ComponentRegistryTwiceTest first;
        REQUIRE(first.registerProbeComponent());
        REQUIRE(first.registerProbeComponentAgain());  // 同 VM 替换：照常释放
    }
    // 第一个 VM 已在此处销毁。用同一个名字在第二个 VM 里再注册一次：
    // 注册表是进程级静态、名字相同就会走"替换旧条目"的分支，若那条分支去释放
    // 属于已销毁 VM 的引用，就是 use-after-free（见方案文档 §9.49）。
    ComponentRegistryTwiceTest second;
    REQUIRE(second.registerProbeComponent());
    REQUIRE(second.registerProbeComponentAgain());
}

// ---------------------------------------------------------------------------
// 组件注册表：名字 + 版本 + 代际（热重载迁移与引擎侧存储的共同前置）
// ---------------------------------------------------------------------------

static const char* kComponentIdentityScript = R"SQ(
class Position extends eve.Component { x = 0.0 }
class PositionV2 extends eve.Component { x = 0.0; y = 0.0 }

function registerOne() {
    return eve.component("Position", 1, Position).ok
}

function registerSecondVersion() {
    return eve.component("Position", 2, PositionV2).ok
}

// 同名同版本再登记：热重载换了一代。
function registerSameVersionAgain() {
    class Position extends eve.Component { x = 1.0 }
    return eve.component("Position", 1, Position).ok
}

function registerWithBadArguments() {
    local emptyName = eve.component("", 1, Position)
    local zeroVersion = eve.component("Zero", 0, Position)
    local notAClass = eve.component("NotAClass", 1, 42)
    return emptyName.ok || zeroVersion.ok || notAClass.ok
}

// 声明顺序 ≠ 成员表遍历顺序（§10.17 实测是哈希顺序），所以这条用例用一组刻意乱序的字段名。
class Inventory extends eve.Component {
    zeta = 1.0
    alpha = 2.0
    mid = 3.0
    beta = 4.0
    omega = 5.0
}

// 继承：基类字段在前。
class BaseStats extends eve.Component { hp = 0.0 }
class EnemyStats extends BaseStats { speed = 0.0 }

function registerSchemaCases() {
    local ordered = eve.component("Inventory", 1, Inventory).ok
    local derived = eve.component("EnemyStats", 1, EnemyStats).ok
    return ordered && derived
}

// 同名同版本换了一代，且字段集变了：墓碑要留住旧代的字段列表，迁移才知道搬什么。
function registerReshapedGeneration() {
    class Reshaped extends eve.Component { x = 0.0 }
    local first = eve.component("Reshaped", 1, Reshaped).ok
    class Reshaped2 extends eve.Component { x = 0.0; y = 0.0 }
    local second = eve.component("Reshaped", 1, Reshaped2).ok
    return first && second
}
)SQ";

UnitSciptTest(ComponentIdentityTest, kComponentIdentityScript);

// `callScript` 是 protected，而"注册表按 VM 隔离"这条要在同一个 TEST_CASE 里先后建两个 VM。
class ComponentIdentityTwiceTest : public ScriptTest {
public:
    ComponentIdentityTwiceTest() : ScriptTest(kComponentIdentityScript) {}
    bool registerOne() { return callScript("registerOne"); }
};

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRegistrationIsReadableFromCpp") {
    REQUIRE(callScript("registerOne"));

    const eve::ScriptComponentRecord* found = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(found != nullptr);
    REQUIRE_EQ(found->generation.value(), std::uint64_t(1));
    REQUIRE(!found->retired);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(1));
    REQUIRE_EQ(all[0].name, std::string("Position"));
    REQUIRE_EQ(all[0].version.value(), std::uint64_t(1));
    REQUIRE(!all[0].retired);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentVersionSeparatesIdentities") {
    REQUIRE(callScript("registerOne"));
    REQUIRE(callScript("registerSecondVersion"));

    // 同一名字的两个版本是两条独立身份，各自可查。
    const auto* v1 = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    const auto* v2 = eve::findScriptComponent("Position", eve::SchemaVersion(2));
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);
    REQUIRE_EQ(v1->generation.value(), std::uint64_t(1));
    REQUIRE_EQ(v2->generation.value(), std::uint64_t(1));
    REQUIRE(eve::findScriptComponent("Position", eve::SchemaVersion(3)) == nullptr);
    REQUIRE_EQ(eve::scriptComponentRecords().size(), std::size_t(2));
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentReRegistrationAdvancesTheGeneration") {
    REQUIRE(callScript("registerOne"));
    REQUIRE(callScript("registerSameVersionAgain"));

    // 当前那条是新代，旧代以墓碑形式留痕——热重载迁移要靠这条链判断"从哪一代搬到哪一代"。
    const auto* current = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(current != nullptr);
    REQUIRE_EQ(current->generation.value(), std::uint64_t(2));
    REQUIRE(!current->retired);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(2));
    REQUIRE_EQ(all[0].generation.value(), std::uint64_t(1));
    REQUIRE(all[0].retired);
    REQUIRE_EQ(all[1].generation.value(), std::uint64_t(2));
    REQUIRE(!all[1].retired);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRegistrationReportsBadArguments") {
    // 参数非法时必须让脚本看得见（返回结果表的 ok=false），而不是静默什么都没发生。
    REQUIRE(!callScript("registerWithBadArguments"));
    REQUIRE(eve::findScriptComponent("", eve::SchemaVersion(1)) == nullptr);
    REQUIRE(eve::findScriptComponent("Zero", eve::SchemaVersion(0)) == nullptr);
    REQUIRE(eve::findScriptComponent("NotAClass", eve::SchemaVersion(1)) == nullptr);
    REQUIRE(eve::scriptComponentRecords().empty());
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentSchemaKeepsDeclarationOrder") {
    REQUIRE(callScript("registerSchemaCases"));

    // 声明是 zeta, alpha, mid, beta, omega；成员表遍历是哈希顺序（§10.17），两者不同。
    const auto* inventory = eve::findScriptComponent("Inventory", eve::SchemaVersion(1));
    REQUIRE(inventory != nullptr);
    const std::vector<std::string> expected{"zeta", "alpha", "mid", "beta", "omega"};
    REQUIRE_EQ(inventory->fields, expected);

    // 基类字段在前，自身字段在后。
    const auto* enemy = eve::findScriptComponent("EnemyStats", eve::SchemaVersion(1));
    REQUIRE(enemy != nullptr);
    const std::vector<std::string> expectedDerived{"hp", "speed"};
    REQUIRE_EQ(enemy->fields, expectedDerived);
}

TEST_CASE_FIXTURE(ComponentIdentityTest, "ScriptECS.componentRetiredGenerationKeepsItsOwnFields") {
    REQUIRE(callScript("registerReshapedGeneration"));

    // 当前代是 {x, y}，墓碑代是 {x}：迁移要按这两组字段对照着搬。
    const auto* current = eve::findScriptComponent("Reshaped", eve::SchemaVersion(1));
    REQUIRE(current != nullptr);
    const std::vector<std::string> expectedCurrent{"x", "y"};
    REQUIRE_EQ(current->fields, expectedCurrent);

    const auto all = eve::scriptComponentRecords();
    REQUIRE_EQ(all.size(), std::size_t(2));
    REQUIRE(all[0].retired);
    const std::vector<std::string> expectedRetired{"x"};
    REQUIRE_EQ(all[0].fields, expectedRetired);
}

TEST_CASE("ScriptECS.componentRegistryIsPerVm") {
    // 代际数的是"同一个 VM 内重载了几次"，所以新 VM 必须从空开始。清理时机沿用既有约定
    // （与 cppEntityViews 同因）：**为新 VM 准备时**清空，而不是上一个 VM 析构时——所以
    // 这里不断言"第一个 VM 一销毁就为空"，而是断言第二个 VM 注册后**没有历史残留**。
    {
        ComponentIdentityTwiceTest first;
        REQUIRE(first.registerOne());
        REQUIRE(eve::findScriptComponent("Position", eve::SchemaVersion(1)) != nullptr);
    }

    ComponentIdentityTwiceTest second;
    REQUIRE(second.registerOne());
    const eve::ScriptComponentRecord* found = eve::findScriptComponent("Position", eve::SchemaVersion(1));
    REQUIRE(found != nullptr);
    // 不是 2：新 VM 没有上一代的记录；也不是两条：旧 VM 的登记没有残留。
    REQUIRE_EQ(found->generation.value(), std::uint64_t(1));
    REQUIRE_EQ(eve::scriptComponentRecords().size(), std::size_t(1));
}

// ---------------------------------------------------------------------------
// 引擎侧组件数据：登记过的组件在创建实体时按 schema 建一份，销毁时清掉。
// ---------------------------------------------------------------------------

static const char* kComponentStoreScript = R"SQ(
class Position extends eve.Component { x = 1.5; y = 2.5 }
// 没登记过的组件：保持纯槽位语义，不建引擎侧数据。
class Loose extends eve.Component { v = 7 }

class Probe extends eve.Entity {
    pos = Position
    loose = Loose
}

function makeProbe() {
    local registered = eve.component("Position", 1, Position).ok
    local e = Probe.create()
    probeId <- e.getId()
    probe <- e
    return registered
}

function destroyProbe() {
    local e = Probe.create()
    e.destroy()
    destroyedId <- e.getId()
    return true
}

function fieldSyntaxStillWorks() {
    local e = Probe.create()
    e.pos.x = 11.0
    return e.pos.x == 11.0 && e.pos.y == 2.5
}

// 脚本侧写 → 引擎侧读：`e.pos.x = v` 走 `_set`，落进引擎侧存储（不是实例槽位）。
function writeProbeX() {
    probe.pos.x = 42.0
    probe.pos.y = 43.0
    return true
}

// 引擎侧写 → 脚本侧读：两边是同一份数据，脚本下一次读就看到新值。
function readProbeX() {
    probeX <- probe.pos.x
    return true
}

// schema 之外的字段名必须报错，而不是静默长出一个没人声明的字段。
function unknownFieldIsRejected() {
    try { probe.pos.notDeclared = 1.0 } catch (err) { return true }
    return false
}

// `_get` 对非字段名是**抛错**的，而 `SQVM::Get` 只在元方法"干净地找不到"时才去问实例默认委托——
// 所以接缝会把委托里的六个方法（getclass/rawget/rawset/rawin/weakref/tostring）一起遮住。
// 引擎把委托原样装成类成员，这里钉住它。
function delegateMethodsSurviveTheSeam() {
    local c = probe.pos
    if (c.getclass() != Position) return false
    if (typeof c.tostring() != "string") return false
    // `rawin` 走的是类成员表：登记的组件字段槽位**确实**被摘掉了，所以这里是 false，
    // 而它本身能被查到（没被 `_get` 遮住）。
    if (c.rawin("x")) return false
    return true
}

// 用户类自己写了 `_get`/`_set`：引擎既不覆盖也不摘槽位，登记失败，槽位语义照旧。
class OwnSeam extends eve.Component {
    x = 1.0
    function _get(k) { return 0.0 }
    function _set(k, v) {}
}

function registerOwnSeam() {
    return eve.component("OwnSeam", 1, OwnSeam).ok
}

function ownSeamStillWorks() {
    local c = OwnSeam()
    return c.x == 1.0
}

// 登记过的组件槽位被摘掉了，但声明里的"类型标记"照旧要解析成真值（`eve.Number` → `0.0`）。
class Marked extends eve.Component {
    n = eve.Number
    b = eve.Boolean
    s = eve.String
    k = 3.5
}

class MarkedHolder extends eve.Entity {
    m = Marked
}

function registerMarked() {
    local registered = eve.component("Marked", 1, Marked).ok
    local e = MarkedHolder.create()
    markedId <- e.getId()
    return registered && e.m.n == 0.0 && e.m.b == false && e.m.s == "" && e.m.k == 3.5
}
)SQ";

UnitSciptTest(ComponentStoreTest, kComponentStoreScript);

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.registeredComponentValuesLiveInTheEngineStore") {
    REQUIRE(callScript("makeProbe"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Position", eve::SchemaVersion(1)};

    // 登记过的组件：字段默认值已经按 schema 落在引擎侧，原生侧读得到。
    const eve::Value* values = eve::findScriptComponentValues(key);
    REQUIRE(values != nullptr);
    REQUIRE(values->find("x") != nullptr);
    REQUIRE(values->find("y") != nullptr);
    REQUIRE_EQ(values->find("x")->asDouble(), 1.5);
    REQUIRE_EQ(values->find("y")->asDouble(), 2.5);

    // 没登记过的组件不建这份数据（它保持纯槽位语义）。
    const eve::ScriptComponentStoreKey looseKey{entityId, "Loose", eve::SchemaVersion(1)};
    REQUIRE(eve::findScriptComponentValues(looseKey) == nullptr);

    // 字段写入/读取：这正是脚本 `_set` 与原生侧共用的那条路。
    REQUIRE(eve::setScriptComponentField(key, "x", eve::Value::number(9.0)).ok());
    const auto readBack = eve::scriptComponentField(key, "x");
    REQUIRE(readBack.ok());
    REQUIRE_EQ(readBack.value().asDouble(), 9.0);

    // schema 之外的字段名必须被拒绝，否则序列化/编辑器会看到没有声明的数据。
    REQUIRE(!eve::setScriptComponentField(key, "notDeclared", eve::Value::number(1.0)).ok());
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.destroyDropsEngineSideComponentValues") {
    REQUIRE(callScript("destroyProbe"));

    const auto                         destroyedId = static_cast<std::uint64_t>(vm.find("destroyedId").toInt());
    const eve::ScriptComponentStoreKey key{destroyedId, "Position", eve::SchemaVersion(1)};

    // 销毁后不许留下"仍可读"的孤儿数据。
    REQUIRE(eve::findScriptComponentValues(key) == nullptr);
    REQUIRE(!eve::scriptComponentField(key, "x").ok());
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.unregisteredComponentsKeepSlotSyntax") {
    // 未登记组件这条路上什么都不该变：写法照旧、值照旧。
    REQUIRE(callScript("fieldSyntaxStillWorks"));
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.registeredComponentFieldsRoundTripBothWays") {
    REQUIRE(callScript("makeProbe"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Position", eve::SchemaVersion(1)};

    // 脚本侧写 → 引擎侧读。
    REQUIRE(callScript("writeProbeX"));
    const auto written = eve::scriptComponentField(key, "x");
    REQUIRE(written.ok());
    REQUIRE_EQ(written.value().asDouble(), 42.0);

    // 引擎侧写 → 脚本侧读：同一份数据，脚本下一次读就看到新值。
    REQUIRE(eve::setScriptComponentField(key, "x", eve::Value::number(7.0)).ok());
    REQUIRE(callScript("readProbeX"));
    REQUIRE_EQ(vm.find("probeX").toFloat(), 7.0);
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.unknownComponentFieldIsRejectedByTheSeam") {
    REQUIRE(callScript("makeProbe"));
    // 字段接缝不接受 schema 之外的字段名：凭空加字段会让存档/编辑器看到没声明的数据。
    REQUIRE(callScript("unknownFieldIsRejected"));
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.registeredComponentKeepsInstanceDelegateMethods") {
    // 反向取证：这条用例在装 `installInstanceDelegateMembers` 之前会抛
    // "'getclass' is not a declared field of component 'Position'"。
    REQUIRE(callScript("makeProbe"));
    REQUIRE(callScript("delegateMethodsSurviveTheSeam"));
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.componentDefiningItsOwnFieldSeamIsRefused") {
    // 用户类自己定义了 `_get`/`_set`：登记失败，且引擎**不做任何改动**（槽位语义照旧）。
    REQUIRE(!callScript("registerOwnSeam"));
    REQUIRE(callScript("ownSeamStillWorks"));
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.registeredComponentResolvesTypeMarkers") {
    // 槽位摘掉之后声明值只在引擎侧；"类型标记"仍要解析成真值，这靠引擎把字段名还给脚本。
    REQUIRE(callScript("registerMarked"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("markedId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Marked", eve::SchemaVersion(1)};
    const eve::Value*                  values = eve::findScriptComponentValues(key);
    REQUIRE(values != nullptr);
    REQUIRE_EQ(values->find("n")->asDouble(), 0.0);
    REQUIRE(!values->find("b")->asBool());
    REQUIRE_EQ(values->find("k")->asDouble(), 3.5);
}

// ---------------------------------------------------------------------------
// 热重载迁移（方案文档实施顺序第 3 步）：同名同版本重新登记 = 换了一代，已存在的字段数据
// 必须按新 schema 对齐。字段数据按 (实体, 名字, 版本) 寻址，所以换代时能就地迁移。
// ---------------------------------------------------------------------------

static const char* kComponentMigrationScript = R"SQ(
class MigPos extends eve.Component { x = 1.0 }
class MigNode extends eve.Entity { mp = MigPos }

function makeMig() {
    migOk <- eve.component("MigPos", 1, MigPos).ok
    local a = MigNode.create()
    a.mp.x = 42.0
    migId <- a.getId()
    migComp <- a.mp
    return true
}

// 热重载重定义了组件类：新增一个字段。
function redefineWithNewField() {
    class MigPos2 extends eve.Component { x = 1.0; y = 7.5 }
    return eve.component("MigPos", 1, MigPos2).ok
}

// 再换一代：这次删掉一个字段。
function redefineDroppingField() {
    class MigPos3 extends eve.Component { y = 0.0 }
    return eve.component("MigPos", 1, MigPos3).ok
}

// 迁移之后，老实体的组件实例（还是旧类的）读到的仍是引擎侧那份数据。
function readMigratedFields() {
    migX <- migComp.x
    migY <- migComp.y
    return migX == 42.0 && migY == 7.5
}
)SQ";

UnitSciptTest(ComponentMigrationTest, kComponentMigrationScript);

TEST_CASE_FIXTURE(ComponentMigrationTest, "ScriptECS.reRegistrationMigratesStoredValuesToTheNewSchema") {
    REQUIRE(callScript("makeMig"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("migId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "MigPos", eve::SchemaVersion(1)};
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asDouble(), 42.0);

    // 新代加了字段：老值保留，新字段取**新代声明的默认值**（不是 null，也不是旧代的形状）。
    REQUIRE(callScript("redefineWithNewField"));
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asDouble(), 42.0);
    REQUIRE_EQ(eve::scriptComponentField(key, "y").value().asDouble(), 7.5);
    REQUIRE(callScript("readMigratedFields"));

    // 新代删了字段：存储里不许留下没人声明的数据（否则序列化/编辑器会看到它）。
    REQUIRE(callScript("redefineDroppingField"));
    REQUIRE(!eve::scriptComponentField(key, "x").ok());
    REQUIRE_EQ(eve::scriptComponentField(key, "y").value().asDouble(), 7.5);
}

// ---------------------------------------------------------------------------
// 热重载迁移（实施顺序第 3 步）：重定义同名类 = 同一类型的新代。老实体必须被新类接管，
// 不许变成"既不在新类视图里、也没销毁、也不报错"的孤儿——这正是 §9.42.5 量到的现场
// （`newClassView=0 oldClassView=2 oldStillAlive=true`）。
//
// 真实重载由 `load.nut` 驱动：阶段③重新执行编译好的 chunk（同名类因此换成新类对象）、
// 阶段⑤a 调 `eve._ecsMigrateAfterReload(重载前的根表快照)`。这里用同一套判据和同一个入口，
// 只把"重新执行 chunk"换成本地从 C++ 跑第二段脚本。
// ---------------------------------------------------------------------------

static const char* kHotReloadBeforeScript = R"SQ(
class HotPos extends eve.Component { x = 1.0 }
class HotNode extends eve.Entity { hp = HotPos }

function makeHot() {
    local ok = eve.component("HotPos", 1, HotPos).ok
    local a = HotNode.create(); a.hp.x = 5.0
    local b = HotNode.create(); b.hp.x = 6.0
    hotId0 <- a.getId()
    hotId1 <- b.getId()
    return ok
}

// 重载前抓根表快照：真实路径里这是 load.nut 的 capture_reload_bindings()。
function captureHotBindings() {
    hotSnapshot <- {}
    foreach (k, v in getroottable()) hotSnapshot[k] <- v
    return true
}

// §9.42.5 的两个计数：老类视图 / 新类视图。
function viewHotCounts() {
    hotOldView <- eve.view(hotSnapshot.HotNode).len()
    hotNewView <- eve.view(HotNode).len()
    return true
}

function migrateHot() {
    local migration = eve._ecsMigrateAfterReload(hotSnapshot, getroottable())
    hotMigrated <- migration.components
    hotUnresolved <- migration.unresolved
    hotTypes <- 0
    foreach (k, v in eve._ecsTypes) hotTypes += 1
    return true
}

// 迁移后：新类视图里的实体都拿得到**新组件类**的实例，且数据没丢（按 id 顺序 = 5.0/6.0）。
function readHotAfterMigration() {
    local list = eve.view(HotNode)
    if (list.len() != 2) return false
    local c0 = list[0].getComponent(HotPos)
    local c1 = list[1].getComponent(HotPos)
    if (c0 == null || c1 == null) return false
    return c0.x == 5.0 && c1.x == 6.0
}
)SQ";

// 等价于 load.nut 阶段③：重跑 chunk。同名类变成新类对象，同时这一代多了一个字段 y
// （也就是同一名字+版本的第 2 代，登记时字段数据会被对齐到新 schema）。
static const char* kHotReloadAfterScript = R"SQ(
class HotPos extends eve.Component { x = 1.0; y = 2.0 }
class HotNode extends eve.Entity { hp = HotPos }
eve.component("HotPos", 1, HotPos)
)SQ";

UnitSciptTest(HotReloadEcsTest, kHotReloadBeforeScript);

TEST_CASE_FIXTURE(HotReloadEcsTest, "ScriptECS.hotReloadMigrationHandsTheNewClassTheOldEntities") {
    REQUIRE(callScript("makeHot"));
    REQUIRE(callScript("captureHotBindings"));

    // 重跑 chunk：这两个全局绑定换成新类对象，老类对象只剩快照里那一个引用。
    vm.run(vm.compileSource(kHotReloadAfterScript));

    // 迁移前就是 §9.42.5 的现场：老实体只挂在老类名下，对新类视图完全不可见。
    REQUIRE(callScript("viewHotCounts"));
    REQUIRE_EQ(vm.find("hotOldView").toInt(), 2);
    REQUIRE_EQ(vm.find("hotNewView").toInt(), 0);

    REQUIRE(callScript("migrateHot"));
    REQUIRE_EQ(vm.find("hotUnresolved").toInt(), 0);
    REQUIRE_EQ(vm.find("hotMigrated").toInt(), 2);
    REQUIRE_EQ(vm.find("hotTypes").toInt(), 1);  // 旧类那一组被搬走，不留每重载一次多一组的孤儿项

    // 验收方式（实施顺序第 3 步）：迁移后新类视图里能看到迁移前的实体数。
    REQUIRE(callScript("viewHotCounts"));
    REQUIRE_EQ(vm.find("hotNewView").toInt(), 2);
    REQUIRE_EQ(vm.find("hotOldView").toInt(), 0);
    REQUIRE(callScript("readHotAfterMigration"));

    // 引擎侧数据同样被接管：两个实体的 x 保留旧值，新字段 y 取新代声明的默认值。
    const auto                         id0 = static_cast<std::uint64_t>(vm.find("hotId0").toInt());
    const auto                         id1 = static_cast<std::uint64_t>(vm.find("hotId1").toInt());
    const eve::ScriptComponentStoreKey key0{id0, "HotPos", eve::SchemaVersion(1)};
    const eve::ScriptComponentStoreKey key1{id1, "HotPos", eve::SchemaVersion(1)};
    REQUIRE_EQ(eve::scriptComponentField(key0, "x").value().asDouble(), 5.0);
    REQUIRE_EQ(eve::scriptComponentField(key1, "x").value().asDouble(), 6.0);
    REQUIRE_EQ(eve::scriptComponentField(key0, "y").value().asDouble(), 2.0);
}

// ---------------------------------------------------------------------------
// 脚本组件数据的持久化（实施顺序第 4 步 / J-3 的存档通路）
// ---------------------------------------------------------------------------

namespace {

eve::PersistentId scriptSnapshotId() {
    const auto parsed = eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    return parsed ? *parsed : eve::PersistentId();
}

/** 测试用的注入式摘要：只要求确定性，不声称任何强度（与其它快照用例同一套写法）。 */
eve::SnapshotHashProvider scriptSnapshotHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint8_t          value = 17;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : input) value = static_cast<std::uint8_t>(value * 31u + byte);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

/** 手搓一条 payload 条目（拒绝类用例要构造出"不该被接受"的载荷）。 */
eve::Value snapshotEntry(std::string entity, std::string name, std::int64_t version, eve::Value::Object fields) {
    return eve::Value::object({{"entity", eve::Value::string(std::move(entity))},
                               {"name", eve::Value::string(std::move(name))},
                               {"version", eve::Value::integer(version)},
                               {"fields", eve::Value::object(std::move(fields))}});
}

eve::Value snapshotPayload(eve::Value entry) {
    return eve::Value::object({{"components", eve::Value::array({std::move(entry)})}});
}

}  // namespace

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.snapshotRoundTripsEngineSideComponentValues") {
    REQUIRE(callScript("makeProbe"));
    REQUIRE(callScript("writeProbeX"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Position", eve::SchemaVersion(1)};

    const auto hash  = scriptSnapshotHash();
    const auto id    = scriptSnapshotId();
    auto       first = eve::serializeScriptComponentSnapshot(id, eve::Revision(3), eve::SimulationTick(5), hash);
    REQUIRE(first.ok());

    // 清空再恢复：数据必须原样回来——这就是"存档/重放读得到脚本组件"。
    eve::clearScriptComponentStore();
    REQUIRE(eve::findScriptComponentValues(key) == nullptr);

    REQUIRE(eve::restoreScriptComponentSnapshotJson(first.value(), hash).ok());
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asDouble(), 42.0);
    REQUIRE_EQ(eve::scriptComponentField(key, "y").value().asDouble(), 43.0);
    // 脚本侧读到的也是恢复后的值：两边是同一份数据，不需要额外同步。
    REQUIRE(callScript("readProbeX"));
    REQUIRE_EQ(vm.find("probeX").toFloat(), 42.0);

    // 确定性：同一份状态再采一次，序列化逐字节相同（哈希输入不含墙钟，条目按 (实体, 名字, 版本) 升序）。
    auto second = eve::serializeScriptComponentSnapshot(id, eve::Revision(3), eve::SimulationTick(5), hash);
    REQUIRE(second.ok());
    REQUIRE_EQ(second.value(), first.value());
}

TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.snapshotRejectsUnknownComponentsFieldsAndTamperedPayloads") {
    REQUIRE(callScript("makeProbe"));

    const auto                         entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Position", eve::SchemaVersion(1)};
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asDouble(), 1.5);

    const auto hash = scriptSnapshotHash();
    const auto id   = scriptSnapshotId();
    const auto seal = [&](eve::Value payload) {
        auto envelope = eve::makeSnapshotEnvelope(eve::kScriptComponentSnapshotType,
                                                  eve::scriptComponentSnapshotSchema(), eve::SchemaVersion(1), id,
                                                  eve::Revision(1), eve::SimulationTick(1), std::move(payload), hash);
        REQUIRE(envelope.ok());
        return std::move(envelope).takeValue();
    };
    const auto stillOriginal = [&]() { return eve::scriptComponentField(key, "x").value().asDouble() == 1.5; };

    // 1) 未知组件：这份数据描述的东西这个版本读不出来 → 明确拒绝（不是静默收下）。
    auto unknownComponent = seal(snapshotPayload(
        snapshotEntry(std::to_string(entityId), "NoSuchComponent", 1, {{"x", eve::Value::number(9.0)}})));
    REQUIRE(!eve::restoreScriptComponents(unknownComponent, hash).ok());
    REQUIRE(stillOriginal());

    // 2) schema 之外的字段：同样拒绝——否则存储里会出现没人声明的字段，序列化/编辑器都会看到它。
    auto unknownField = seal(
        snapshotPayload(snapshotEntry(std::to_string(entityId), "Position", 1,
                                      {{"x", eve::Value::number(9.0)}, {"notDeclared", eve::Value::number(1.0)}})));
    REQUIRE(!eve::restoreScriptComponents(unknownField, hash).ok());
    REQUIRE(stillOriginal());

    // 3) 被改过的载荷：哈希对不上，拒绝且不改动现有数据。
    auto tampered    = seal(snapshotPayload(snapshotEntry(
        std::to_string(entityId), "Position", 1, {{"x", eve::Value::number(9.0)}, {"y", eve::Value::number(9.0)}})));
    tampered.payload = snapshotPayload(snapshotEntry(
        std::to_string(entityId), "Position", 1, {{"x", eve::Value::number(123.0)}, {"y", eve::Value::number(9.0)}}));
    REQUIRE(!eve::restoreScriptComponents(tampered, hash).ok());
    REQUIRE(stillOriginal());

    // 4) 正例对照：同一套构造、没被改过、字段合法 → 收下。
    auto accepted = seal(snapshotPayload(snapshotEntry(
        std::to_string(entityId), "Position", 1, {{"x", eve::Value::number(9.0)}, {"y", eve::Value::number(9.0)}})));
    REQUIRE(eve::restoreScriptComponents(accepted, hash).ok());
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asDouble(), 9.0);
}

// 工具面（MCP 工具 `eve_script_components` 的内容）：引擎侧脚本组件数据的确定性 JSON 视图。
TEST_CASE_FIXTURE(ComponentStoreTest, "ScriptECS.toolJsonReportsEngineSideComponentData") {
    REQUIRE(callScript("makeProbe"));
    REQUIRE(callScript("writeProbeX"));

    const auto entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());

    auto json = eve::scriptComponentStatesJson();
    REQUIRE(json.ok());
    const std::string& text = json.value();
    // 实体 id 是十进制字符串；组件带名字/版本/代际与字段值（脚本写进存储的 42/43）。
    CHECK(text.find("\"entities\"") != std::string::npos);
    CHECK(text.find("\"" + std::to_string(entityId) + "\"") != std::string::npos);
    CHECK(text.find("\"Position\"") != std::string::npos);
    CHECK(text.find("\"version\":1") != std::string::npos);
    CHECK(text.find("\"x\":42") != std::string::npos);
    CHECK(text.find("\"y\":43") != std::string::npos);
    // 未登记的组件（Loose）不在工具视图里——它没有名字与版本，工具无从寻址。
    CHECK(text.find("\"Loose\"") == std::string::npos);

    // 没有数据时也是合法 JSON 的空列表：工具不该因为"还没登记组件"就报错。
    eve::clearScriptComponentStore();
    auto empty = eve::scriptComponentStatesJson();
    REQUIRE(empty.ok());
    CHECK(empty.value().find("\"entities\":[]") != std::string::npos);
}
}  // namespace test_ScriptECS
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_ability_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "action/AbilityAsset.h"
#include "action/AbilityRuntime.h"
#include "action/ActionModule.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_ability_script.cpp
namespace test_action_ability_script {
namespace {

eve::action::AbilityDefinition lightAttack() {
    eve::action::AbilityDefinition definition;
    definition.id                    = *eve::LogicalId::fromParts("ability", "light-attack");
    definition.action.id             = *eve::LogicalId::fromParts("action", "light-attack");
    definition.action.timing.windup  = eve::Duration::fromNanoseconds(100'000'000);
    definition.action.timing.active  = eve::Duration::fromNanoseconds(200'000'000);
    definition.action.timing.recover = eve::Duration::fromNanoseconds(200'000'000);
    definition.cooldown              = eve::Duration::fromNanoseconds(500'000'000);
    definition.instancing            = eve::action::AbilityInstancingPolicy::PerOwner;
    definition.activationGroup       = eve::action::AbilityActivationGroup::Independent;
    definition.triggers.push_back({"Gameplay.Input.Attack", eve::tags::GameplayTagMatch::Exact});
    return definition;
}

}  // namespace

TEST_CASE("actionScript.runtimeOwnsAbilityLifecycle") {
    auto encoded = eve::action::encodeAbilityAsset(lightAttack());
    REQUIRE(encoded.ok());
    auto abilityJson = encoded.value().toJson();
    REQUIRE(abilityJson.ok());

    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::action::Action::expose(eve);
    const std::string abilityLiteral = abilityJson.value();
    eve.addFunc("abilityJson", [abilityLiteral]() { return abilityLiteral; });
    vm.run(vm.compileSource(R"(
        action <- eve.Action();
        created <- action.newRuntime();
        runtime <- created.value;
        registered <- runtime.registerAbilityJson(eve.abilityJson());
        granted <- runtime.grantAbility("fighter:player", "ability:light-attack");
        grantId <- granted.value.grantId;
        activated <- runtime.activateAbility(grantId, 1);
        blocked <- runtime.activateAbility(grantId, 1);
        stepped <- runtime.advanceAbilities(2, 0.10);
        cooling <- runtime.abilityGrant(grantId);
        finished <- runtime.advanceAbilities(3, 0.50);
        ready <- runtime.abilityGrant(grantId);
        matched <- runtime.matchingAbilities("fighter:player", "Gameplay.Input.Attack");
        submitted <- runtime.submitAction("action:direct", 0, 100000000, 0, 10);
        advanced <- runtime.advanceAction(submitted.value.executionId, 11, 0.05);
        found <- runtime.findAction(submitted.value.executionId);
        invalid <- runtime.grantAbility("fighter:player", "ability:missing");
        coolingSeconds <- cooling.value.cooldownSeconds;
        finishedActive <- finished.value.activeCount;
        readySeconds <- ready.value.cooldownSeconds;
        matchCount <- matched.value.len();
        foundPhase <- found.value.phase;
        ownership <- runtime.ownership();
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK_EQ(vm.find("ownership").toString(), std::string("owned"));
    CHECK(vm.find("registered").toTable().get<bool>("ok"));
    CHECK(vm.find("granted").toTable().get<bool>("ok"));
    CHECK(vm.find("activated").toTable().get<bool>("ok"));
    CHECK(!vm.find("blocked").toTable().get<bool>("ok"));
    CHECK(vm.find("stepped").toTable().get<bool>("ok"));
    CHECK(vm.find("coolingSeconds").toFloat() > 0.0f);
    CHECK_EQ(vm.find("finishedActive").toInt(), 0);
    CHECK_EQ(vm.find("readySeconds").toFloat(), 0.0f);
    CHECK(vm.find("matched").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("matchCount").toInt(), 1);
    CHECK(vm.find("submitted").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("found").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
}

TEST_CASE("actionScript.advanceAbilitiesSynchronizesTerminalFailures") {
    auto encoded = eve::action::encodeAbilityAsset(lightAttack());
    REQUIRE(encoded.ok());
    auto abilityJson = encoded.value().toJson();
    REQUIRE(abilityJson.ok());

    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::action::Action::expose(eve);
    const std::string abilityLiteral = abilityJson.value();
    eve.addFunc("abilityJson", [abilityLiteral]() { return abilityLiteral; });
    vm.run(vm.compileSource(R"(
        action <- eve.Action();
        runtime <- action.newRuntime().value;
        runtime.registerAbilityJson(eve.abilityJson());
        grantId <- runtime.grantAbility("fighter:player", "ability:light-attack").value.grantId;
        activated <- runtime.activateAbility(grantId, 1);
        executionId <- activated.value.executionId;
        // Cancel the underlying action without synchronizing the ability activation list.
        cancelled <- runtime.cancelAction(executionId, 1);
        failedAdvance <- runtime.advanceAbilities(2, 0.10);
        // After the failed advance, the terminal activation must have been synchronized away.
        recovered <- runtime.advanceAbilities(3, 0.10);
        recoveredActive <- recovered.value.activeCount;
    )"));
    CHECK(vm.find("activated").toTable().get<bool>("ok"));
    CHECK(vm.find("cancelled").toTable().get<bool>("ok"));
    CHECK(!vm.find("failedAdvance").toTable().get<bool>("ok"));
    CHECK(vm.find("recovered").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("recoveredActive").toInt(), 0);
}
}  // namespace test_action_ability_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_script)
#include "PrivateTestScope.h"
#include <filesystem>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PathBesideSource.h"
#include "agent/AgentModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// agent_script.cpp
namespace test_agent_script {
namespace {
void exposeAgent(ssq::VM& vm) {
    auto eve = vm.addTable("eve");
    auto cls = eve.addClass<eve::agent::Agent>("Agent");
    eve::agent::Agent::expose(cls);
}
}  // namespace
TEST_CASE("agent.scriptTrainInferReplayExample") {
    ssq::VM vm(1024);
    exposeAgent(vm);
    const auto path = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() /
                      "examples/agent/train.nut";
    vm.run(vm.compileFile(path.string().c_str()));
    vm.run(vm.compileSource(R"(
        assert(agentExampleReport.policy.schemaId == "evengine.agent.policy");
        assert(agentExampleReport.trainingSamples > 0);
        assert(agentExampleReport.best.environmentSeed == "42");
        local a = eve.Agent();
        local unavailable = a.inferTensor(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!unavailable.ok && unavailable.code == "unsupported");
        local untouched = {resets=0,reset=function(seed){resets++;},step=function(action,dt){}};
        local unavailableRun = a.run({backend="tensor"}, untouched);
        assert(!unavailableRun.ok && unavailableRun.code == "unsupported" && untouched.resets == 0);
        local gpuRun = a.run({backend="gpu"}, untouched);
        assert(!gpuRun.ok && gpuRun.code == "unsupported" && untouched.resets == 0);
        local gpuInfer = a.inferGpu(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!gpuInfer.ok && gpuInfer.code == "unsupported");
        local gpuAct = a.actGpu(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!gpuAct.ok && gpuAct.code == "unsupported");
        local bad = a.run({unknown=1}, {});
        assert(!bad.ok);
    )"));
}
TEST_CASE("agent.scriptCallbackFailureAndReentry") {
    ssq::VM vm(1024);
    exposeAgent(vm);
    vm.run(vm.compileSource(R"(
        local a = eve.Agent();
        local e = { reset=function(seed) { throw "injected callback error"; }, step=function(action,dt) {} };
        local r = a.run({}, e);
        assert(!r.ok && r.diagnostics[0].code != 0);
        e.reset = function(seed) {
            local nested = a.run({}, this);
            assert(!nested.ok && nested.code == "conflict");
            return {features=[0.0],legalActions=[],outcome="success"};
        };
        local recovered = a.run({population=1,elites=1,generations=1}, e);
        assert(recovered.ok);
        assert(recovered.value.steps == 0);
    )"));
}
}  // namespace test_agent_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_tensor_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "agent/Agent.h"
#include "agent/AgentModule.h"
#include "agent/tensor/TensorBackend.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// agent_tensor_script.cpp
namespace test_agent_tensor_script {
TEST_CASE("agent.scriptTensorTrainingLoop") {
    using namespace eve::agent;
    auto made = makeTensorBackend();
    REQUIRE(made.ok());
    auto provider = std::move(made).takeValue();
    struct Scope {
        IPolicyBackend& backend;
        explicit Scope(IPolicyBackend& b) : backend(b) { eve::cap::provide<IPolicyBackend>(&b); }
        ~Scope() { eve::cap::revoke<IPolicyBackend>(&backend); }
    } scope(*provider);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Agent>("Agent");
    Agent::expose(cls);
    vm.run(vm.compileSource(R"(
        local a = eve.Agent();
        local e = { reset=function(seed) {return {features=[0.0],legalActions=[0,1]};},
                    step=function(action,dt) {return {features=[0.5],legalActions=[],reward=1.0,outcome="success"};} };
        local r = a.run({backend="tensor",population=2,elites=1,generations=2,randomProbability=0.0}, e);
        assert(r.ok);
        assert(r.value.backend == "tensor-cpu");
        assert(r.value.trainingSamples > 0);
        local o = {features=[0.1],legalActions=[1]};
        local p = a.inferTensor(r.value.policy,o);
        assert(p.ok && p.value[0] == 0.0 && p.value[1] == 1.0);
        local action = a.actTensor(r.value.policy,o);
        assert(action.ok && action.value == 1);
    )"));
}
}  // namespace test_agent_tensor_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_clip_editing)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "animation/AnimClip.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_clip_editing.cpp
namespace test_animation_clip_editing {
using namespace eve::animation;


TEST_CASE("animation.script.composesClipTimelineFromReflectedKeys") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        anim <- eve.Animation();
        clip <- anim.newClip("project-action");
        clip.setDuration(2.0);
        clip.addPositionKey(0, 0.0, 0.0, 0.0, 0.0);
        clip.addPositionKey(0, 1.0, 1.0, 2.0, 3.0);
        edited <- clip.setPositionKey(0, 1, 0.5, 4.0, 5.0, 6.0);
        keyTime <- clip.getPositionKeyTime(0, 1);
        keyX <- clip.getPositionKeyX(0, 1);
        clip.addEvent(0.25, "step", "left");
        eventEdited <- clip.setEvent(0, 0.75, "land", "hard");
        eventTime <- clip.getEventTime(0);
        eventName <- clip.getEventName(0);
        trackCount <- clip.getTrackCount();
        keyRemoved <- clip.removePositionKey(0, 0);
    )"));

    CHECK(vm.find("edited").toBool());
    CHECK_EQ(vm.find("keyTime").toFloat(), 0.5f);
    CHECK_EQ(vm.find("keyX").toFloat(), 4.f);
    CHECK(vm.find("eventEdited").toBool());
    CHECK_EQ(vm.find("eventTime").toFloat(), 0.75f);
    CHECK_EQ(vm.find("eventName").toString(), std::string("land"));
    CHECK_EQ(vm.find("trackCount").toInt(), 1);
    CHECK(vm.find("keyRemoved").toBool());
}
}  // namespace test_animation_clip_editing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_async)
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "platform_event/PlatformEvent.h"
#include "scripts.h"
#include "thread/Thread.h"
#include "timer/Timer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// async.cpp
namespace test_async {
namespace {

[[maybe_unused]] void exposeCore(ssq::VM& vm) { eve::ModuleManager::expose(vm); }

[[maybe_unused]] std::string runAsyncSnippet(const std::string& body) {
    ssq::VM vm(1024, ssq::Libs::ALL);
    exposeCore(vm);
    {
        ssq::Table eve(vm.find("eve"));
        eve.set("asyncScript", std::string(eve::async_content ? eve::async_content : ""));
    }

    // Minimal globals expected by async.nut / load prelude.
    std::string prelude = R"(
        timer <- eve.Timer();
        thread <- eve.Thread();
        event <- eve.PlatformEvent();
        if ("asyncScript" in eve && eve.asyncScript != "")
            compilestring(eve.asyncScript)();
    )";

    vm.run(vm.compileSource((prelude + "\n" + body).c_str()));
    // Return a global `result` string if set.
    try {
        return vm.find("result").toString();
    } catch (...) {
        return {};
    }
}

}  // namespace

TEST_CASE("async.promise.resolveThen") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        local p = Promise.resolve(42);
        p.then(function(v) { result = "" + v; });
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("42"));
}

TEST_CASE("async.setTimeout") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        setTimeout(function() { result = "fired"; }, 5);
        local t0 = timer.getTime();
        while (timer.getTime() - t0 < 0.05) {
            // busy wait wall time; timer uses SDL performance counter
        }
        // Advance timer.step so getTime keeps moving; fire timers.
        for (local i = 0; i < 5; i++) {
            timer.step();
            async_pump();
        }
        // If still pending, force due by pumping after sleep via thread
        if (result == "pending") {
            local pool = thread.getPool();
            local task = pool.submitSleep(20);
            task.wait();
            timer.step();
            async_pump();
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("fired"));
}

TEST_CASE("async.asyncSleep") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        asyncSleep(1).then(function(v) { result = "ok"; });
        local pool = thread.getPool();
        local task = pool.submitSleep(15);
        task.wait();
        for (local i = 0; i < 8; i++) {
            timer.step();
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("ok"));
}

TEST_CASE("async.asyncDelay.worker") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        asyncDelay(5, "hello").then(function(v) { result = v; });
        local pool = thread.getPool();
        local task = pool.submitSleep(30);
        task.wait();
        for (local i = 0; i < 10; i++) {
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("hello"));
}

TEST_CASE("async.asyncPost.sameNameCorrelatedByTask") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        local slow = "";
        local fast = "";
        asyncPost("same-name-correlation", "slow", 25).then(function(v) { slow = v; });
        asyncPost("same-name-correlation", "fast", 1).then(function(v) { fast = v; });
        thread.getPool().waitAll();

        local name = event.poll();
        while (name != "") {
            async_dispatch_event(name, event.getLastData());
            name = event.poll();
        }
        for (local i = 0; i < 6; i++)
            async_pump();
        result = slow + "," + fast;
    )");
    CHECK_EQ(out, std::string("slow,fast"));
}


TEST_CASE("async.Promise.all") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        Promise.all([Promise.resolve(1), Promise.resolve(2)]).then(function(arr) {
            result = "" + arr[0] + "," + arr[1];
        });
        async_pump();
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("1,2"));
}

TEST_CASE("async.asyncSeq") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        asyncSeq([
            function() { return Promise.resolve(1); },
            function(v) { return Promise.resolve(v + 2); },
            function(v) { return v * 10; },
        ]).then(function(v) { result = "" + v; });
        async_pump();
        async_pump();
        async_pump();
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("30"));
}

TEST_CASE("async.eveScriptAwaitStateMachine") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        class Counter {
            offset = 4;
            async function compute(limit: int) -> int {
                local total = this.offset;
                for (local i = 0; i < limit; ++i)
                    total += await Promise.resolve(i);
                try {
                    await Promise.reject("expected");
                } catch (e) {
                    total += 10;
                }
                return total;
            }
        }
        Counter().compute(4).then(
            function(v) { result = "" + v; },
            function(e) { result = "error:" + e; }
        );
        for (local i = 0; i < 32; ++i)
            async_pump();
    )");
    CHECK_EQ(out, std::string("20"));
}

TEST_CASE("async.hotReloadCancelsOldContinuations") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        async function wait_forever() {
            await Promise(function(resolve, reject) {});
            return "unexpected";
        }
        wait_forever().then(
            function(v) { result = v; },
            function(e) { result = e; }
        );
        async_cancel_continuations("test reload");
        async_pump();
        async_pump();
    )");
    CHECK(out.find("async continuation cancelled: test reload") != std::string::npos);
}
}  // namespace test_async
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_authority)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "authority/Authority.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// authority.cpp
namespace test_authority {
using namespace eve::authority;


TEST_CASE("authority.script.explainQueryAndLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Authority();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local grantResult = store != null ? store.grant("actor:7", "scope:2", "operate", "role:3", 5, 0.0) : { ok = false };
        local denyResult = store != null ? store.deny("actor:7", "scope:2", "operate", "policy:9", 5, 1.0) : { ok = false };
        local grantId = grantResult.ok ? grantResult.value : "";
        local denyId = denyResult.ok ? denyResult.value : "";
        local before = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null) {
            store.update(1.0);
        }
        local after = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null && after != null &&
            !before.isAllowed() && before.getWinningRuleId() == denyId &&
            after.isAllowed() && after.getWinningRuleId() == grantId &&
            store.queryActor("actor:7") == 1 && store.eventAt(2).getKind() == "expired") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_authority
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "animation/Tween.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarInstance.h"
#include "avatar/Live2DNullBackend.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "inventory/Equipment.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// avatar.cpp
namespace test_avatar {
// avatar.cpp
using namespace eve::avatar;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;

namespace {

class FakeLive2D : public ILive2DBackend {
public:
    std::string getName() const override { return "fake"; }
    bool        loadModel(const std::string& path) override {
        loadedPath = path;
        return !path.empty();
    }
    void  update(float) override { ++updates; }
    void  setParameter(const std::string& name, float value) override { params[name] = value; }
    float getParameter(const std::string& name) const override {
        auto it = params.find(name);
        return it == params.end() ? 0.f : it->second;
    }
    bool setExpression(const std::string& name) override {
        expression = name;
        return true;
    }
    bool setMotion(const std::string& name) override {
        motion = name;
        return true;
    }

    std::string                            loadedPath;
    std::string                            expression;
    std::string                            motion;
    int                                    updates = 0;
    std::unordered_map<std::string, float> params;
};

[[maybe_unused]] ILive2DBackend* makeFakeLive2D() { return new FakeLive2D(); }

}  // namespace


TEST_CASE("avatar.script.buildsInspectorAndMotionBrowserFromMetadata") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        avatarModule <- eve.Avatar();
        animationModule <- eve.Animation();
        avatar <- avatarModule.newVroidAvatar();
        blinkDefined <- avatar.defineParameter("blink", 0.0, 0.0, 1.0);
        blinkMin <- avatar.getParameterMinimum("blink");
        blinkMax <- avatar.getParameterMaximum("blink");
        blinkDefault <- avatar.getParameterDefault("blink");
        clip <- animationModule.newClip("idle");
        motionRegistered <- avatar.registerMotion("idle", clip);
        motionCount <- avatar.getMotionCount();
        motionName <- avatar.getMotionName(0);
        sameClip <- avatar.getMotionClip("idle").getName();
        expressionDefined <- avatar.defineExpression("happy", "blink=0");
        expressionName <- avatar.getExpressionName(0);
        motionRemoved <- avatar.unregisterMotion("idle");
    )"));

    CHECK(vm.find("blinkDefined").toBool());
    CHECK_EQ(vm.find("blinkMin").toFloat(), 0.f);
    CHECK_EQ(vm.find("blinkMax").toFloat(), 1.f);
    CHECK_EQ(vm.find("blinkDefault").toFloat(), 0.f);
    CHECK(vm.find("motionRegistered").toBool());
    CHECK_EQ(vm.find("motionCount").toInt(), 1);
    CHECK_EQ(vm.find("motionName").toString(), std::string("idle"));
    CHECK_EQ(vm.find("sameClip").toString(), std::string("idle"));
    CHECK(vm.find("expressionDefined").toBool());
    CHECK_EQ(vm.find("expressionName").toString(), std::string("happy"));
    CHECK(vm.find("motionRemoved").toBool());
}


// avatar_attachment_point.cpp
}  // namespace test_avatar
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_binding_selfcheck)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// binding_selfcheck.cpp
namespace test_binding_selfcheck {
using namespace eve;


// Runtime binding self-check (companion to scripts/check_bindings.py): builds
// a bare script VM with every registered module exposed, then verifies curated
// "hot" bindings on light modules actually exist. Constructing a module class
// is a real smoke: it exercises ModuleManager::create + expose. (eve.reflect is
// covered by test/ReflectScript.cpp, which uses the full Runtime.)
TEST_CASE("binding.selfcheck.scriptSurface") {
    ScriptTest t(R"(
        local missing = [];

        // Light modules: constructor must succeed and hot bindings must exist.
        local hot = {
            Math = ["newVec2", "newMat4", "setRandomSeed"],
            DataModule = ["newByteData", "decodeJson", "encodeJson"],
            PlatformEvent = ["pump", "poll", "pushData"],
            Timer = ["getTime", "step"],
            Rx = ["newSubject"],
            Spatial = ["newQuadTree", "newOctree"],
            IK = ["newSkeleton2D", "newSolver2D"],
            RPG = ["newActor"],
            Inventory = ["newBag"],
            Climbing = ["newRuntime", "newAnchorGraphJson"],
        };
        foreach (cls, methods in hot) {
            // Module classes are installed lazily through eve's _get delegate.
            // Squirrel's `in` operator only checks existing slots and therefore
            // bypasses the resolver that this smoke test is meant to exercise.
            local ctor = null;
            try {
                ctor = eve[cls];
            } catch (e) {
                missing.push(cls + ":ctor-missing");
                continue;
            }
            if (ctor == null) { missing.push(cls + ":ctor-null"); continue; }
            local inst = null;
            try {
                inst = ctor();
            } catch (e) {
                missing.push(cls + ":ctor:" + e);
                continue;
            }
            if (inst == null) { missing.push(cls + ":ctor-null"); continue; }
            foreach (fn in methods) if (!(fn in inst)) missing.push(cls + "." + fn);
        }

        local msg = "";
        for (local i = 0; i < missing.len(); i++) {
            if (i > 0) msg += ",";
            msg += missing[i];
        }
        if (missing.len() > 0) throw "binding self-check failed: " + msg;
    )");
}
}  // namespace test_binding_selfcheck
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_building_placement_regression)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// building_placement_regression.cpp
namespace test_building_placement_regression {
// building_placement_regression.cpp
using namespace eve::building;


// building_visual_placement.cpp


TEST_CASE("buildingfx.scriptMeshResolverValidatesAndCopiesResources") {
    eve::graphics::Mesh mesh;
    ssq::VM             vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto eveTable = vm.find("eve").toTable();
    eve::graphics::Graphics::expose(eveTable);
    vm.addFunc("testHouseMesh", [&]() { return &mesh; });
    vm.run(vm.compileSource(R"(
        b <- eve.Building();
        b.registerBuildingsFromJson("[{\"id\":\"script-house\",\"renderMode\":\"3d\",\"visual3d\":{\"mesh\":\"house.obj\"}}]");
        world <- b.newWorld(4, 4, 1.0);
        fx <- eve.BuildingFx();
        resources <- { "house.obj": testHouseMesh() };
        fx.setMeshResolver(resources);
        resources.clear();
        rejected <- false;
        try { fx.setMeshResolver({ "house.obj": world }); }
        catch (error) { rejected = true; }
        fx.attach(world);
        id <- world.placeAt("script-house", 1, 1, 0.0);
        fx.sync(world);
        resolved <- fx.getVisualFallbackReason(world, id) == "";
        fx.clearMeshResolver();
        fx.sync(world);
        missing <- fx.getVisualFallbackReason(world, id) == "resolver_unavailable";
        fx.detach(world);
        world.destroy();
    )"));
    REQUIRE(vm.find("rejected").toBool());
    REQUIRE(vm.find("resolved").toBool());
    REQUIRE(vm.find("missing").toBool());
    eve::building::BuildingRegistry::clear();
}
}  // namespace test_building_placement_regression
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card)
#include <cmath>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "card/Card.h"
#include "common/ECS.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// card.cpp
namespace test_card {
using namespace eve;


using namespace eve::card;

namespace {

[[maybe_unused]] int countCardView() {
    int  n    = 0;
    auto view = ecs::View<CardData, CardData::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

[[maybe_unused]] bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

const char* kDefs = R"JSON(
[{"id":"flame","name":"Flame","kind":"creature","cost":2,"attack":3,"health":2},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

}  // namespace

TEST_CASE("card.factory.newCardFromJson") {
    Card mod;
    CHECK_EQ(mod.registerCardsFromJson(kDefs), 2);
    CHECK(mod.hasCardDefinition("flame"));
    CardData* c = mod.newCard("flame");
    REQUIRE(c != nullptr);
    CHECK_EQ(c->identity()->name, std::string("Flame"));
    CHECK_EQ(c->stats()->cost, 2);
    CHECK_EQ(c->stats()->attack, 3);
    CHECK_EQ(mod.newCard("missing"), nullptr);
}


TEST_CASE("card.ecs.removeKeepsEntityInView") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    const int     before = countCardView();
    LayoutConfig* cfg    = mod.newConfig();
    Hand*         h      = mod.newHand(cfg);
    CardData*     c      = mod.newCard("flame");
    h->addCard(c);
    CHECK_EQ(countCardView(), before + 1);
    CHECK(h->removeCard(c));
    CHECK_EQ(h->count(), 0);
    CHECK_EQ(countCardView(), before + 1);
    CHECK_EQ(c->identity()->name, std::string("Flame"));
}


static const char* kCardViewScript = R"SQ(
function testCardCppView() {
    local card = eve.Card()
    card.registerCardsFromJson("[{\"id\":\"flame\",\"name\":\"Flame\"}]")
    local c = card.newCard("flame")
    if (c == null) return false
    local all = eve.view(eve.CardData)
    foreach (e in all) {
        if (e.getId() == c.getId()) return true
    }
    return false
}
)SQ";

UnitSciptTest(CardViewScriptTest, kCardViewScript);

TEST_CASE_FIXTURE(CardViewScriptTest, "card.script.viewSeesNewCard") { REQUIRE(callScript("testCardCppView")); }


namespace {

[[maybe_unused]] void settleHand(Hand* h, const std::vector<Zone*>& zones, std::vector<CardEvent>& out) {
    for (int i = 0; i < 120; ++i) h->update(1.f / 60.f, 0.f, 0.f, false, zones, out);
    out.clear();
}

}  // namespace
}  // namespace test_card
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card_gameplay_control)
#include "PrivateTestScope.h"
#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "card/Card.h"
#include "card/CardTypes.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// card_gameplay_control.cpp
namespace test_card_gameplay_control {
// 卡牌玩法域契约测试：真实 Card 模块 + 手牌/牌库走共享玩法协议，
// 覆盖发现、手牌观察、抽取、出牌支付边界、作弊档位、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000a01";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000a02";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000a03";

constexpr const char* kDefinitions = R"JSON(
[{"id":"free","name":"Free","kind":"spell","cost":0},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

[[maybe_unused]] const eve::Value::Array* handCards(const eve::Value& state) {
    const eve::Value* hand = member(state, "hand");
    return hand == nullptr ? nullptr : hand->getIf<eve::Value::Array>();
}

/** 手牌里第一张指定定义的卡牌实例 id；找不到返回空串。 */
[[maybe_unused]] std::string firstCardOf(const eve::Value& state, const char* definition) {
    const auto* cards = handCards(state);
    if (cards == nullptr) return {};
    for (const auto& card : *cards) {
        const eve::Value* def = member(card, "definition");
        const eve::Value* id  = member(card, "card");
        if (def != nullptr && id != nullptr && def->asString() == definition) return id->asString();
    }
    return {};
}

/** 真实模块 + 手牌 + 牌库，并把该手牌发布到共享玩法协议（未绑定支付账户）。 */
struct CardFixture {
    eve::card::Card*         module = nullptr;
    eve::card::Hand*         hand   = nullptr;
    eve::card::LayoutConfig* config = nullptr;
    eve::SubjectRef          instance;
    eve::SubjectRef          owner;

    CardFixture() {
        module = eve::card::Card::create();
        REQUIRE(module != nullptr);
        module->clearGameplayControls();
        module->clearCardDefinitions();
        const int registered = module->registerCardsFromJson(kDefinitions);
        REQUIRE_EQ(registered, 2);
        config = module->newConfig();
        REQUIRE(config != nullptr);
        module->setConfig(config);
        hand = module->newHand(config);
        REQUIRE(hand != nullptr);
        hand->meta()->owner   = "player";
        eve::card::Deck* deck = module->newDeck();
        REQUIRE(deck != nullptr);
        deck->push(module->newCard("free"));
        deck->push(module->newCard("bolt"));
        eve::card::CardData* inHand = module->newCard("free");
        REQUIRE(inHand != nullptr);
        hand->addCard(inHand);
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = module->publishGameplay(kInstanceId, kOwnerId, hand);
        REQUIRE(published.ok());
    }

    ~CardFixture() {
        module->clearGameplayControls();
        module->clearCardDefinitions();
    }

    CardFixture(const CardFixture&)            = delete;
    CardFixture& operator=(const CardFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "card") found = candidate;
        });
        return found;
    }
};

[[maybe_unused]] eve::GameplayCommand command(const CardFixture& fixture, const char* id, const char* actionText,
                                              std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"card\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                                         const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


TEST_CASE("gameplay.control.cardScriptPublishesItsHand") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local card = eve.Card();
        card.clearGameplayControls();
        card.clearCardDefinitions();
        card.registerCardsFromJson("[{\"id\":\"free\",\"name\":\"Free\",\"kind\":\"spell\",\"cost\":0}]");
        local hand = card.newHand(card.newConfig());
        scriptPublished <- card.publishGameplay(
            "00000000-0000-7000-8000-000000000e01",
            "00000000-0000-7000-8000-000000000e02", hand).ok;
        scriptInstances <- card.getGameplayControlCount();
        local duplicate = card.publishGameplay(
            "00000000-0000-7000-8000-000000000e01",
            "00000000-0000-7000-8000-000000000e02", hand);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = card.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000e02", hand);
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- card.unpublishGameplay("00000000-0000-7000-8000-000000000e01").ok;
        scriptRemaining <- card.getGameplayControlCount();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}
}  // namespace test_card_gameplay_control
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_anchor_authoring)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include "PathBesideSource.h"
#include "climbing/ClimbingAnchorAuthoring.h"
#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingTelemetry.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_anchor_authoring.cpp
namespace test_climbing_anchor_authoring {
// climbing_anchor_authoring.cpp
namespace {

[[maybe_unused]] eve::climbing::ClimbingAnchorBakeRequest request() {
    eve::climbing::ClimbingAnchorBakeRequest value;
    value.graphId                  = "playground.route";
    value.sourceGeometryContentId  = "sha256:playground-geometry";
    value.settings.handSpacing     = 0.4f;
    value.settings.hangingFeetDrop = 1.4f;
    eve::climbing::ClimbingLedgeBakeSource ledge;
    ledge.id           = "wall";
    ledge.points       = {{0.f, 2.f, 0.f}, {1.f, 2.f, 0.f}, {1.f, 2.f, 1.f}};
    ledge.localNormals = {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}};
    ledge.tags         = {"stone"};
    value.ledges.push_back(std::move(ledge));
    value.ladders.push_back({"ladder", {3.f, 0.3f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, -1.f}, 0.3f, 0.5f, 4, {"metal"}});
    return value;
}

}  // namespace


// climbing_codec.cpp
namespace {

[[maybe_unused]] eve::climbing::ClimbingActionDefinition action(std::string id) {
    eve::climbing::ClimbingActionDefinition result;
    result.id                     = std::move(id);
    result.kind                   = eve::climbing::ClimbingActionKind::Vault;
    result.minHeight              = 0.3f;
    result.maxHeight              = 1.1f;
    result.duration               = eve::Duration::fromNanoseconds(450000000);
    result.requiredNotifies       = {"contact.left_hand", "land"};
    result.tags                   = {"parkour", "vault"};
    result.sourceModes            = eve::climbing::ClimbingSourceMode::Grounded;
    result.requiredCommand        = eve::climbing::ClimbingCommandRequirement::Jump;
    result.probeRecipe            = eve::climbing::ClimbingProbeRecipe::Obstacle;
    result.minDepth               = 0.2f;
    result.maxDepth               = 1.4f;
    result.minDistance            = 0.1f;
    result.maxDistance            = 1.5f;
    result.minSurfaceNormalY      = -0.2f;
    result.maxSlopeRadians        = 1.2f;
    result.maxCurvature           = 0.3f;
    result.requiredSupportTags    = {"solid"};
    result.trajectory             = eve::climbing::ClimbingTrajectoryKind::BallisticArc;
    result.animation              = {"anim:vault_low", "climbing.vault", "Root", false};
    result.branchWindows          = {{0.45f, 0.7f, "combo:mantle"}};
    result.contactConstraints     = {{eve::climbing::ClimbingContactTarget::LeftHand, 0.2f, 0.6f, 0.9f}};
    result.landingPolicy          = eve::climbing::ClimbingLandingPolicy::PreserveMomentum;
    result.terminalVelocityPolicy = eve::climbing::ClimbingTerminalVelocityPolicy::ClampDownward;
    result.comboTags              = {"combo:flow", "combo:mantle"};
    result.repetitionPenalty      = 125;
    result.requiredConditionTags  = {"ability:parkour"};
    result.staminaCost            = 7.5f;
    result.cameraCue              = "camera:vault";
    result.eventMetadata          = {"event:parkour"};
    result.extensionMetadata      = {{"plugin.cameraCue", eve::Value("close_follow")}};
    return result;
}

}  // namespace


// climbing_playground.cpp
TEST_CASE("climbing.playground.mainScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "climbing-playground" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    CHECK(source.str().find("math.sin") == std::string::npos);

    eve::Runtime runtime(2048, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.runSource("climbing_playground_sine_probe <- sin(0.0);", "climbing-playground-sine-probe.nut");
        runtime.compileSource(source.str(), "examples/climbing-playground/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}

// climbing_telemetry.cpp
using namespace eve;
using namespace eve::climbing;
}  // namespace test_climbing_anchor_authoring
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_carrier)
#include "PrivateTestScope.h"
#include <cmath>
#include <map>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>
#include "common/Diagnostic.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "weapon/CombatCarrier.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_carrier.cpp
namespace test_combat_carrier {
// combat_carrier.cpp
namespace {

[[maybe_unused]] eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return duration.value();
}

class TargetProvider final : public eve::weapon::IProjectileTargetProvider {
public:
    [[nodiscard]] eve::Result<eve::weapon::ProjectilePoint> position(ecs::EntityHandle target) const override {
        auto found = positions.find(target.id);
        if (reject || found == positions.end())
            return eve::Result<eve::weapon::ProjectilePoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "target unavailable"));
        return eve::Result<eve::weapon::ProjectilePoint>::success(found->second);
    }

    mutable bool                                          reject = false;
    std::map<std::uint32_t, eve::weapon::ProjectilePoint> positions;
};

class HitProbe final : public eve::weapon::ICarrierHitProbe {
public:
    [[nodiscard]] eve::Result<std::vector<Contact>> query(eve::weapon::CarrierHandle, const eve::weapon::CarrierMotion&,
                                                          const eve::weapon::CarrierMotion& current) const override {
        ++queries;
        if (reject)
            return eve::Result<std::vector<Contact>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "hit probe failed"));
        std::vector<Contact> contacts;
        for (const auto& planned : plannedHits) {
            if (std::fabs(current.position.x - planned.atX) < 1e-9) {
                Contact contact;
                contact.target = planned.target;
                contact.point  = current.position;
                contact.normal = planned.normal;
                contacts.push_back(contact);
            }
        }
        return eve::Result<std::vector<Contact>>::success(std::move(contacts));
    }

    struct PlannedHit {
        double                        atX = 0.0;
        ecs::EntityHandle             target{};
        eve::weapon::ProjectileVector normal{-1.0, 0.0, 0.0};
    };

    mutable int             queries = 0;
    mutable bool            reject  = false;
    std::vector<PlannedHit> plannedHits;
};

[[maybe_unused]] eve::weapon::CarrierRecipe linearShell(std::string_view recipeId, double damage) {
    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id(recipeId);
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::EmitHit, eve::weapon::CarrierTriggerKind::OnHit, damage});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});
    return recipe;
}

}  // namespace


class BodyDefenseProvider final : public eve::weapon::ICarrierBodyDefenseProvider {
public:
    [[nodiscard]] eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>> sample(
        eve::weapon::CarrierHandle, ecs::EntityHandle, const eve::weapon::CarrierMotion&) const override {
        return eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>>::success(bodies);
    }

    std::vector<eve::weapon::CarrierBodyDefenseSample> bodies;
};


// combat_carrier_script.cpp
TEST_CASE("combatCarrierScript.castAtTargetReportsHitEvents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        created <- weapon.newCarrierRuntime(16);
        pool <- created.value;
        pool.setTargetPosition(7, 1, 5.0, 0.0, 0.0);
        pool.addHitTarget(7, 1, 5.0, 0.0, 0.0, 0.6);
        casted <- pool.castAtTarget([
            { kind = "homing", turnRate = 90 },
            { kind = "pierce", count = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 12, lifetime = 2, element = "fire" }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 7, 1);
        missingTarget <- pool.cast([
            { kind = "homing", turnRate = 90 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 1, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        frame <- pool.update(0.5);
        eventCount <- frame.value.eventCount;
        firstImpact <- frame.value.events[0].impact;
        firstTrigger <- frame.value.events[0].trigger;
        preset <- pool.registerRecipe({
            id = "spell:shell",
            lifetime = 1.5,
            speed = 8.0,
            damage = 4.0,
            motion = "linear"
        });
        spawned <- pool.spawn("spell:shell", 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
        // Preset OnHit recipes must advance even with an empty hit list.
        emptyHitFrame <- pool.update(0.1);
        invalid <- pool.cast([
            { kind = "projectile", id = "spell:a", speed = 1, damage = 1, lifetime = 1 },
            { kind = "homing", turnRate = 10 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        oversized <- pool.addHitTarget(4294967296, 1, 0.0, 0.0, 0.0, 0.5);
        avoidBody <- pool.registerRecipe({
            id = "spell:avoid",
            lifetime = 1.0,
            speed = 5.0,
            motion = [ { kind = "avoidBody", avoidLookAhead = 2.0, maxTurnRateDegrees = 90.0 }, "linear" ],
            triggers = [ "onExpire" ],
            impacts = [ { kind = "release", on = "onExpire" } ]
        });
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK(vm.find("casted").toTable().get<bool>("ok"));
    CHECK(!vm.find("missingTarget").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("eventCount").toInt() >= 1);
    CHECK_EQ(vm.find("firstImpact").toString(), std::string("emitHit"));
    CHECK_EQ(vm.find("firstTrigger").toString(), std::string("onHit"));
    CHECK(vm.find("preset").toTable().get<bool>("ok"));
    CHECK(vm.find("spawned").toTable().get<bool>("ok"));
    CHECK(vm.find("emptyHitFrame").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(!vm.find("oversized").toTable().get<bool>("ok"));
    CHECK(!vm.find("avoidBody").toTable().get<bool>("ok"));
}

TEST_CASE("combatCarrierScript.castUniquifiesRecipeIds") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        pool <- weapon.newCarrierRuntime(16).value;
        first <- pool.cast([
            { kind = "damage", add = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        second <- pool.cast([
            { kind = "damage", multiply = 3 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 1.0, 0.0, 1.0, 0.0, 0.0);
        pool.addHitTarget(1, 1, 5.0, 0.0, 0.0, 0.6);
        pool.addHitTarget(2, 1, 5.0, 1.0, 0.0, 0.6);
        frame <- pool.update(0.5);
        firstRecipe <- first.value.recipeId;
        secondRecipe <- second.value.recipeId;
        eventCount <- frame.value.eventCount;
        dmgA <- frame.value.events[0].damage;
        dmgB <- frame.value.events[1].damage;
    )"));
    CHECK(vm.find("first").toTable().get<bool>("ok"));
    CHECK(vm.find("second").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("firstRecipe").toString() != vm.find("secondRecipe").toString());
    CHECK(vm.find("eventCount").toInt() >= 2);
    const double dmgA = vm.find("dmgA").toFloat();
    const double dmgB = vm.find("dmgB").toFloat();
    // First cast: 5+1=6; second: 5*3=15. Both must survive independently.
    const bool sawSix     = std::fabs(dmgA - 6.0) < 1e-6 || std::fabs(dmgB - 6.0) < 1e-6;
    const bool sawFifteen = std::fabs(dmgA - 15.0) < 1e-6 || std::fabs(dmgB - 15.0) < 1e-6;
    CHECK(sawSix);
    CHECK(sawFifteen);
}
}  // namespace test_combat_carrier
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "combat/Combat.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_script.cpp
namespace test_combat_script {
TEST_CASE("combatScript.runtimeOwnsMovementAndDamageState") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::combat::Combat::expose(eve);
    vm.run(vm.compileSource(R"(
        combat <- eve.Combat();
        created <- combat.newRuntime();
        arena <- created.value;
        player <- "01020304-0506-0708-890a-0b0c0d0e0f10";
        enemy <- "11121314-1516-1718-991a-1b1c1d1e1f20";
        registeredPlayer <- arena.registerFighter(player, "fighter:player", 0.0, 0.0, 100.0, 40.0, 4.0, 20.0);
        registeredEnemy <- arena.registerFighter(enemy, "fighter:enemy", 3.0, 0.0, 80.0, 30.0, 3.0, 15.0);
        moved <- arena.setMoveIntent(player, 1.0, 0.0, 1.0);
        advanced <- arena.advance(1, 0.25);
        playerState <- arena.state(player);
        damaged <- arena.applyDamage(player, enemy, "Damage.Physical.Slash", 18.0, 10.0, 0.0, 0.0, 0.0);
        timelineDamage <- arena.applyTimelineDamage(player, enemy,
            "{\"damageType\":\"Damage.Physical.Slash\",\"amount\":7,\"poiseAmount\":2}");
        enemyState <- arena.state(enemy);
        invalid <- arena.applyDamage("bad", enemy, "Damage.Physical.Slash", 1.0, 0.0, 0.0, 0.0, 0.0);
        grantedAbility <- arena.grantAbility("fighter:player", "combat-ability:light-attack");
        grantId <- grantedAbility.value.grantId;
        activatedAbility <- arena.activateAbility(grantId, 2);
        blockedAbility <- arena.activateAbility(grantId, 2);
        abilityStep <- arena.advanceAbilities(3, 0.10);
        coolingGrant <- arena.abilityGrant(grantId);
        abilityFinished <- arena.advanceAbilities(4, 0.40);
        readyGrant <- arena.abilityGrant(grantId);
        reactivatedAbility <- arena.activateAbility(grantId, 5);
        invalidAbility <- arena.grantAbility("fighter:player", "combat-ability:missing");
        coolingSeconds <- coolingGrant.value.cooldownSeconds;
        finishedActiveCount <- abilityFinished.value.activeCount;
        readySeconds <- readyGrant.value.cooldownSeconds;
        playerX <- playerState.value.position.x;
        enemyHealth <- enemyState.value.health;
        damageReaction <- damaged.value.reaction;
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("arena").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("registeredPlayer").toTable().get<bool>("ok"));
    CHECK(vm.find("registeredEnemy").toTable().get<bool>("ok"));
    CHECK(vm.find("moved").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playerX").toFloat() > 0.0f);
    CHECK_EQ(vm.find("enemyHealth").toFloat(), 55.0f);
    CHECK(vm.find("timelineDamage").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("damageReaction").toString(), std::string("flinch"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(vm.find("grantedAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("activatedAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("blockedAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("abilityStep").toTable().get<bool>("ok"));
    CHECK(vm.find("coolingSeconds").toFloat() > 0.0f);
    CHECK_EQ(vm.find("finishedActiveCount").toInt(), 0);
    CHECK_EQ(vm.find("readySeconds").toFloat(), 0.0f);
    CHECK(vm.find("reactivatedAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidAbility").toTable().get<bool>("ok"));
}
}  // namespace test_combat_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_composable_gameplay)
#include "PrivateTestScope.h"
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// composable_gameplay.cpp
namespace test_composable_gameplay {
TEST_CASE("composableGameplay.scriptBuildsRebellionWithoutDomainCpp") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/composable-rebellion/simulation.nut";
    std::ifstream     input(path);
    REQUIRE(input.good());
    std::ostringstream source;
    source << input.rdbuf();
    const std::string script = source.str();

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(script.c_str()));
    vm.run(vm.compileSource(R"(
        scenarioResult <- run_rebellion_scenario();
        scenarioOwner <- demoSocial.ownerOf(demoState["baseId"]);
        scenarioProduction <- demoState.baseStats.getFinal("production_speed", 0.0);
        scenarioOrder <- demoState.commandQueue.current().getKind();
        scenarioCanCommand <- demoTags.hasCapability(demoState.general, "command_army");
        scenarioEffect <- demoState.statusEffects.effectAt(0).getType();
        scenarioEvent <- demoState.events.at(0).getType();
        scenarioTransaction <- demoState.transactions.at(0).getState();
        scenarioStateOwner <- demoState.authoritativeState.get(demoState.baseId, "owner");
        scenarioRankResult <- demoState.definitions.resolve("rank", "rank.general");
        scenarioRank <- scenarioRankResult.ok ? scenarioRankResult.value.json : "";
        scenarioAuthority <- demoState.authority.can(demoState.general, demoState.baseId, "govern_base");
        scenarioPolicy <- demoState.policies.select("administration").getName();
        scenarioProductionA <- demoState.production.taskAt(0).getState() + ":" +
                               demoState.production.taskAt(0).getKind();
        scenarioProductionB <- demoState.production.taskAt(1).getState() + ":" +
                               demoState.production.taskAt(1).getKind();
        scenarioAiTarget <- demoState.aiTarget;
        scenarioAiState <- demoState.decisions.state("defense.ai");
        scenarioAiAction <- demoState.aiAction;
        scenarioAiVelocity <- demoState.aiVelocity;
        scenarioAiThreat <- demoState.aiThreat;
    )"));

    CHECK(vm.find("scenarioResult").toBool());
    CHECK_EQ(vm.find("scenarioOwner").toString(), std::string("faction.frontier"));
    CHECK_EQ(vm.find("scenarioProduction").toFloat(), 1.4F);
    CHECK_EQ(vm.find("scenarioOrder").toString(), std::string("secure_assets"));
    CHECK(vm.find("scenarioCanCommand").toBool());
    CHECK_EQ(vm.find("scenarioEffect").toString(), std::string("salary_unpaid"));
    CHECK_EQ(vm.find("scenarioEvent").toString(), std::string("rebellion_started"));
    CHECK_EQ(vm.find("scenarioTransaction").toString(), std::string("committed"));
    CHECK_EQ(vm.find("scenarioStateOwner").toString(), std::string("\"faction.frontier\""));
    CHECK_EQ(vm.find("scenarioRank").toString(), std::string("{\"authority\":[\"govern_base\",\"command_army\"],"
                                                             "\"commandCapacity\":8}"));
    CHECK(vm.find("scenarioAuthority").toBool());
    CHECK_EQ(vm.find("scenarioPolicy").toString(), std::string("governor_bonus"));
    CHECK_EQ(vm.find("scenarioProductionA").toString(), std::string("completed:build_unit"));
    CHECK_EQ(vm.find("scenarioProductionB").toString(), std::string("completed:issue_decree"));
    CHECK_EQ(vm.find("scenarioAiTarget").toString(), std::string("army.raiders"));
    CHECK_EQ(vm.find("scenarioAiState").toString(), std::string("engage"));
    CHECK_EQ(vm.find("scenarioAiAction").toString(), std::string("attack"));
    CHECK(vm.find("scenarioAiVelocity").toString().find("\"x\"") != std::string::npos);
    CHECK_EQ(vm.find("scenarioAiThreat").toFloat(), 0.8F);

    vm.run(vm.compileSource(R"(
        reset_demo();
        demoState.authority.revokeBySource("rank.general", "rank_removed");
        refresh_governor_bonus();
        deniedProduction <- demoState.baseStats.getFinal("production_speed", 0.0);
        deniedReason <- demoState.authority.explain(
            demoState.general, demoState.baseId, "govern_base").getReason();
    )"));
    CHECK_EQ(vm.find("deniedProduction").toFloat(), 1.0F);
    CHECK_EQ(vm.find("deniedReason").toString(), std::string("no_matching_rule"));

    vm.run(vm.compileSource(R"(
        reset_demo();
        apply_unpaid_salary();
        conflictResult <- demoState.authoritativeState.newBatch();
        local conflict = conflictResult.ok ? conflictResult.value : null;
        if (conflict != null) {
            conflict.setExpected(demoState.baseId, "owner", "\"faction.usurper\"", "\"faction.crown\"");
            demoState.authoritativeState.commit(conflict);
        }
        conflictResult <- evaluate_rebellion();
        conflictGeneralOwner <- demoState.authoritativeState.get(demoState.general, "owner");
        conflictSocialOwner <- demoSocial.ownerOf(demoState.general);
        conflictTransaction <- demoState.transactions.at(0).getState();
        conflictEvents <- demoState.events.size();
    )"));

    CHECK(!vm.find("conflictResult").toBool());
    CHECK_EQ(vm.find("conflictGeneralOwner").toString(), std::string("\"faction.crown\""));
    CHECK_EQ(vm.find("conflictSocialOwner").toString(), std::string("faction.crown"));
    CHECK_EQ(vm.find("conflictTransaction").toString(), std::string("failed"));
    CHECK_EQ(vm.find("conflictEvents").toInt(), int64_t{0});
}

TEST_CASE("composableGameplay.cardAndRtsAdaptersShareDefinitionsTerrainAndIdentity") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/composable-editor/gameplay_components.nut";
    std::ifstream     input(path);
    REQUIRE(input.good());
    std::ostringstream source;
    source << input.rdbuf();

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(source.str().c_str()));
    vm.run(vm.compileSource(R"(
        gameplay <- GameplayEditorComponents();
        heightmapResult <- eve.Procgen().newHeightmap(8, 8);
        heightmap <- heightmapResult.ok ? heightmapResult.value : null;
        for (local y = 0; y < 8; ++y)
            for (local x = 0; x < 8; ++x) heightmap.setHeight(x, y, (x + y).tofloat() / 16.0);
        gameplay.bindTerrain(heightmap, 1.0);
        authoredCard <- gameplay.createCardInstance();
        fakeEntity <- { position={ x=0.0, z=0.0 } };
        authoredUnit <- gameplay.createRtsUnit("unit.editor", fakeEntity, 0.5, 0.5);
        gameplay.update(0.1);
        authoredCardDefinition <- authoredCard.getDefinitionId();
        authoredCardCount <- gameplay.cards.getCardDefinitionCount();
        authoredUnitStable <- gameplay.crowdSim.getAgentStableId(
            gameplay.crowdSim.getNamedAgentIndex("unit.editor"));
        authoredDefinitionCount <- gameplay.definitions.size();
        authoredTerrainCost <- gameplay.crowdSim.getCellCost(7, 7);
    )"));

    CHECK(vm.find("authoredUnit").toBool());
    CHECK_EQ(vm.find("authoredCardDefinition").toString(), std::string("card.scout"));
    CHECK_EQ(vm.find("authoredCardCount").toInt(), 1);
    CHECK_EQ(vm.find("authoredUnitStable").toString(), std::string("unit.editor"));
    CHECK_EQ(vm.find("authoredDefinitionCount").toInt(), 2);
    CHECK(vm.find("authoredTerrainCost").toFloat() > 1.f);
}
}  // namespace test_composable_gameplay
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_console)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// console.cpp
namespace test_console {
using namespace eve::dev;


TEST_CASE("devtools.console.attachEvalPrintCapture") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    console.clear();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    {
        auto script = vm.compileSource("score <- 40;\nfunction double(x) { return x * 2; }\n");
        vm.run(script);
    }
    dt.attach(vm, false);
    CHECK(console.isAttached());

    // REPL: evaluate a root-table expression.
    const std::string result = console.eval("score + 2");
    CHECK(result == "42");
    CHECK(console.format(16).find("42") != std::string::npos);

    // REPL: call a user function.
    CHECK(console.eval("double(21)") == "42");

    // REPL: string literal echoes quoted.
    CHECK(console.eval("\"hi\"") == "\"hi\"");

    // REPL: compile error reported without crashing.
    CHECK(console.eval("1 +") == "error: compile failed");

    // print() from a script is captured into the log.
    {
        auto script = vm.compileSource("print(\"captured-line\");\n");
        vm.run(script);
    }
    CHECK(console.format(16).find("captured-line") != std::string::npos);

    dt.detach();
    CHECK(!console.isAttached());
    console.clear();
}

TEST_CASE("devtools.console.persistentAcrossVms") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    console.clear();

    // The ring buffer survives a detach/re-attach cycle (DevTools singleton).
    ssq::VM vm1(512, ssq::Libs::ALL);
    dt.attach(vm1, false);
    console.addInfo("sticky");
    dt.detach();

    ssq::VM vm2(512, ssq::Libs::ALL);
    dt.attach(vm2, false);
    CHECK(console.format(8).find("sticky") != std::string::npos);
    dt.detach();
    console.clear();
}
}  // namespace test_console
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_contacts)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
#include "zeroerr/unittest.h"
// crowd_contacts.cpp
namespace test_crowd_contacts {
using namespace eve::crowd;


TEST_CASE("crowd.contacts.scriptAdvanceUsesCanonicalResult") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.addAgent(3.0, 4.0, 0.0, 1.0);
        assert(crowd.configureAvoidance(true, 2.0, 0.1, 32).ok);
        assert(crowd.getAvoidanceSettings().enabled);
        assert(!crowd.configureAvoidance(true, -1.0, 0.1, 32).ok);
        assert(crowd.setAgentInteraction(0, 0.25, true, 1, 3).ok);
        local policy = crowd.getAgentInteraction(0);
        assert(policy.ok && policy.hasValue && policy.value.holdPosition);
        assert(policy.value.pushability == 0.25 && policy.value.mask == 3);
        assert(!crowd.setAgentInteraction(0, -1.0, false, 1, 3).ok);
        assert(!crowd.getAgentInteraction(123).ok);
        local rejected = crowd.advance(-1.0);
        assert(!rejected.ok && !rejected.hasValue);
        assert(rejected.diagnostics.len() > 0);
        local result = crowd.advance(0.016);
        assert(result.ok && result.hasValue);
        assert(result.value.substeps >= 1);
        assert(result.value.avoidanceChecks == 0);
        assert(result.value.avoidanceTruncations == 0);
        assert(result.value.unresolvedContacts == 0);
        assert(result.value.unresolvedWalls == 0);
    )"));
}
}  // namespace test_crowd_contacts
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_script)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* crowd_content;
// crowd_script.cpp
namespace test_crowd_script {
using namespace eve;


UnitSciptTest(CrowdScriptTest, crowd_content);

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.basic") { REQUIRE(callScript("basic")); }

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.invalidIds") { REQUIRE(callScript("invalidIds")); }
}  // namespace test_crowd_script
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_spawn)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
// crowd_spawn.cpp
namespace test_crowd_spawn {
using namespace eve::crowd;


TEST_CASE("crowd.spawn.scriptTransactionAndMalformedInput") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.addNamedAgent("old", 1.5, 0.0, 0.0, 1.0);
        local options = { policy = "pushNeighbors", maxDistance = 10.0,
                          searchSpacing = 1.0, maxPasses = 64, maxChecks = 10000 };
        local agents = [{ stableId = "new", x = 0.0, y = 0.0, heading = 0.0, radius = 1.0,
                          pushability = 1.0, holdPosition = false, layer = 1, mask = 1 }];
        local result = crowd.applySpawnBatch(agents, options);
        assert(result.ok && result.hasValue);
        assert(result.value.created[0].stableId == "new");
        assert(result.value.displacedAgents == 1);
        assert(crowd.getAgentCount() == 2);
        local duplicate = crowd.applySpawnBatch(agents, options);
        assert(!duplicate.ok && duplicate.diagnostics.len() > 0);
        local malformed = crowd.applySpawnBatch([{ stableId = "bad" }], options);
        assert(!malformed.ok && !malformed.hasValue);
        assert(crowd.getAgentCount() == 2 && !crowd.hasNamedAgent("bad"));
    )"));
}
}  // namespace test_crowd_spawn
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_state_script_provider)
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "common/StateAccess.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueState.h"
#include "dialogue/StateAccessScriptSeam.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_state_script_provider.cpp
namespace test_dialogue_state_script_provider {
// ---------------------------------------------------------------------------
// 阶段 5 的第五、六个插座：`IStateQuery` / `IStateMutation` 可以由脚本提供。
//
// 这两条的形状与前面几个的区别在于**注册面不同**：
//  - `IStateQuery` 的消费方（`DialogueState::queryProviders`）先问 `cap::query` 的单实现、再按优先级
//    问 listener，**第一个给出非空答案的赢** → 脚本走 listener 面，补在原生 provider 后面。
//  - `IStateMutation` 的消费方用 `cap::query` → 脚本进单实现槽位（顶掉时要看得见）。
//
// 还有一个特殊点：`IStateQuery` 的每个方法只回 `optional`，**没有失败通道**，而 `nullopt` 的语义是
// "不拥有这一问"。所以脚本抛错只能按"不拥有"返回（否则一个 provider 的错误会毁掉整条条件链），
// 代价是必须**计数 + 上报**，让失败看得见。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;

const char* kScriptStateAccess = R"SQ(
seenSubject <- ""
seenKey <- ""
lastMutationCount <- -1
lastMutationKind <- ""
lastMutationPersistent <- false
lastTransactionId <- ""
appliedGold <- -1

class WorldState {
    function state(subject, key) {
        seenSubject = subject
        seenKey = key
        if (key == "gold") return 42
        return null                      // 不拥有这一问
    }
    function hasTag(subject, tag) {
        if (tag == "wanted") return true
        return null
    }
}

// 只实现一部分方法：没实现的那几类问题应当被当作"不拥有"，而不是失败。
class PartialState {
    function value(subject, key) {
        if (key == "mood") return "calm"
        return null
    }
}

class BrokenState {
    function state(subject, key) { throw "state provider is broken" }
    function hasTag(subject, tag) { return 7 }        // 形状不对：布尔位置给了整数
}

class WorldMutations {
    function applyStateMutations(payload) {
        lastTransactionId = payload.transactionId
        lastMutationCount = payload.mutations.len()
        if (lastMutationCount > 0) {
            lastMutationKind = payload.mutations[0].kind
            lastMutationPersistent = payload.mutations[0].persistent
            if (payload.mutations[0].key == "gold") appliedGold = payload.mutations[0].value
        }
        return payload.mutations.len()
    }
}

class NoopMutations {
    function applyStateMutations(payload) { return 0 }
}

class BadMutations {
    function applyStateMutations(payload) { return "not a count" }
}

class BrokenMutations {
    function applyStateMutations(payload) { throw "mutation provider is broken" }
}

stateProvider <- WorldState()
partialProvider <- PartialState()
brokenProvider <- BrokenState()
mutationProvider <- WorldMutations()
noopProvider <- NoopMutations()
badProvider <- BadMutations()
brokenMutationProvider <- BrokenMutations()
)SQ";

class ScriptStateAccessTest : public ScriptTest {
public:
    ScriptStateAccessTest() : ScriptTest(kScriptStateAccess) {
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        cap::detail::clearAllRaw();
        eve::dialogue::resetScriptStateQueryFailures();
    }

    ~ScriptStateAccessTest() {
        eve::dialogue::withdrawScriptStateQuerySeam();
        eve::dialogue::withdrawScriptStateMutationSeam();
        cap::detail::clearAllRaw();
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    ssq::Object publish(const char* capability, const char* name, const char* instance) {
        const std::string source = std::string("published <- eve.provider.publishFor(\"") + capability + "\", \"" +
                                   name + "\", " + instance + ")";
        vm.run(vm.compileSource(source.c_str()));
        return vm.find("published");
    }

    void unload(const char* name) {
        auto& registry = ScriptProviderRegistry::instance();
        REQUIRE(registry.unload(ScriptProviderHandle{name, registry.generationOf(name)}).ok());
    }

private:
    std::vector<std::string> declaredBefore_;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptStateAccessTest, "dialogue.state.scriptQueryAnswersConditionsThroughTheListenerFace") {
    eve::dialogue::declareScriptStateQuerySeam();
    ssq::Object published = publish(eve::IStateQuery::capabilityName, "test.state.world", "stateProvider");
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(cap::listenerCount<eve::IStateQuery>(), std::size_t{1});

    // 消费方就是对话的条件求值上下文：它按"单实现 → listener"的顺序问，第一个非空答案赢。
    eve::dialogue::StateEvaluationContext context("npc:blacksmith");
    const auto                            gold = context.state("gold");
    REQUIRE(gold.has_value());
    REQUIRE(gold->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*gold->getIf<std::int64_t>(), 42);
    // 脚本看到的是对话给的主体与键（编组：两个位置文本 + 任意值返回）。
    REQUIRE_EQ(vm.find("seenSubject").toString(), std::string("npc:blacksmith"));
    REQUIRE_EQ(vm.find("seenKey").toString(), std::string("gold"));

    const auto wanted = context.hasTag("wanted");
    REQUIRE(wanted.has_value());
    REQUIRE(*wanted);

    // `null` = 不拥有这一问：已知的 false 与"不拥有"必须分得开。
    const auto unknown = context.state("unknown-key");
    REQUIRE(!unknown.has_value());
    const auto notTag = context.hasTag("not-my-tag");
    REQUIRE(!notTag.has_value());

    unload("test.state.world");
    REQUIRE_EQ(cap::listenerCount<eve::IStateQuery>(), std::size_t{0});
    REQUIRE(!context.state("gold").has_value());
}

TEST_CASE_FIXTURE(ScriptStateAccessTest, "dialogue.state.scriptQueryDoesNotDisplaceANativeProvider") {
    eve::dialogue::declareScriptStateQuerySeam();

    // 原生单实现槽位：脚本 provider 走 listener 面，所以单实现先赢——两者可以同时在位。
    class NativeQuery final : public eve::IStateQuery {
    public:
        [[nodiscard]] std::optional<eve::Value> value(std::string_view, std::string_view) const override {
            return std::nullopt;
        }
        [[nodiscard]] std::optional<bool> hasTag(std::string_view, std::string_view) const override {
            return std::nullopt;
        }
        [[nodiscard]] std::optional<eve::Value> attribute(std::string_view, std::string_view) const override {
            return std::nullopt;
        }
        [[nodiscard]] std::optional<eve::Value> resource(std::string_view, std::string_view) const override {
            return std::nullopt;
        }
        [[nodiscard]] std::optional<eve::Value> state(std::string_view, std::string_view key) const override {
            if (key != "gold") return std::nullopt;
            return eve::Value(std::string("native"));
        }
        [[nodiscard]] std::optional<bool> authority(std::string_view, std::string_view) const override {
            return std::nullopt;
        }
    };

    NativeQuery native;
    cap::provide<eve::IStateQuery>(&native);
    REQUIRE(publish(eve::IStateQuery::capabilityName, "test.state.script", "stateProvider").find("ok").toBool());

    eve::dialogue::StateEvaluationContext context("npc:blacksmith");
    const auto                            gold = context.state("gold");
    REQUIRE(gold.has_value());
    REQUIRE(gold->getIf<std::string>() != nullptr);
    REQUIRE_EQ(*gold->getIf<std::string>(), std::string("native"));  // 单实现优先于 listener

    // 原生退场后，同一个键就轮到脚本回答——两者确实在同一条链上，不是互相顶掉。
    cap::revoke<eve::IStateQuery>(&native);
    const auto scriptAnswer = context.state("gold");
    REQUIRE(scriptAnswer.has_value());
    REQUIRE(scriptAnswer->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*scriptAnswer->getIf<std::int64_t>(), 42);
}

TEST_CASE_FIXTURE(ScriptStateAccessTest,
                  "dialogue.state.partialAndFailingScriptProvidersDeclineWithoutBreakingTheChain") {
    eve::dialogue::declareScriptStateQuerySeam();
    REQUIRE(publish(eve::IStateQuery::capabilityName, "test.state.partial", "partialProvider").find("ok").toBool());
    REQUIRE(publish(eve::IStateQuery::capabilityName, "test.state.broken", "brokenProvider").find("ok").toBool());

    eve::dialogue::StateEvaluationContext context("npc:blacksmith");

    // 只实现了 `value`：没实现的方法算"不拥有"，不产生失败计数。
    const auto mood = context.value("mood");
    REQUIRE(mood.has_value());
    REQUIRE(mood->getIf<std::string>() != nullptr);
    REQUIRE_EQ(*mood->getIf<std::string>(), std::string("calm"));
    REQUIRE_EQ(eve::dialogue::scriptStateQueryFailures(), std::uint64_t{0});

    // 抛错 / 答案形状不对：按"不拥有"返回（接口没有失败通道），但**可见**——计数 + 上报。
    REQUIRE(!context.state("gold").has_value());
    REQUIRE(eve::dialogue::scriptStateQueryFailures() >= 1);
    const std::uint64_t afterThrow = eve::dialogue::scriptStateQueryFailures();
    REQUIRE(!context.hasTag("wanted").has_value());
    REQUIRE(eve::dialogue::scriptStateQueryFailures() > afterThrow);
}

TEST_CASE_FIXTURE(ScriptStateAccessTest, "dialogue.state.scriptMutationAppliesTheWholeBatchAndReportsAReceipt") {
    eve::dialogue::declareScriptStateMutationSeam();
    ssq::Object published = publish(eve::IStateMutation::capabilityName, "test.state.mutations", "mutationProvider");
    REQUIRE(published.find("ok").toBool());
    REQUIRE(cap::query<eve::IStateMutation>() != nullptr);

    eve::dialogue::DialogueStateContext   context("npc:blacksmith");
    const std::vector<eve::StateMutation> mutations = {
        eve::StateMutation{"npc:blacksmith", "gold", eve::Value(std::int64_t(25)), eve::MutationKind::AddNumber, false},
        eve::StateMutation{"npc:blacksmith", "met", eve::Value(true), eve::MutationKind::AddTag, false},
    };
    const eve::MutationContext mutationContext{"tx-1", "corr-1", "cause-1"};
    auto                       receipt = context.apply(mutations, mutationContext);
    REQUIRE(receipt.ok());
    REQUIRE_EQ(receipt.value().changedCount, std::size_t{2});
    REQUIRE_EQ(receipt.value().transactionId, std::string("tx-1"));

    // 编组：整批一次进脚本，字段名与枚举拼写都对得上。
    REQUIRE_EQ(vm.find("lastMutationCount").toInt(), 2);
    REQUIRE_EQ(vm.find("lastMutationKind").toString(), std::string("addNumber"));
    REQUIRE_EQ(vm.find("lastMutationPersistent").toBool(), false);
    REQUIRE_EQ(vm.find("lastTransactionId").toString(), std::string("tx-1"));
    REQUIRE_EQ(vm.find("appliedGold").toInt(), 25);

    // 0 条变更 = NoOp（与原生适配器同一个收据语义），不是失败。
    unload("test.state.mutations");
    REQUIRE(publish(eve::IStateMutation::capabilityName, "test.state.noop", "noopProvider").find("ok").toBool());
    auto noop = context.apply(mutations, mutationContext);
    REQUIRE(noop.ok());
    REQUIRE_EQ(noop.code(), eve::StatusCode::NoOp);
    REQUIRE_EQ(noop.value().changedCount, std::size_t{0});
}

TEST_CASE_FIXTURE(ScriptStateAccessTest, "dialogue.state.scriptMutationFailuresAreStructuredAndVisible") {
    eve::dialogue::DialogueStateContext   context("npc:blacksmith");
    const std::vector<eve::StateMutation> mutations = {
        eve::StateMutation{"npc:blacksmith", "gold", eve::Value(std::int64_t(1)), eve::MutationKind::Set, false}};
    const eve::MutationContext mutationContext{"tx-1", {}, {}};

    // ① 接缝还没声明：发布被明确拒绝（不是"绑了不响"），消费方也报"没有 provider"。
    REQUIRE(!ScriptProviderRegistry::instance().hasCapabilitySeam(eve::IStateMutation::capabilityName));
    REQUIRE(!publish(eve::IStateMutation::capabilityName, "test.state.early", "mutationProvider").find("ok").toBool());
    const auto unavailable = context.apply(mutations, mutationContext);
    REQUIRE(!unavailable.ok());

    eve::dialogue::declareScriptStateMutationSeam();

    // ② 返回值不是"改了几条"：结构化 `InvalidArgument`，不是静默成功。
    REQUIRE(publish(eve::IStateMutation::capabilityName, "test.state.bad", "badProvider").find("ok").toBool());
    auto bad = context.apply(mutations, mutationContext);
    REQUIRE(!bad.ok());
    REQUIRE_EQ(static_cast<int>(bad.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
    unload("test.state.bad");

    // ③ 脚本抛错：失败原样传出（消费方靠返回值判断，吞掉会让上层以为写成功了）。
    REQUIRE(publish(eve::IStateMutation::capabilityName, "test.state.broken", "brokenMutationProvider")
                .find("ok")
                .toBool());
    auto broken = context.apply(mutations, mutationContext);
    REQUIRE(!broken.ok());
    REQUIRE_EQ(static_cast<int>(broken.error()->code()), static_cast<int>(eve::DiagnosticCode::Failed));
    unload("test.state.broken");

    // ④ 生产接线：`Dialogue` 模块自己构造时两条接缝都在位。
    eve::dialogue::Dialogue module;
    REQUIRE(ScriptProviderRegistry::instance().hasCapabilitySeam(eve::IStateQuery::capabilityName));
    REQUIRE(ScriptProviderRegistry::instance().hasCapabilitySeam(eve::IStateMutation::capabilityName));
}
}  // namespace test_dialogue_state_script_provider
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_provider)
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Diagnostic.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"
// script_provider.cpp
namespace test_script_provider {
using namespace eve;
namespace {

const char* kScriptProviderContent = R"SQ(
class Counter {
    total = 0.0
    function onTick(dt) { total += dt; return total }
    function ping() { total += 1.0 }
    function label(dt) { return "not a number" }
    function boom(dt) { throw "deliberate provider failure" }
    function scaleDamage(vehicleId, defId, amount, zone, sourceId) { return amount * 0.5 }

    // 决定③（对象/表参数 + 结构化返回）：插座可以收一个表、也可以答一个表。
    function describe(request) { return request.kind + ":" + request.count }
    function summarize(request) {
        return { kind = request.kind, count = request.count * 2.0, tags = ["near", "armed"], ok = true }
    }
    function returnsClosure() { return function() { return 1 } }
}
provider <- Counter()
)SQ";

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

/** Capability the seam tests declare; the tests below own its whole lifetime. */
constexpr const char* kSeamCapability = "ScriptProviderSeamTest";

}  // namespace

/**
 * The registry roots a live VM object, so it must be empty before this fixture's VM
 * is destroyed. The destructor body runs before the base class members (including
 * `vm`) are torn down, which is exactly the ordering the registry requires.
 *
 * Declared seams are not published state, so `clearAll` keeps them; this fixture
 * withdraws whatever a case declared, which keeps the cases order-independent when
 * several of them share one process.
 */
class ScriptProviderTest : public ScriptTest {
public:
    ScriptProviderTest() : ScriptTest(kScriptProviderContent) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
    }

    ~ScriptProviderTest() {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            auto removed = registry.removeCapability(capability);
            removed.ignore("fixture teardown withdraws only the seams this case declared");
        }
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.publishAndCallNumber") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE(published.value().name == std::string("test.counter"));
    REQUIRE(published.value().generation != 0);
    REQUIRE_EQ(registry.size(), std::size_t{1});
    REQUIRE(registry.contains("test.counter"));

    auto first = registry.callNumber(published.value(), "onTick", 0.5);
    REQUIRE(first.ok());
    REQUIRE(std::abs(first.value() - 0.5) < 1e-6);

    // The call runs on the live instance, so state persists across invocations.
    auto second = registry.callNumber(published.value(), "onTick", 0.25);
    REQUIRE(second.ok());
    REQUIRE(std::abs(second.value() - 0.75) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.voidCallAndMethodFailures") {
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    REQUIRE(registry.call(published.value(), "ping").ok());
    auto ticks = registry.callNumber(published.value(), "onTick", 0.0);
    REQUIRE(ticks.ok());
    REQUIRE(std::abs(ticks.value() - 1.0) < 1e-6);

    // A method the provider does not implement is Unsupported, never a silent no-op
    // and never a crash.
    auto missing = registry.call(published.value(), "notAMethod");
    REQUIRE(!missing.ok());
    REQUIRE_EQ(firstDiagnosticCode(missing.status()), eve::DiagnosticCode::Unsupported);

    auto nonNumeric = registry.callNumber(published.value(), "label", 0.0);
    REQUIRE(!nonNumeric.ok());
    REQUIRE_EQ(firstDiagnosticCode(nonNumeric.status()), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.scriptErrorsAreReportedNotRaised") {
    // Provider methods are user script. A failing one must come back as a structured
    // `Failed` result carrying the script error, never as a raise into the native
    // frame that called it.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto raised = registry.call(published.value(), "boom", 0.0);
    REQUIRE(!raised.ok());
    REQUIRE_EQ(firstDiagnosticCode(raised.status()), eve::DiagnosticCode::Failed);
    REQUIRE(raised.status().describe().find("deliberate provider failure") != std::string::npos);

    // The provider is still usable afterwards: the failed call restored the stack.
    auto recovered = registry.callNumber(published.value(), "onTick", 2.0);
    REQUIRE(recovered.ok());
    REQUIRE(std::abs(recovered.value() - 2.0) < 1e-6);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.retirementIsNeverAliased") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto first = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(first.ok());
    auto second = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(second.ok());
    REQUIRE(second.value().generation > first.value().generation);
    REQUIRE_EQ(registry.generationOf("test.counter"), second.value().generation);

    // Republishing retires the older generation rather than aliasing it.
    auto superseded = registry.call(first.value(), "ping");
    REQUIRE(!superseded.ok());
    REQUIRE_EQ(firstDiagnosticCode(superseded.status()), eve::DiagnosticCode::StaleHandle);

    REQUIRE(registry.unload(second.value()).ok());
    REQUIRE(!registry.contains("test.counter"));
    REQUIRE_EQ(registry.generationOf("test.counter"), std::uint64_t{0});
    REQUIRE_EQ(registry.size(), std::size_t{0});

    auto retired = registry.call(second.value(), "ping");
    REQUIRE(!retired.ok());
    REQUIRE_EQ(firstDiagnosticCode(retired.status()), eve::DiagnosticCode::StaleHandle);

    // Retiring twice is stale, not a silent success.
    auto again = registry.unload(second.value());
    REQUIRE(!again.ok());
    REQUIRE_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::StaleHandle);

    // A later publish must not let either old handle resolve against it.
    auto third = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(third.ok());
    for (const auto& stale : {first.value(), second.value()}) {
        auto refused = registry.call(stale, "ping");
        REQUIRE(!refused.ok());
        REQUIRE_EQ(firstDiagnosticCode(refused.status()), eve::DiagnosticCode::StaleHandle);
    }
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.rejectsBadPublishArguments") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    auto emptyName = registry.publish("", vm.find("provider"));
    REQUIRE(!emptyName.ok());
    REQUIRE_EQ(firstDiagnosticCode(emptyName.status()), eve::DiagnosticCode::InvalidArgument);

    ssq::Object unbound;
    auto        notAnInstance = registry.publish("test.none", unbound);
    REQUIRE(!notAnInstance.ok());
    REQUIRE_EQ(firstDiagnosticCode(notAnInstance.status()), eve::DiagnosticCode::InvalidArgument);
    REQUIRE_EQ(registry.size(), std::size_t{0});

    // A handle for a name that was never published is stale, not a lookup failure.
    auto unknown = registry.call(eve::ScriptProviderHandle{"test.absent", 7}, "ping");
    REQUIRE(!unknown.ok());
    REQUIRE_EQ(firstDiagnosticCode(unknown.status()), eve::DiagnosticCode::StaleHandle);
}

namespace {

/** Records what the registry bound, so the seam tests can prove the callbacks ran. */
struct SeamRecorder {
    std::vector<std::string> bound;
    std::vector<std::string> unbound;

    eve::Result<void> onBind(const eve::ScriptProviderHandle& handle) {
        bound.push_back(handle.name);
        return eve::Result<void>::success();
    }

    eve::Result<void> onUnbind(const eve::ScriptProviderHandle& handle) {
        unbound.push_back(handle.name);
        return eve::Result<void>::success();
    }
};

}  // namespace

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.capabilitySeamRequiresDeclaration") {
    auto& registry = eve::ScriptProviderRegistry::instance();

    // Without a declared seam the capability is not answerable, and the refusal is
    // visible rather than a published provider that serves nothing.
    REQUIRE(!registry.hasCapabilitySeam(kSeamCapability));
    auto refused = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(!refused.ok());
    REQUIRE_EQ(firstDiagnosticCode(refused.status()), eve::DiagnosticCode::NotFound);
    REQUIRE_EQ(registry.size(), std::size_t{0});

    // The declaration is what makes it answerable, and it is discoverable.
    registry.declareCapability(
        kSeamCapability, [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); },
        [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); });
    REQUIRE(registry.hasCapabilitySeam(kSeamCapability));
    const auto available = registry.capabilities();
    REQUIRE(std::find(available.begin(), available.end(), std::string(kSeamCapability)) != available.end());

    auto published = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE_EQ(published.value().name, std::string("test.seam"));

    const auto listed = registry.published();
    REQUIRE_EQ(listed.size(), std::size_t{1});
    REQUIRE_EQ(listed.front().capability, std::string(kSeamCapability));

    // Withdrawing the seam stops further binding but leaves the live generation
    // usable, so a provider can still be retired through the handle it was given.
    REQUIRE(registry.removeCapability(kSeamCapability).ok());
    REQUIRE(!registry.hasCapabilitySeam(kSeamCapability));
    auto afterWithdrawal = registry.publishForCapability(kSeamCapability, "test.seam2", vm.find("provider"));
    REQUIRE(!afterWithdrawal.ok());
    REQUIRE_EQ(firstDiagnosticCode(afterWithdrawal.status()), eve::DiagnosticCode::NotFound);
    REQUIRE(registry.call(published.value(), "ping").ok());
    REQUIRE(registry.unload(published.value()).ok());

    auto again = registry.removeCapability(kSeamCapability);
    REQUIRE(!again.ok());
    REQUIRE_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::NotFound);
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.capabilitySeamBindsAndUnbinds") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    auto  recorder = std::make_shared<SeamRecorder>();

    registry.declareCapability(
        kSeamCapability, [recorder](const eve::ScriptProviderHandle& handle) { return recorder->onBind(handle); },
        [recorder](const eve::ScriptProviderHandle& handle) { return recorder->onUnbind(handle); });

    auto published = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(published.ok());
    REQUIRE_EQ(recorder->bound.size(), std::size_t{1});
    REQUIRE_EQ(recorder->bound.front(), std::string("test.seam"));
    REQUIRE(recorder->unbound.empty());

    // Superseding runs the unbinder for the retired generation, then binds the new one.
    auto superseded = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(superseded.ok());
    REQUIRE_EQ(recorder->unbound.size(), std::size_t{1});
    REQUIRE_EQ(recorder->unbound.front(), std::string("test.seam"));
    REQUIRE_EQ(recorder->bound.size(), std::size_t{2});

    REQUIRE(registry.unload(superseded.value()).ok());
    REQUIRE_EQ(recorder->unbound.size(), std::size_t{2});
    REQUIRE_EQ(registry.size(), std::size_t{0});
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.failedBindIsRolledBack") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    int   unbinds  = 0;

    registry.declareCapability(
        kSeamCapability,
        [](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "the consumer was already claimed", handle.name));
        },
        [&unbinds](const eve::ScriptProviderHandle&) -> eve::Result<void> {
            ++unbinds;
            return eve::Result<void>::success();
        });

    auto failed = registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider"));
    REQUIRE(!failed.ok());
    REQUIRE_EQ(firstDiagnosticCode(failed.status()), eve::DiagnosticCode::Conflict);

    // Atomic: no half-bound provider stays observable, and the rollback ran the
    // unbinder so a partially installed adapter cannot survive.
    REQUIRE_EQ(unbinds, 1);
    REQUIRE_EQ(registry.size(), std::size_t{0});
    REQUIRE(!registry.contains("test.seam"));
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.clearAllRunsUnbinders") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    int   unbinds  = 0;

    registry.declareCapability(
        kSeamCapability, [](const eve::ScriptProviderHandle&) { return eve::Result<void>::success(); },
        [&unbinds](const eve::ScriptProviderHandle&) -> eve::Result<void> {
            ++unbinds;
            return eve::Result<void>::success();
        });

    REQUIRE(registry.publishForCapability(kSeamCapability, "test.seam", vm.find("provider")).ok());
    registry.clearAll();
    REQUIRE_EQ(unbinds, 1);
    REQUIRE_EQ(registry.size(), std::size_t{0});
    // Declared seams are composition-time declarations, not published state.
    REQUIRE(registry.hasCapabilitySeam(kSeamCapability));
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.callsCarryScalarArguments") {
    // A capability seam needs more than one number: `IVehicleDamageModifier` takes a
    // stable id, a template id, an amount, a zone and a source id.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    const eve::ScriptArg args[] = {
        eve::ScriptArg::fromText("tank#1"), eve::ScriptArg::fromText("tank"), eve::ScriptArg::fromNumber(12.5),
        eve::ScriptArg::fromText("front"),  eve::ScriptArg::fromInteger(7),
    };
    auto scaled = registry.callNumber(published.value(), "scaleDamage", args);
    REQUIRE(scaled.ok());
    REQUIRE(std::abs(scaled.value() - 6.25) < 1e-6);

    // Wrong arity is a reported failure, and it leaves the provider usable.
    const eve::ScriptArg tooFew[] = {eve::ScriptArg::fromText("tank#1")};
    auto                 arity    = registry.callNumber(published.value(), "scaleDamage", tooFew);
    REQUIRE(!arity.ok());
    REQUIRE_EQ(firstDiagnosticCode(arity.status()), eve::DiagnosticCode::Failed);
    REQUIRE(registry.call(published.value(), "ping").ok());
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.callSetupErrorsStayReported") {
    // A parameter-count mistake is raised by `SQVM::StartCall`, and `SQVM::Execute`
    // sends that straight to the VM's error handler *regardless* of `sq_call`'s
    // `raiseerror`. This case is the regression guard for that path: with the engine
    // hook installed (see ScriptTest) it is a reported failure, not a raise into the
    // native frame and not a crash inside simplesquirrel's default handler.
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto tooMany = registry.call(published.value(), "ping", 1.0);
    REQUIRE(!tooMany.ok());
    REQUIRE_EQ(firstDiagnosticCode(tooMany.status()), eve::DiagnosticCode::Failed);
    REQUIRE(tooMany.status().describe().find("parameters") != std::string::npos);

    auto tooFew = registry.call(published.value(), "onTick");
    REQUIRE(!tooFew.ok());
    REQUIRE_EQ(firstDiagnosticCode(tooFew.status()), eve::DiagnosticCode::Failed);

    // Both failures left the VM stack intact: the provider still answers.
    auto recovered = registry.callNumber(published.value(), "onTick", 3.0);
    REQUIRE(recovered.ok());
    REQUIRE(std::abs(recovered.value() - 3.0) < 1e-6);
}

// ---------------------------------------------------------------------------
// 决定③（2026-09-20）：桥支持"对象/表参数 + 结构化返回"。
//
// 原来的契约写的是"carries values, not native structs / 不是通用反射桥"。这条决定把它放宽到
// "标量 + 一个对象，返回也可以是一个对象/数组"，代价是编组语义（所以 ScriptArg 的 Table 与
// callValue 的 @cost 都按"构造一个表/投影整棵树"来读）。这一片只做桥本身；把被卡的插座
// （ISensingCandidateProvider 等）逐个接上属于后续工作。
// ---------------------------------------------------------------------------

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.callsCarryTableArguments") {
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto request =
        eve::ScriptArg::fromTable({{"kind", eve::Value::string("sensor")}, {"count", eve::Value::number(3.0)}});
    const eve::ScriptArg args[] = {request};

    // 脚本侧真的收到了一个表（字段名与值都对），而不是被打平成一串标量。
    auto described = registry.callValue(published.value(), "describe", args);
    REQUIRE(described.ok());
    REQUIRE(described.value().isString());
    REQUIRE_EQ(described.value().asString(), std::string("sensor:3"));
}

TEST_CASE_FIXTURE(ScriptProviderTest, "ScriptProvider.structuredReturnsComeBackAsValues") {
    auto& registry  = eve::ScriptProviderRegistry::instance();
    auto  published = registry.publish("test.counter", vm.find("provider"));
    REQUIRE(published.ok());

    auto request =
        eve::ScriptArg::fromTable({{"kind", eve::Value::string("sensor")}, {"count", eve::Value::number(2.5)}});
    const eve::ScriptArg args[]     = {request};
    auto                 summarised = registry.callValue(published.value(), "summarize", args);
    REQUIRE(summarised.ok());

    const eve::Value& payload = summarised.value();
    REQUIRE(payload.isObject());
    REQUIRE_EQ(payload.find("kind")->asString(), std::string("sensor"));
    REQUIRE_EQ(payload.find("count")->asDouble(), 5.0);
    REQUIRE(payload.find("ok")->asBool());
    const eve::Value* tags = payload.find("tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->isArray());
    REQUIRE_EQ(tags->arraySize(), std::size_t{2});
    REQUIRE_EQ(tags->at(0).asString(), std::string("near"));

    // 无参数重载与有参数重载走同一条路：`onTick` 返回一个数，`callValue` 照样给一棵 Value 树。
    const eve::ScriptArg dt[]  = {eve::ScriptArg::fromNumber(4.0)};
    auto                 total = registry.callValue(published.value(), "onTick", dt);
    REQUIRE(total.ok());
    REQUIRE(total.value().isDouble());
    REQUIRE(std::abs(total.value().asDouble() - 4.0) < 1e-6);

    // 转换不了的结果是**上报失败**，不是硬塞一个值：闭包不是 Value 能表达的东西。
    auto unconvertible = registry.callValue(published.value(), "returnsClosure");
    REQUIRE(!unconvertible.ok());
    REQUIRE_EQ(firstDiagnosticCode(unconvertible.status()), eve::DiagnosticCode::InvalidArgument);

    // 失败之后 provider 仍然可用（调用纪律没被破坏）。
    REQUIRE(registry.call(published.value(), "ping").ok());
}
}  // namespace test_script_provider
#endif
