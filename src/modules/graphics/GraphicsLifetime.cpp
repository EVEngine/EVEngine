#include <cstdio>
#include <unordered_map>
#include "common/Subscription.h"
#include "graphics/Graphics.h"
namespace eve::graphics {
namespace {
struct Lifetime {
    eve::Observer<> retiring;
    bool            retired = false;
};
bool registryAlive = false;
struct Registry {
    std::unordered_map<const Graphics*, std::shared_ptr<Lifetime>> tokens;
    Registry() { registryAlive = true; }
    ~Registry() { registryAlive = false; }
};
auto& lifetimes() {
    static Registry registry;
    return registry.tokens;
}
auto lifetime(const Graphics* graphics) {
    auto& value = lifetimes()[graphics];
    if (!value) value = std::make_shared<Lifetime>();
    return value;
}
}  // namespace
std::weak_ptr<const void> Graphics::resourceLifetime() const {
    auto value = lifetime(this);
    return value->retired ? std::weak_ptr<const void>{} : value;
}
Result<eve::Subscription> Graphics::onResourcesRetiring(std::function<void()> callback) const {
    auto value = lifetime(this);
    if (value->retired)
        return Result<eve::Subscription>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Graphics resources are retiring"));
    return Result<eve::Subscription>::success(value->retiring.subscribe(std::move(callback)));
}
void Graphics::retireResourceLifetime() const {
    if (!registryAlive) return;
    auto it = lifetimes().find(this);
    if (it == lifetimes().end() || it->second->retired) return;
    auto value          = it->second;
    value->retired      = true;
    const auto failures = value->retiring.notifyChecked([]() noexcept {});
    if (failures) std::fprintf(stderr, "Graphics resource retirement: %zu callback failures\n", failures);
    lifetimes().erase(this);
}
}  // namespace eve::graphics
