#pragma once

#include "procgen/Grid2D.h"
#include "procgen/Params.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

using GeneratorFn = std::function<bool(const Params &params, Grid2D &out, std::string &error)>;

class GeneratorRegistry {
public:
    static GeneratorRegistry &instance();

    void registerAlgorithm(const std::string &id, GeneratorFn fn);
    bool has(const std::string &id) const;
    bool generate(const std::string &id, const Params &params, Grid2D &out, std::string &error) const;
    std::vector<std::string> list() const;

    /** Register built-in DTL / custom map algorithms (idempotent). */
    void registerBuiltins();

private:
    GeneratorRegistry() = default;
    std::unordered_map<std::string, GeneratorFn> algorithms_;
    bool builtinsRegistered_ = false;
};

}  // namespace eve::procgen
