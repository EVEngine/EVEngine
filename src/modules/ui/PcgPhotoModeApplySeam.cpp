#include "ui/PcgPhotoModeApplySeam.h"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/PcgPhotoModeApply.h"
#include "common/ScriptProvider.h"
#include "common/Value.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::ui {
namespace {

/**
 * `PhotoModeDomain` ↔ 脚本侧名字。
 *
 * 脚本写 `domain = "camera"` 而不是一个枚举数字：这是决定③之后脚本能读到的那张表的形状，
 * 数字对脚本作者不可读、也没有任何一处能检查它对不对。名字表是**唯一**的一处映射，
 * `static_assert` 钉住它的长度（枚举目前是 0..11），并且 `test/pcg_photo_mode_apply.cpp`
 * 逐个取值双向验证——加一个枚举值而忘了这里会立刻红。
 */
struct DomainName {
    PhotoModeDomain  domain;
    std::string_view name;
};

constexpr std::array<DomainName, 12> kDomainNames = {{
    {PhotoModeDomain::Photo, "photo"},
    {PhotoModeDomain::FramePacing, "framepacing"},
    {PhotoModeDomain::Graphics, "graphics"},
    {PhotoModeDomain::Camera, "camera"},
    {PhotoModeDomain::Streaming, "streaming"},
    {PhotoModeDomain::Weather, "weather"},
    {PhotoModeDomain::Lighting, "lighting"},
    {PhotoModeDomain::Water, "water"},
    {PhotoModeDomain::PostFx, "postfx"},
    {PhotoModeDomain::Terrain, "terrain"},
    {PhotoModeDomain::Grass, "grass"},
    {PhotoModeDomain::Audio, "audio"},
}};

/** @brief 脚本侧名字；未知枚举值返回空串（调用方据此上报，而不是编一个名字）。 */
std::string_view domainName(PhotoModeDomain domain) {
    for (const DomainName& entry : kDomainNames) {
        if (entry.domain == domain) return entry.name;
    }
    return {};
}

/** @brief 解析脚本给的名字；大小写敏感，与 `domainName` 严格互逆。 */
bool parseDomain(std::string_view text, PhotoModeDomain* out) {
    for (const DomainName& entry : kDomainNames) {
        if (entry.name == text) {
            if (out != nullptr) *out = entry.domain;
            return true;
        }
    }
    return false;
}

/** @brief `PhotoModeValue` → 脚本表里的 `value` 字段：标量直给，颜色给一张 `{r,g,b,a}`。 */
Value photoModeValueToScript(const PhotoModeValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) return Value::boolean(*boolean);
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return Value::integer(*integer);
    if (const auto* number = std::get_if<float>(&value)) return Value::number(double(*number));
    if (const auto* text = std::get_if<std::string>(&value)) return Value::string(*text);
    const auto& color = std::get<PhotoModeColor>(value);
    return Value::object({{"r", Value::number(double(color.r))},
                          {"g", Value::number(double(color.g))},
                          {"b", Value::number(double(color.b))},
                          {"a", Value::number(double(color.a))}});
}

/**
 * @brief 脚本提供方实现的 `IPhotoModeApplySink`。
 *
 * 每次赋值编成**一张表**（`field` / `domain` / `value`）交给脚本的 `applyPhotoModeAssignment`：
 * 这就是决定③要的"对象参数"。失败原样上报（`call` 的纪律：缺方法 = `Unsupported`，抛错 = `Failed`），
 * 因为 `PcgPhotoModeApplyPlan` 要靠返回值决定回滚，吞掉失败会让它以为写入成功。
 */
class ScriptPhotoModeApplySink final : public IPhotoModeApplySink {
public:
    explicit ScriptPhotoModeApplySink(ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const ScriptProviderHandle& handle() const noexcept { return handle_; }

    Result<void> applyPhotoModeAssignment(const PhotoModeAssignment& assignment) override {
        const std::string_view name   = domainName(assignment.domain);
        const ScriptArg        args[] = {ScriptArg::fromTable({{"field", Value::string(assignment.field)},
                                                               {"domain", Value::string(std::string(name))},
                                                               {"value", photoModeValueToScript(assignment.value)}})};
        return ScriptProviderRegistry::instance().call(handle_, "applyPhotoModeAssignment", args);
    }

private:
    ScriptProviderHandle handle_;
};

/**
 * @brief 进程级持有脚本适配器。
 *
 * 进程级是刻意的（与武器投射物接缝同一条理由）：能力表本身是进程级的，适配器的簿记必须比任何
 * 一次 `executeRegistered()` 活得久；而在自己回调里被卸载的适配器必须活到那次调用返回为止，
 * 所以 `retired` 只摘注册、保留对象。
 */
struct ScriptPhotoModeApplyProviders {
    std::vector<std::unique_ptr<ScriptPhotoModeApplySink>> live;
    std::vector<std::unique_ptr<ScriptPhotoModeApplySink>> retired;
};

ScriptPhotoModeApplyProviders& scriptPhotoModeApplyProviders() {
    static ScriptPhotoModeApplyProviders state;
    return state;
}

}  // namespace

EVENGINE_API_WORLD void declareScriptPhotoModeApplySeam() {
    ScriptPhotoModeApplyProviders& owned = scriptPhotoModeApplyProviders();
    ScriptProviderRegistry::instance().declareCapability(
        IPhotoModeApplySink::capabilityName,
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            // 单实现槽位：`provide` 会顶掉当前实现，顶掉必须可见（否则原生提供方会"悄悄失效"，
            // 而症状只是照片模式的字段不再生效）。
            if (cap::query<IPhotoModeApplySink>() != nullptr) {
                recordLogEvent("warn", "script photo-mode provider '" + handle.name +
                                           "' replaced the provider that owned IPhotoModeApplySink");
            }
            auto                      adapter = std::make_unique<ScriptPhotoModeApplySink>(handle);
            ScriptPhotoModeApplySink* raw     = adapter.get();
            // 先注册再交所有权：从这一刻起能力槽位持有裸指针，`owned.live` 是让它有效的唯一原因。
            cap::provide<IPhotoModeApplySink>(raw);
            owned.live.push_back(std::move(adapter));
            return Result<void>::success();
        },
        [&owned](const ScriptProviderHandle& handle) -> Result<void> {
            const auto match = std::find_if(owned.live.begin(), owned.live.end(),
                                            [&handle](const std::unique_ptr<ScriptPhotoModeApplySink>& adapter) {
                                                return adapter->handle() == handle;
                                            });
            if (match == owned.live.end()) {
                // 幂等：后置条件是"这一代没有适配器"，模块退出时可能已经撤过了。
                return Result<void>::success();
            }
            cap::revoke<IPhotoModeApplySink>(match->get());
            owned.retired.push_back(std::move(*match));
            owned.live.erase(match);
            return Result<void>::success();
        });
}

EVENGINE_API_WORLD void withdrawScriptPhotoModeApplySeam() {
    ScriptPhotoModeApplyProviders& owned = scriptPhotoModeApplyProviders();
    for (auto& adapter : owned.live) {
        cap::revoke<IPhotoModeApplySink>(adapter.get());
        owned.retired.push_back(std::move(adapter));
    }
    owned.live.clear();

    ScriptProviderRegistry::instance()
        .removeCapability(IPhotoModeApplySink::capabilityName)
        .ignore("module teardown may already have dropped this seam");
}

}  // namespace eve::ui
