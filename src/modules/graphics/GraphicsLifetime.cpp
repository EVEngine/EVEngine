#include <unordered_map>
#include "graphics/Graphics.h"

namespace eve::graphics {
namespace {
// Render-thread-only observation registry. Erasing on destruction prevents address reuse
// from making an old observer valid for a new Graphics instance.
bool registryAlive = false;
struct Registry {
    std::unordered_map<const Graphics*, std::shared_ptr<const void>> tokens;
    Registry() { registryAlive = true; }
    ~Registry() { registryAlive = false; }
};
auto& lifetimes() {
    static Registry registry;
    return registry.tokens;
}
}  // namespace
std::weak_ptr<const void> Graphics::resourceLifetime() const {
    auto& token = lifetimes()[this];
    if (!token) token = std::make_shared<const int>(0);
    return token;
}
void Graphics::retireResourceLifetime() const {
    if (registryAlive) lifetimes().erase(this);
}
}  // namespace eve::graphics
