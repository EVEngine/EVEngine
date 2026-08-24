#pragma once

#include "procgen/Grid2D.h"
#include "procgen/ParamSchema.h"
#include "procgen/Params.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::procgen {

using GeneratorFn = std::function<bool(const Params &params, Grid2D &out, std::string &error)>;

/** @brief Backward-compatible name for the shared recipe schema. */
using GeneratorDescriptor = RecipeDescriptor;

/** @brief Registry for executable generators and their reflection metadata. */
class GeneratorRegistry {
public:
    /** @brief Return the process-wide generator registry. */
    static GeneratorRegistry &instance();

    /**
     * @brief Register an algorithm without metadata for backward compatibility.
     * @param id Stable id. @param fn Generator callback.
     */
    void registerAlgorithm(const std::string &id, GeneratorFn fn);
    /**
     * @brief Register an algorithm together with its dynamic-editor schema.
     * @param descriptor Schema and id. @param fn Generator callback.
     */
    void registerAlgorithm(GeneratorDescriptor descriptor, GeneratorFn fn);
    /** @brief Return true when an executable algorithm is registered.
     * @param id Algorithm id. @return Whether it exists. */
    bool has(const std::string &id) const;
    /**
     * @brief Execute one registered algorithm.
     * @param id Algorithm id. @param params Input parameters. @param out Output grid.
     * @param error Error text. @return Whether generation succeeded.
     */
    bool generate(const std::string &id, const Params &params, Grid2D &out, std::string &error) const;
    /** @brief Return sorted registered algorithm ids. */
    std::vector<std::string> list() const;
    /**
     * @brief Return reflection metadata for an algorithm.
     * @param id Algorithm id. @return Descriptor or nullptr.
     */
    const GeneratorDescriptor *descriptor(const std::string &id) const;
    /**
     * @brief Fill only missing algorithm-specific values from the registered schema.
     * @param id Algorithm id. @param params Parameters to update. @return Whether a schema exists.
     */
    bool applyDefaults(const std::string &id, Params &params) const;

    /** @brief Register built-in DTL / custom map algorithms (idempotent). */
    void registerBuiltins();

private:
    struct Entry {
        GeneratorFn          generate;
        GeneratorDescriptor descriptor;
    };
    GeneratorRegistry() = default;
    std::unordered_map<std::string, Entry> algorithms_;
    bool builtinsRegistered_ = false;
};

}  // namespace eve::procgen
