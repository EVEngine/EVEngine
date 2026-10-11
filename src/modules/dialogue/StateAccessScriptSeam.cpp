#include "dialogue/StateAccessScriptSeam.h"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/ScriptProvider.h"
#include "common/StateAccess.h"
#include "common/Value.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

/** @brief 脚本词表的一条：枚举值 ↔ 脚本看到的字符串。 */
template <class T>
struct NameEntry {
    T                value;
    std::string_view name;
};

constexpr std::array<NameEntry<eve::MutationKind>, 5> kMutationKinds = {{
    {eve::MutationKind::Set, "set"},
    {eve::MutationKind::Remove, "remove"},
    {eve::MutationKind::AddTag, "addTag"},
    {eve::MutationKind::RemoveTag, "removeTag"},
    {eve::MutationKind::AddNumber, "addNumber"},
}};
static_assert(kMutationKinds.size() == 5, "变更种类表与枚举不同步（加枚举值要同时改表）");

std::string_view mutationKindName(eve::MutationKind kind) noexcept {
    for (const NameEntry<eve::MutationKind>& entry : kMutationKinds) {
        if (entry.value == kind) return entry.name;
    }
    return {};
}

std::uint64_t& scriptStateQueryFailureCount() {
    static std::uint64_t count = 0;
    return count;
}

// ---------------------------------------------------------------------------
// IStateQuery 的脚本适配器
// ---------------------------------------------------------------------------

/**
 * @brief 脚本提供方实现的 `IStateQuery`。
 *
 * 六个方法名与接口一一对应，参数是 `(subject, key/tag/scope)` 两个文本，返回值经权威转换回
 * `eve::Value`：`null` = 不拥有这一问（`nullopt`）；缺方法同理（合法的"不负责这一类问题"）；
 * 抛错或形状不对 = 真失败，但接口没有失败通道，所以按"不拥有"返回并**计数 + 上报**。
 */
class ScriptStateQuery final : public eve::IStateQuery {
public:
    explicit ScriptStateQuery(eve::ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const eve::ScriptProviderHandle& handle() const noexcept { return handle_; }

    [[nodiscard]] std::optional<eve::Value> value(std::string_view subject, std::string_view key) const override {
        return askValue("value", subject, key);
    }
    [[nodiscard]] std::optional<bool> hasTag(std::string_view subject, std::string_view tag) const override {
        return askBool("hasTag", subject, tag);
    }
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view subject, std::string_view key) const override {
        return askValue("attribute", subject, key);
    }
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view subject, std::string_view key) const override {
        return askValue("resource", subject, key);
    }
    [[nodiscard]] std::optional<eve::Value> state(std::string_view subject, std::string_view key) const override {
        return askValue("state", subject, key);
    }
    [[nodiscard]] std::optional<bool> authority(std::string_view subject, std::string_view scope) const override {
        return askBool("authority", subject, scope);
    }

private:
    std::optional<eve::Value> askValue(const char* method, std::string_view subject, std::string_view key) const {
        auto answer = ask(method, subject, key);
        if (!answer.has_value() || answer->isNull()) return std::nullopt;
        return answer;
    }

    std::optional<bool> askBool(const char* method, std::string_view subject, std::string_view key) const {
        auto answer = ask(method, subject, key);
        if (!answer.has_value() || answer->isNull()) return std::nullopt;
        if (const auto* boolean = answer->getIf<bool>()) return *boolean;
        reportFailure(std::string(method) + " must return a boolean (or null for 'not mine')");
        return std::nullopt;
    }

    /** 一次问询；"不拥有"与"失败"都返回空，区别只在于失败会被计数与上报。 */
    std::optional<eve::Value> ask(const char* method, std::string_view subject, std::string_view key) const {
        const eve::ScriptArg args[] = {eve::ScriptArg::fromText(std::string(subject)),
                                       eve::ScriptArg::fromText(std::string(key))};
        auto                 answer = eve::ScriptProviderRegistry::instance().callValue(handle_, method, args);
        if (answer.ok()) return std::move(answer).takeValue();
        // 缺方法 = 这一类问题不归我（接口语义允许），不算失败。
        if (answer.code() != eve::StatusCode::Unsupported)
            reportFailure(std::string(method) + " failed: " + answer.status().describe());
        return std::nullopt;
    }

    void reportFailure(std::string message) const {
        ++scriptStateQueryFailureCount();
        if (reportedGeneration_ == handle_.generation) return;
        reportedGeneration_ = handle_.generation;
        recordLogEvent("warn",
                       "script state query provider '" + handle_.name + "' declined a query: " + std::move(message));
    }

    eve::ScriptProviderHandle handle_;
    /** 已经报过失败的那一代（0 = 还没报过）；计数在别处，这里只管"日志只写一次"。 */
    mutable std::uint64_t reportedGeneration_ = 0;
};

// ---------------------------------------------------------------------------
// IStateMutation 的脚本适配器
// ---------------------------------------------------------------------------

/** @brief 脚本提供方实现的 `IStateMutation`。 */
class ScriptStateMutation final : public eve::IStateMutation {
public:
    explicit ScriptStateMutation(eve::ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const eve::ScriptProviderHandle& handle() const noexcept { return handle_; }

    [[nodiscard]] eve::Result<eve::MutationReceipt> apply(std::span<const eve::StateMutation> mutations,
                                                          const eve::MutationContext&         context) override {
        eve::Value::Array encoded;
        encoded.reserve(mutations.size());
        for (const eve::StateMutation& mutation : mutations) {
            encoded.push_back(
                eve::Value::object({{"subject", eve::Value::string(mutation.subject)},
                                    {"key", eve::Value::string(mutation.key)},
                                    {"kind", eve::Value::string(std::string(mutationKindName(mutation.kind)))},
                                    {"persistent", eve::Value::boolean(mutation.persistent)},
                                    {"value", mutation.value}}));
        }
        const eve::ScriptArg args[] = {
            eve::ScriptArg::fromTable({{"transactionId", eve::Value::string(context.transactionId)},
                                       {"correlationId", eve::Value::string(context.correlationId)},
                                       {"causationId", eve::Value::string(context.causationId)},
                                       {"mutations", eve::Value::array(std::move(encoded))}})};
        auto answer = eve::ScriptProviderRegistry::instance().callValue(handle_, "applyStateMutations", args);
        if (!answer.ok()) return eve::Result<eve::MutationReceipt>::failure(answer.status());

        const auto* changed = answer.value().getIf<std::int64_t>();
        if (changed == nullptr || *changed < 0)
            return eve::Result<eve::MutationReceipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "applyStateMutations must return the number of changed entries as a non-negative integer",
                "applyStateMutations", {}, "dialogue.state-script"));
        const std::size_t changedCount = static_cast<std::size_t>(*changed);
        return eve::Result<eve::MutationReceipt>::success(
            eve::MutationReceipt{context.transactionId, changedCount},
            eve::Status::success(changedCount == 0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
    }

private:
    eve::ScriptProviderHandle handle_;
};

// ---------------------------------------------------------------------------
// 归属
// ---------------------------------------------------------------------------

/** @brief 进程级持有脚本适配器（理由同其它接缝：能力表是进程级的）。 */
struct ScriptStateAccessProviders {
    std::vector<std::unique_ptr<ScriptStateQuery>>    liveQueries;
    std::vector<std::unique_ptr<ScriptStateQuery>>    retiredQueries;
    std::vector<std::unique_ptr<ScriptStateMutation>> liveMutations;
    std::vector<std::unique_ptr<ScriptStateMutation>> retiredMutations;
};

ScriptStateAccessProviders& scriptStateAccessProviders() {
    static ScriptStateAccessProviders state;
    return state;
}

}  // namespace

std::uint64_t scriptStateQueryFailures() { return scriptStateQueryFailureCount(); }

void resetScriptStateQueryFailures() { scriptStateQueryFailureCount() = 0; }

void declareScriptStateQuerySeam() {
    ScriptStateAccessProviders& owned = scriptStateAccessProviders();
    eve::ScriptProviderRegistry::instance().declareCapability(
        eve::IStateQuery::capabilityName,
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            auto adapter = std::make_unique<ScriptStateQuery>(handle);
            // listener 面：脚本 provider 补在原生 provider 后面（先问单实现、再按优先级问 listener），
            // 谁也不顶掉谁。先注册再交所有权：能力表持有裸指针，`live` 是让它有效的唯一原因。
            eve::cap::addListener<eve::IStateQuery>(adapter.get());
            owned.liveQueries.push_back(std::move(adapter));
            return eve::Result<void>::success();
        },
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            const auto match = std::find_if(
                owned.liveQueries.begin(), owned.liveQueries.end(),
                [&handle](const std::unique_ptr<ScriptStateQuery>& adapter) { return adapter->handle() == handle; });
            if (match == owned.liveQueries.end()) {
                // 幂等：后置条件是"这一代没有适配器"，模块退出时可能已经撤过了。
                return eve::Result<void>::success();
            }
            eve::cap::removeListener<eve::IStateQuery>(match->get());
            owned.retiredQueries.push_back(std::move(*match));
            owned.liveQueries.erase(match);
            return eve::Result<void>::success();
        });
}

void withdrawScriptStateQuerySeam() {
    ScriptStateAccessProviders& owned = scriptStateAccessProviders();
    for (auto& adapter : owned.liveQueries) {
        eve::cap::removeListener<eve::IStateQuery>(adapter.get());
        owned.retiredQueries.push_back(std::move(adapter));
    }
    owned.liveQueries.clear();
    eve::ScriptProviderRegistry::instance()
        .removeCapability(eve::IStateQuery::capabilityName)
        .ignore("a module may be destroyed without ever having declared the seam");
}

void declareScriptStateMutationSeam() {
    ScriptStateAccessProviders& owned = scriptStateAccessProviders();
    eve::ScriptProviderRegistry::instance().declareCapability(
        eve::IStateMutation::capabilityName,
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            // 单实现槽位（消费方用 `cap::query`）：顶掉当前实现必须可见，否则原生 provider 会
            // "悄悄失效"，症状只是世界状态不再被写入。
            if (eve::cap::query<eve::IStateMutation>() != nullptr) {
                recordLogEvent("warn", "script state mutation provider '" + handle.name +
                                           "' replaced the provider that owned IStateMutation");
            }
            auto adapter = std::make_unique<ScriptStateMutation>(handle);
            eve::cap::provide<eve::IStateMutation>(adapter.get());
            owned.liveMutations.push_back(std::move(adapter));
            return eve::Result<void>::success();
        },
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            const auto match = std::find_if(
                owned.liveMutations.begin(), owned.liveMutations.end(),
                [&handle](const std::unique_ptr<ScriptStateMutation>& adapter) { return adapter->handle() == handle; });
            if (match == owned.liveMutations.end()) {
                return eve::Result<void>::success();
            }
            eve::cap::revoke<eve::IStateMutation>(match->get());
            owned.retiredMutations.push_back(std::move(*match));
            owned.liveMutations.erase(match);
            return eve::Result<void>::success();
        });
}

void withdrawScriptStateMutationSeam() {
    ScriptStateAccessProviders& owned = scriptStateAccessProviders();
    for (auto& adapter : owned.liveMutations) {
        eve::cap::revoke<eve::IStateMutation>(adapter.get());
        owned.retiredMutations.push_back(std::move(adapter));
    }
    owned.liveMutations.clear();
    eve::ScriptProviderRegistry::instance()
        .removeCapability(eve::IStateMutation::capabilityName)
        .ignore("a module may be destroyed without ever having declared the seam");
}

}  // namespace eve::dialogue
