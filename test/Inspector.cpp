#include "common/Runtime.h"
#include "ui/Inspector.h"
#include "ui/UIHost.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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
    auto hp = std::find_if(cls->members.begin(), cls->members.end(),
                           [](const ReflectedMember& m) { return m.name == "hp"; });
    REQUIRE(hp != cls->members.end());
    CHECK_EQ(hp->attrFloat("min"), 0.f);
    CHECK_EQ(hp->attrFloat("max"), 100.f);
    auto job = std::find_if(cls->members.begin(), cls->members.end(),
                            [](const ReflectedMember& m) { return m.name == "job"; });
    REQUIRE(job != cls->members.end());
    CHECK(job->attrOptions("options") ==
          std::vector<std::string>({"warrior", "mage", "rogue"}));

    // Instance creation runs the default constructor and roots the object.
    ssq::Object hero = runtime.createInstance("InspectorHero");
    CHECK(static_cast<int>(hero.getType()) == static_cast<int>(ssq::Type::INSTANCE));
    CHECK_EQ(runtime.classNameOf(hero), std::string("InspectorHero"));
    CHECK_EQ(runtime.readProperty(hero, "name").asString(), std::string("Hero"));

    // reflectInstance merges own + inherited members with live values.
    const std::vector<ReflectedMember> members = runtime.reflectInstance(hero);
    auto byName = [&](const std::string& name) -> const ReflectedMember* {
        auto it = std::find_if(members.begin(), members.end(),
                               [&](const ReflectedMember& m) { return m.name == name; });
        return it == members.end() ? nullptr : &*it;
    };
    const ReflectedMember* baseLevel = byName("baseLevel");
    REQUIRE(baseLevel != nullptr);
    CHECK(static_cast<int>(baseLevel->value.kind) ==
          static_cast<int>(ReflectedValueKind::Integer));
    CHECK_EQ(baseLevel->value.integer, int64_t(1));
    const ReflectedMember* update = byName("update");
    REQUIRE(update != nullptr);
    CHECK(update->method);

    // Typed writes preserve the script slot's type.
    ReflectedValue damage;
    damage.kind = ReflectedValueKind::Float;
    damage.floating = 4.5;
    CHECK(runtime.writeProperty(hero, "hp", damage));
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 4.5);

    ReflectedValue level;
    level.kind = ReflectedValueKind::Float;  // float in, integer slot stays integer
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
    value.kind = ReflectedValueKind::Integer;
    value.integer = 1;
    CHECK(!runtime.writeProperty(hero, "update", value));
    CHECK(!runtime.writeProperty(hero, "missing", value));

    // Constructor side effects run on createInstance.
    runtime.runSource("class InspectorCtor { x = 0\n constructor() { x = 42 } }",
                      "ctor.nut");
    ssq::Object ctorInstance = runtime.createInstance("InspectorCtor");
    CHECK_EQ(runtime.readProperty(ctorInstance, "x").asInt(), int64_t(42));

    // Classes defined outside the Runtime API (dofile/compilestring path) are
    // picked up by scanClasses(); redefining a name refreshes the reflection.
    const auto runRaw = [&](const char* source) {
        auto scope = runtime.enter();
        auto stack = runtime.guard();
        HSQUIRRELVM vm = runtime.handle();
        const SQInteger top = sq_gettop(vm);
        REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(vm, source,
                                              static_cast<SQInteger>(std::strlen(source)),
                                              "external.nut", SQTrue)));
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
    REQUIRE(static_cast<int>(selected.getType()) ==
            static_cast<int>(ssq::Type::INSTANCE));
    auto tree = host->tree();
    REQUIRE_GE(hp->handlerValue, 1u);
    tree->valueHandlers[size_t(hp->handlerValue - 1)](42.f);
    CHECK_EQ(runtime.readProperty(selected, "hp").asFloat(), 42.0);

    // Model → view: external change is pulled into the tree by sync().
    ReflectedValue value;
    value.kind = ReflectedValueKind::Float;
    value.floating = 55.0;
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
    CHECK_EQ(runtime.tableKeys(holder, "buffs"),
             std::vector<std::string>({"haste"}));
    CHECK_EQ(runtime.tableGet(holder, "buffs", "haste").asFloat(), 1.0);
    ReflectedValue speed;
    speed.kind = ReflectedValueKind::Float;
    speed.floating = 2.0;
    CHECK(runtime.tableSet(holder, "buffs", "speed", speed));
    CHECK_EQ(runtime.tableGet(holder, "buffs", "speed").asFloat(), 2.0);
    CHECK(runtime.tableRemove(holder, "buffs", "haste"));
    CHECK_EQ(runtime.tableKeys(holder, "buffs"),
             std::vector<std::string>({"speed"}));

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

    UINode* skill0 = nodeById(host, "arr_NestedHolder_skills_0");
    REQUIRE(skill0 != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(skill0->handlerText, 1u);
    tree->textHandlers[size_t(skill0->handlerText - 1)]("spin");
    const ssq::Object selected = inspector.selectedInstance();
    CHECK_EQ(runtime.arrayGet(selected, "skills", 0).asString(), std::string("spin"));

    UINode* buffHaste = nodeById(host, "tbl_NestedHolder_buffs_haste");
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
    CHECK_EQ(runtime.readProperty(
                 inspector.selectedInstance(), "name").asString(),
             std::string("cleave"));

    UINode* back = nodeById(host, "inspector_back");
    REQUIRE(back != nullptr);
    REQUIRE_GE(back->handlerClick, 1u);
    tree2->clickHandlers[size_t(back->handlerClick - 1)]();
    REQUIRE(nodeById(host, "arr_NestedHolder_skills_0") != nullptr);
    CHECK_EQ(runtime.classNameOf(inspector.selectedInstance()),
             std::string("NestedHolder"));
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
    UIHost *host = resolveHost(inspector.host());
    REQUIRE(host != nullptr);

    UINode *pick = nodeById(host, "inspector_pick");
    REQUIRE(pick != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(pick->handlerClick, 1u);
    tree->clickHandlers[size_t(pick->handlerClick - 1)]();

    CHECK_EQ(inspector.selectedClass(), std::string("PickedHero"));
    CHECK_EQ(inspector.instanceCount(), 1);
    CHECK_EQ(runtime.readProperty(inspector.selectedInstance(), "name").asString(),
             std::string("picked"));
}
