#include "procgen/GeneratorRegistry.h"

#include <algorithm>
namespace eve::procgen {

GeneratorRegistry &GeneratorRegistry::instance() {
    static GeneratorRegistry reg;
    return reg;
}

void GeneratorRegistry::registerAlgorithm(const std::string &id, GeneratorFn fn) {
    GeneratorDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    registerAlgorithm(std::move(descriptor), std::move(fn));
}

void GeneratorRegistry::registerAlgorithm(GeneratorDescriptor descriptor, GeneratorFn fn) {
    const std::string id = descriptor.id;
    algorithms_[id] = Entry{std::move(fn), std::move(descriptor)};
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
    return it->second.generate(params, out, error);
}

const GeneratorDescriptor *GeneratorRegistry::descriptor(const std::string &id) const {
    const auto it = algorithms_.find(id);
    return it == algorithms_.end() ? nullptr : &it->second.descriptor;
}

bool GeneratorRegistry::applyDefaults(const std::string &id, Params &params) const {
    const GeneratorDescriptor *schema = descriptor(id);
    if (!schema) return false;
    schema->applyDefaults(params);
    return true;
}

std::vector<std::string> GeneratorRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(algorithms_.size());
    for (const auto &kv : algorithms_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace eve::procgen
