#include "ScriptTest.h"

#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "common/StateAccess.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueState.h"
#include "dialogue/StateAccessScriptSeam.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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
