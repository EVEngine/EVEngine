#include "procgen/GeneratorRegistry.h"

#include <algorithm>

namespace eve::procgen {

GeneratorRegistry &GeneratorRegistry::instance() {
    static GeneratorRegistry reg;
    return reg;
}

void GeneratorRegistry::registerAlgorithm(const std::string &id, GeneratorFn fn) {
    algorithms_[id] = std::move(fn);
}

bool GeneratorRegistry::has(const std::string &id) const {
    return algorithms_.find(id) != algorithms_.end();
}

bool GeneratorRegistry::generate(const std::string &id, const Params &params, Grid2D &out,
                                 std::string &error) const {
    auto it = algorithms_.find(id);
    if (it == algorithms_.end()) {
        error = "unknown algorithm: " + id;
        return false;
    }
    return it->second(params, out, error);
}

std::vector<std::string> GeneratorRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(algorithms_.size());
    for (const auto &kv : algorithms_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace eve::procgen
