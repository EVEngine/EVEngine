#include "map/TileCollisionScriptSink.h"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/ScriptProvider.h"
#include "common/Value.h"
#include "map/TileCollision.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::map {
namespace {

/**
 * @brief 一次发布的载荷：`{layer = {id, generation}, rects = [{x, y, width, height}, …]}`。
 *
 * 层身份与几何一起进同一张表：脚本要能回答"这批矩形属于哪一层"，否则一个进程里两层同时发布
 * 时它认不出来。字段名与接口参数一一对应，没有额外语义。
 *
 * @cost 一次与矩形数成正比的表构造（大层 = 一次大分配）。这是脚本化这条通路的固有成本：
 *       原生 sink 收的是借用指针，零编组。
 */
Value::Object collisionPayload(TileLayerRef layer, const TileCollisionRect* rects, size_t count) {
    Value::Array encoded;
    encoded.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const TileCollisionRect& rect = rects[index];
        encoded.push_back(Value::object({{"x", Value::number(double(rect.x))},
                                         {"y", Value::number(double(rect.y))},
                                         {"width", Value::number(double(rect.width))},
                                         {"height", Value::number(double(rect.height))}}));
    }
    return {{"layer", Value::object({{"id", Value::integer(static_cast<std::int64_t>(layer.slot))},
                                     {"generation", Value::integer(static_cast<std::int64_t>(layer.generation))}})},
            {"rects", Value::array(std::move(encoded))}};
}

/** @brief 脚本 sink 失败计数（`scriptTileCollisionSinkFailures()` 的实现）。 */
std::uint64_t& scriptTileCollisionSinkFailureCount() {
    static std::uint64_t count = 0;
    return count;
}

/** @brief 脚本提供方实现的 `ITileCollisionSink`。 */
class ScriptTileCollisionSink final : public ITileCollisionSink {
public:
    explicit ScriptTileCollisionSink(ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const ScriptProviderHandle& handle() const noexcept { return handle_; }

    void replaceTileCollision(TileLayerRef layer, const TileCollisionRect* rects, size_t count) override {
        const ScriptArg args[]    = {ScriptArg::fromTable(collisionPayload(layer, rects, count))};
        const auto      published = ScriptProviderRegistry::instance().call(handle_, "replaceTileCollision", args);
        if (published.ok()) return;

        // 接口是 `void`：失败没有返回值可放，所以**计数 + 上报**（每一代只写一行日志，避免
        // 地图重建循环里把日志刷满；计数是每一次都记，测试与诊断要看得见真实次数）。
        ++scriptTileCollisionSinkFailureCount();
        if (reportedGeneration_ == handle_.generation) return;
        reportedGeneration_ = handle_.generation;
        recordLogEvent("warn", "script tile collision sink '" + handle_.name +
                                   "' did not accept the geometry for layer " + std::to_string(layer.slot) + ": " +
                                   published.status().describe());
    }

private:
    /** 本适配器所服务的那一代脚本提供方。 */
    ScriptProviderHandle handle_;
    /** 已经报过失败的那一代（0 = 还没报过）；计数在别处，这里只管"日志只写一次"。 */
    std::uint64_t reportedGeneration_ = 0;
};

/** @brief 进程级持有脚本适配器（理由同伤害规则接缝：能力表是进程级的）。 */
struct ScriptTileCollisionSinks {
    std::vector<std::unique_ptr<ScriptTileCollisionSink>> live;
    std::vector<std::unique_ptr<ScriptTileCollisionSink>> retired;
};

ScriptTileCollisionSinks& scriptTileCollisionSinks() {
    static ScriptTileCollisionSinks state;
    return state;
}

}  // namespace

std::uint64_t scriptTileCollisionSinkFailures() { return scriptTileCollisionSinkFailureCount(); }

void resetScriptTileCollisionSinkFailures() { scriptTileCollisionSinkFailureCount() = 0; }

void declareScriptTileCollisionSinkSeam() {
    ScriptTileCollisionSinks& owned = scriptTileCollisionSinks();
    ScriptProviderRegistry::instance().declareCapability(
        ITileCollisionSink::capabilityName,
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            auto adapter = std::make_unique<ScriptTileCollisionSink>(handle);
            // 先注册再交所有权：从这一刻起 listener 表持有裸指针，`owned.live` 是让它有效的唯一原因。
            cap::addListener<ITileCollisionSink>(adapter.get());
            owned.live.push_back(std::move(adapter));
            return Result<void>::success();
        },
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            const auto match = std::find_if(owned.live.begin(), owned.live.end(),
                                            [&handle](const std::unique_ptr<ScriptTileCollisionSink>& adapter) {
                                                return adapter->handle() == handle;
                                            });
            if (match == owned.live.end()) {
                // 幂等：后置条件是"这一代没有适配器"，模块退出时可能已经撤过了。
                return Result<void>::success();
            }
            cap::removeListener<ITileCollisionSink>(match->get());
            owned.retired.push_back(std::move(*match));
            owned.live.erase(match);
            return Result<void>::success();
        });
}

void withdrawScriptTileCollisionSinkSeam() {
    ScriptTileCollisionSinks& owned = scriptTileCollisionSinks();
    for (auto& adapter : owned.live) {
        cap::removeListener<ITileCollisionSink>(adapter.get());
        owned.retired.push_back(std::move(adapter));
    }
    owned.live.clear();

    // 连声明一起撤：模块不在了还留着接缝，只会让脚本发布出一个永远不会被派发的适配器。
    ScriptProviderRegistry::instance()
        .removeCapability(ITileCollisionSink::capabilityName)
        .ignore("a module may be destroyed without ever having declared the seam");
}

}  // namespace eve::map
