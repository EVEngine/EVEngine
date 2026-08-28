#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"
#include "i18n/I18n.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace eve::dialogue;
using eve::avatar::Avatar;
using eve::avatar::AvatarInstance;
using eve::i18n::I18n;

namespace {

using Vars = std::unordered_map<std::string, Dialogue::VarValue>;

DataValue O(DataValue::Object v) {
    return DataValue::object(std::move(v));
}

DataValue A(std::vector<DataValue> v) { return DataValue::array(std::move(v)); }

DataValue S(const std::string &v) { return DataValue::string(v); }

DataValue I(long long v) { return DataValue::integer(v); }

DataValue F(double v) { return DataValue::number(v); }

DataValue B(bool v) { return DataValue::boolean(v); }

DataValue poolsRoot(DataValue pools) {
    return O({{"pools", std::move(pools)}});
}

void resetDialogue(Dialogue *dlg) {
    dlg->reset();
    dlg->clearPools();
    dlg->clearVars("all");
}

}  // namespace

TEST_CASE("dialogueProc.varsScopes") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    CHECK(dlg->setVarValue("hp", Dialogue::VarValue::integer(100), "global"));
    CHECK(dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "scene"));
    CHECK(dlg->setVarValue("ratio", Dialogue::VarValue::number(1.5), "global"));
    CHECK(dlg->setVarValue("flag", Dialogue::VarValue::boolean(true), "scene"));

    CHECK(dlg->hasVar("hp", "global"));
    CHECK(!dlg->hasVar("hp", "scene"));
    CHECK_EQ(dlg->getVarType("mood", "scene"), std::string("string"));
    CHECK_EQ(dlg->getVarType("hp", "global"), std::string("int"));
    CHECK_EQ(dlg->getVarType("ratio", "global"), std::string("float"));
    CHECK_EQ(dlg->getVarType("flag", "scene"), std::string("bool"));
    CHECK_EQ(dlg->getVarType("missing", "global"), std::string(""));

    CHECK_EQ(dlg->getVarInt("hp", -1, "global"), 100);
    CHECK_EQ(dlg->getVarInt("hp", -1, "scene"), -1);
    CHECK_EQ(dlg->getVarFloat("ratio", 0.f, "global"), 1.5f);
    CHECK(dlg->getVarBool("flag", false, "scene"));
    CHECK_EQ(dlg->getVarString("mood", "", "scene"), std::string("happy"));
    CHECK_EQ(dlg->getVarString("mood", "", "global"), std::string(""));

    dlg->clearVars("scene");
    CHECK(!dlg->hasVar("mood", "scene"));
    CHECK(dlg->hasVar("hp", "global"));
    dlg->clearVars("global");
    CHECK(!dlg->hasVar("hp", "global"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.conditions") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");
    dlg->setVarValue("flag", Dialogue::VarValue::boolean(true), "global");

    CHECK(dlg->evalConditionData(O({
        {"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")},
    })));
    CHECK(!dlg->evalConditionData(O({
        {"var", S("mood")}, {"op", S("eq")}, {"value", S("sad")},
    })));
    CHECK(dlg->evalConditionData(O({
        {"var", S("hour")}, {"op", S("ge")}, {"value", I(18)},
    })));
    CHECK(!dlg->evalConditionData(O({
        {"var", S("hour")}, {"op", S("lt")}, {"value", I(18)},
    })));
    CHECK(dlg->evalConditionData(O({{"var", S("flag")}, {"op", S("eq")}, {"value", B(true)}})));
    CHECK(dlg->evalConditionData(O({{"var", S("flag")}, {"op", S("has")}})));
    CHECK(dlg->evalConditionData(O({{"var", S("nope")}, {"op", S("missing")}})));
    CHECK(!dlg->evalConditionData(O({{"var", S("nope")}, {"op", S("eq")}, {"value", S("x")}})));

    CHECK(dlg->evalConditionData(O({
        {"all", A({
            O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}}),
            O({{"var", S("hour")}, {"op", S("ge")}, {"value", I(18)}}),
        })},
    })));
    CHECK(dlg->evalConditionData(O({
        {"any", A({
            O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("angry")}}),
            O({{"var", S("hour")}, {"op", S("ge")}, {"value", I(18)}}),
        })},
    })));
    CHECK(dlg->evalConditionData(O({
        {"not", O({{"var", S("flag")}, {"op", S("eq")}, {"value", B(false)}})},
    })));

    // Script predicates need a VM; without one they evaluate false.
    CHECK(!dlg->evalConditionData(O({{"script", S("missingPred")}})));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.poolLoading") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"alice.greet", O({
            {"noRepeat", I(4)},
            {"lines", A({
                O({{"id", S("a1")}, {"speaker", S("alice")}, {"text", S("你好")}}),
                O({{"speaker", S("alice")}, {"i18n", S("line.hi")}, {"weight", I(2)},
                   {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}})},
                   {"meta", O({{"expression", S("happy")}, {"voice", S("vo/a1.ogg")}})},
                   {"tags", A({S("greet"), S("evening")})}}),
            })},
        })},
    }));

    CHECK_EQ(dlg->loadPoolsFromData(root), 1);
    CHECK(dlg->hasPool("alice.greet"));
    CHECK_EQ(dlg->getPoolCount(), 1);
    CHECK_EQ(dlg->getPoolId(0), std::string("alice.greet"));
    // Missing id -> auto-generated "poolId.序号".
    CHECK(dlg->playLineWithParams("alice.greet.2", {}));
    CHECK(dlg->getLastPoolsError().empty());

    // Same pool id replaces the previous definition (no duplication).
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);
    CHECK_EQ(dlg->getPoolCount(), 1);

    // Invalid root / missing pools object.
    CHECK_EQ(dlg->loadPoolsFromData(S("oops")), 0);
    CHECK(!dlg->getLastPoolsError().empty());
    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.pickLine") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"pool.abc", O({
            {"lines", A({
                O({{"id", S("p1")}, {"speaker", S("a")}, {"text", S("one")}, {"weight", I(1)}}),
                O({{"id", S("p2")}, {"speaker", S("a")}, {"text", S("two")}, {"weight", I(1)}}),
            })},
        })},
        {"pool.filt", O({
            {"lines", A({
                O({{"id", S("f1")}, {"speaker", S("a")}, {"text", S("happy line")},
                   {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}})}}),
                O({{"id", S("f2")}, {"speaker", S("a")}, {"text", S("sad line")},
                   {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("sad")}})}}),
            })},
        })},
        {"pool.zero", O({
            {"lines", A({
                O({{"id", S("z0")}, {"speaker", S("a")}, {"text", S("zero")}, {"weight", I(0)}}),
                O({{"id", S("z1")}, {"speaker", S("a")}, {"text", S("one")}, {"weight", I(1)}}),
            })},
        })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 3);

    // Determinism: same seed reproduces the same pick sequence.
    dlg->setRandomSeed(42);
    const std::string first = dlg->pickLineWithParams("pool.abc", {});
    CHECK(!first.empty());
    dlg->setRandomSeed(42);
    const std::string second = dlg->pickLineWithParams("pool.abc", {});
    CHECK_EQ(second, first);

    // Both lines appear across many picks (50/50 weights).
    bool sawP1 = false, sawP2 = false;
    dlg->setRandomSeed(7);
    for (int i = 0; i < 200; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.abc", {});
        if (id == "p1") sawP1 = true;
        if (id == "p2") sawP2 = true;
    }
    CHECK(sawP1);
    CHECK(sawP2);

    // Unknown pool -> "".
    CHECK_EQ(dlg->pickLineWithParams("nope", {}), std::string(""));

    // when filtering by variables.
    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    for (int i = 0; i < 10; ++i) CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string("f1"));
    dlg->setVarValue("mood", Dialogue::VarValue::string("sad"), "global");
    for (int i = 0; i < 10; ++i) CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string("f2"));
    dlg->clearVars("global");
    CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string(""));

    // weight <= 0 lines never win.
    dlg->setRandomSeed(3);
    for (int i = 0; i < 50; ++i)
        CHECK_EQ(dlg->pickLineWithParams("pool.zero", {}), std::string("z1"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.noRepeat") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    // noRepeat=1 with two lines: consecutive picks must differ.
    const DataValue root = poolsRoot(O({
        {"pool.alt", O({
            {"noRepeat", I(1)},
            {"lines", A({
                O({{"id", S("n1")}, {"speaker", S("a")}, {"text", S("one")}}),
                O({{"id", S("n2")}, {"speaker", S("a")}, {"text", S("two")}}),
            })},
        })},
        // noRepeat=2 with two lines: third pick falls back to the full pool.
        {"pool.fb", O({
            {"noRepeat", I(2)},
            {"lines", A({
                O({{"id", S("m1")}, {"speaker", S("a")}, {"text", S("one")}}),
                O({{"id", S("m2")}, {"speaker", S("a")}, {"text", S("two")}}),
            })},
        })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 2);

    dlg->setRandomSeed(11);
    std::string prev = dlg->pickLineWithParams("pool.alt", {});
    for (int i = 1; i < 40; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.alt", {});
        CHECK(id != prev);
        CHECK((id == "n1" || id == "n2"));
        prev = id;
    }

    dlg->setRandomSeed(22);
    for (int i = 0; i < 5; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.fb", {});
        CHECK((id == "m1" || id == "m2"));
    }

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.playLineAndMeta") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);
    Avatar *avmod = Avatar::create();
    CHECK(dlg->registerCharacter("alice", "Alice"));
    AvatarInstance *alice = avmod->newImageAvatar();
    alice->addLayer("face", nullptr, 0);
    alice->defineExpression("happy", "face=1");
    CHECK(dlg->bindAvatar("alice", alice));

    const DataValue root = poolsRoot(O({
        {"pool.play", O({
            {"lines", A({
                O({{"id", S("pl1")}, {"speaker", S("alice")}, {"text", S("你好，{name}！")},
                   {"meta", O({{"expression", S("happy")}, {"motion", S("wave")},
                               {"voice", S("vo/alice.ogg")}})},
                   {"tags", A({S("greet"), S("night")})}}),
                O({{"id", S("pl2")}, {"speaker", S("")}, {"text", S("旁白")}}),
                O({{"id", S("pl3")}, {"speaker", S("alice")}, {"i18n", S("greet.key")}}),
            })},
        })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);

    // Literal text: params override global vars in {var} substitution.
    dlg->setVarValue("name", Dialogue::VarValue::string("Alice"), "global");
    Vars params;
    params["name"] = Dialogue::VarValue::string("Bob");
    CHECK(dlg->playLineWithParams("pl1", params));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("pl1"));
    CHECK_EQ(dlg->getFullText(), std::string("你好，Bob！"));
    CHECK_EQ(dlg->getCurrentLineMeta("voice"), std::string("vo/alice.ogg"));
    CHECK_EQ(dlg->getCurrentLineMeta("expression"), std::string("happy"));
    CHECK_EQ(dlg->getCurrentLineMeta("missing"), std::string(""));
    CHECK_EQ(dlg->getCurrentLineTags().size(), size_t(2));
    CHECK_EQ(dlg->getCurrentLineTags()[0], std::string("greet"));
    // Expression was auto-applied to the bound avatar.
    CHECK_EQ(alice->getExpression(), std::string("happy"));
    CHECK_EQ(alice->getMotion(), std::string("wave"));

    // Narration line (empty speaker).
    CHECK(dlg->playLineWithParams("pl2", {}));
    CHECK_EQ(dlg->getSpeakerId(), std::string(""));
    CHECK_EQ(dlg->getFullText(), std::string("旁白"));

    // i18n key resolution through a real I18n instance.
    I18n *i18n = I18n::create();
    CHECK(i18n->loadFromJson("en", R"({"greet": { "key": "Hi, {name}!" } })"));
    CHECK(i18n->setLanguage("en"));
    dlg->setVarValue("name", Dialogue::VarValue::string("Carol"), "global");
    CHECK(dlg->playLineWithParams("pl3", {}));
    CHECK_EQ(dlg->getFullText(), std::string("Hi, Carol!"));

    resetDialogue(dlg);
    dlg->bindAvatar("alice", nullptr);  // 解除绑定，避免后续 reset 触碰已释放的 avatar
    alice->release();
    delete alice;
}

TEST_CASE("dialogueProc.playPool") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"pool.greet", O({
            {"lines", A({
                O({{"id", S("g1")}, {"speaker", S("npc")}, {"text", S("Hi {name}")}}),
            })},
        })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);

    Vars params;
    params["name"] = Dialogue::VarValue::string("Alex");
    CHECK(dlg->playPoolWithParams("pool.greet", params));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("g1"));
    CHECK_EQ(dlg->getFullText(), std::string("Hi Alex"));
    CHECK(!dlg->playPoolWithParams("pool.gone", params));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.dnutParser") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    const std::string src =
        "pool alice.greet noRepeat=4 {\n"
        "    when mood == \"happy\" && hour >= 18 {\n"
        "        alice: \"晚上好，{name}！\" weight=2 meta(expression=\"happy\", motion=\"wave\") tags=[\"greet\",\"evening\"]\n"
        "        alice: \"你来啦。\" weight=1\n"
        "    }\n"
        "    when mood == \"shy\" {\n"
        "        alice: \"那个……你好。\" meta(expression=\"shy\")\n"
        "    }\n"
        "    - \"远处传来钟声。\"\n"
        "}\n";

    CHECK_EQ(dlg->loadPoolsFromDnut(src, "pools.dnut"), 1);
    CHECK(dlg->hasPool("alice.greet"));
    CHECK(dlg->getLastPoolsError().empty());

    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");

    // Line 1: compiled when = all(mood eq happy, hour ge 18); meta + tags.
    CHECK(dlg->playLineWithParams("alice.greet.1", {}));
    CHECK_EQ(dlg->getCurrentLineMeta("expression"), std::string("happy"));
    CHECK_EQ(dlg->getCurrentLineMeta("motion"), std::string("wave"));
    CHECK_EQ(dlg->getCurrentLineTags().size(), size_t(2));
    CHECK_EQ(dlg->getCurrentLineTags()[0], std::string("greet"));

    // When filtering: hour < 18 -> only the narration line matches.
    dlg->setVarValue("hour", Dialogue::VarValue::integer(8), "global");
    for (int i = 0; i < 10; ++i) {
        const std::string id = dlg->pickLineWithParams("alice.greet", {});
        CHECK_EQ(id, std::string("alice.greet.4"));
    }

    // Narration line: empty speaker.
    CHECK(dlg->playLineWithParams("alice.greet.4", {}));
    CHECK_EQ(dlg->getSpeakerId(), std::string(""));
    CHECK_EQ(dlg->getFullText(), std::string("远处传来钟声。"));

    // shy branch: line 3 命中；旁白行 4 无条件也合格，但 noRepeat 历史会先排除它。
    dlg->setVarValue("mood", Dialogue::VarValue::string("shy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");
    const std::string firstShy = dlg->pickLineWithParams("alice.greet", {});
    CHECK_EQ(firstShy, std::string("alice.greet.3"));
    for (int i = 0; i < 9; ++i) {
        const std::string id = dlg->pickLineWithParams("alice.greet", {});
        CHECK((id == "alice.greet.3" || id == "alice.greet.4"));
    }

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.dnutAttrsAndErrors") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);

    const std::string src =
        "pool x {\n"
        "    alice: \"hi\" weight=3 i18n=\"line.hi\" id=\"custom\"\n"
        "}\n";
    CHECK_EQ(dlg->loadPoolsFromDnut(src, "a.dnut"), 1);
    CHECK(dlg->playLineWithParams("custom", {}));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("custom"));
    // weight=3 参与随机（相对池内其它行权重更高）；这里单行池直接命中。
    CHECK_EQ(dlg->pickLineWithParams("x", {}), std::string("custom"));

    // 语法错误：行号与路径出现在 getLastPoolsError。
    CHECK_EQ(dlg->loadPoolsFromDnut("pool x {\n  alice: hi\n}\n", "bad.dnut"), 0);
    CHECK(dlg->getLastPoolsError().find("bad.dnut:2") != std::string::npos);

    // 未知属性。
    CHECK_EQ(dlg->loadPoolsFromDnut("pool x { alice: \"hi\" bogus=1 }\n", "bad2.dnut"), 0);
    CHECK(!dlg->getLastPoolsError().empty());

    // 未闭合的 pool。
    CHECK_EQ(dlg->loadPoolsFromDnut("pool x {\n  alice: \"hi\"\n", "bad3.dnut"), 0);
    CHECK(dlg->getLastPoolsError().find("bad3.dnut") != std::string::npos);

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.sceneVarsAutoClear") {
    Dialogue *dlg = Dialogue::create();
    resetDialogue(dlg);
    dlg->setVarValue("keep", Dialogue::VarValue::integer(1), "global");

    eve::scene::Scene *scn = eve::scene::Scene::create();
    scn->mountAs("dialogueSceneA", eve::scene::node("root")).ignore("test setup");
    scn->mountAs("dialogueSceneB", eve::scene::node("root")).ignore("test setup");
    scn->select("dialogueSceneA");

    dlg->update(0.016f);  // record current scene name
    dlg->setVarValue("tmp", Dialogue::VarValue::integer(2), "scene");
    CHECK(dlg->hasVar("tmp", "scene"));

    scn->select("dialogueSceneB");
    dlg->update(0.016f);  // scene changed -> scene vars cleared
    CHECK(!dlg->hasVar("tmp", "scene"));
    CHECK(dlg->hasVar("keep", "global"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.stateCaptureRestoreRoundtrip") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);
    dlg->setVarValue("hp", Dialogue::VarValue::integer(100), "global");
    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "scene");
    dlg->setRandomSeed(12345);
    dlg->say("hero", "Hello world");
    dlg->skipTyping();
    CHECK(dlg->isWaitingAdvance());

    eve::StateValue captured;
    REQUIRE(dlg->captureState(captured));

    // Mutate, then restore.
    dlg->reset();
    dlg->setVarValue("hp", Dialogue::VarValue::integer(1), "global");

    std::string err;
    CHECK(dlg->restoreState(captured, &err));
    CHECK(err.empty());
    CHECK_EQ(dlg->getVarInt("hp", 0, "global"), 100);
    CHECK_EQ(dlg->getVarString("mood", "", "scene"), std::string("happy"));
    CHECK_EQ(dlg->getRandomSeed(), 12345);
    CHECK(dlg->isWaitingAdvance());
    CHECK_EQ(dlg->getFullText(), std::string("Hello world"));
    CHECK_EQ(dlg->getSpeakerId(), std::string("hero"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.stateRestoreRejectsMalformed") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    eve::StateValue bad = eve::StateValue::object();
    bad.set("phase", eve::StateValue::string("no_such_phase"));
    std::string err;
    CHECK(!dlg->restoreState(bad, &err));
    CHECK(!err.empty());

    eve::StateValue missing = eve::StateValue::object();
    CHECK(!dlg->restoreState(missing, &err));
    CHECK(!err.empty());

    resetDialogue(dlg);
}
