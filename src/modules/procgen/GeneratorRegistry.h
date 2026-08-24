#pragma once

#include "procgen/Grid2D.h"
#include "procgen/Params.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::procgen {

using GeneratorFn = std::function<bool(const Params &params, Grid2D &out, std::string &error)>;

/** @brief Semantic type used by schema-driven generator parameter editors. */
enum class ParamKind { Integer, Float, Boolean, String, Choice };

/** @brief UI-independent metadata for one procedural generator parameter. */
struct ParamDescriptor {
    std::string              key;
    std::string              displayName;
    std::string              description;
    std::string              category = "General";
    ParamKind                kind = ParamKind::String;
    std::string              defaultValue;
    bool                     hasMinimum = false;
    bool                     hasMaximum = false;
    double                   minimum = 0.0;
    double                   maximum = 0.0;
    double                   step = 0.0;
    std::vector<std::string> choices;
    bool                     advanced = false;

    /**
     * @brief Construct a bounded integer descriptor.
     * @param key Stable key. @param label Display label. @param defaultValue Default value.
     * @param minimum Minimum value. @param maximum Maximum value. @param step Editing step.
     * @return Complete descriptor.
     */
    static ParamDescriptor integer(std::string key, std::string label, int defaultValue,
                                   int minimum, int maximum, int step = 1);
    /**
     * @brief Construct a bounded floating-point descriptor.
     * @param key Stable key. @param label Display label. @param defaultValue Default value.
     * @param minimum Minimum value. @param maximum Maximum value. @param step Editing step.
     * @return Complete descriptor.
     */
    static ParamDescriptor floating(std::string key, std::string label, float defaultValue,
                                    float minimum, float maximum, float step);
    /**
     * @brief Construct a Boolean descriptor stored as zero or one in Params.
     * @param key Stable key. @param label Display label. @param defaultValue Default value.
     * @return Complete descriptor.
     */
    static ParamDescriptor boolean(std::string key, std::string label, bool defaultValue);
    /**
     * @brief Construct a free-form string descriptor.
     * @param key Stable key. @param label Display label. @param defaultValue Default value.
     * @return Complete descriptor.
     */
    static ParamDescriptor text(std::string key, std::string label, std::string defaultValue);
    /**
     * @brief Construct a finite-choice string descriptor.
     * @param key Stable key. @param label Display label. @param defaultValue Default choice.
     * @param choices Allowed values. @return Complete descriptor.
     */
    static ParamDescriptor choice(std::string key, std::string label, std::string defaultValue,
                                  std::vector<std::string> choices);
};

/** @brief Complete metadata and parameter schema for one generator algorithm. */
struct GeneratorDescriptor {
    std::string                  id;
    std::string                  displayName;
    std::string                  category;
    std::vector<ParamDescriptor> params;

    /**
     * @brief Create a grid generator schema with seed, width and height parameters.
     * @param id Stable algorithm id.
     * @param displayName Human-readable name.
     * @param category Browser category.
     * @param minimumWidth Minimum supported width.
     * @param minimumHeight Minimum supported height.
     * @param maximumWidth Maximum supported width.
     * @param maximumHeight Maximum supported height.
     * @return Descriptor initialized with common grid parameters.
     */
    static GeneratorDescriptor grid(std::string id, std::string displayName,
                                    std::string category, int minimumWidth,
                                    int minimumHeight, int maximumWidth = 4096,
                                    int maximumHeight = 4096);
    /**
     * @brief Find one parameter descriptor by stable key.
     * @param key Stable parameter key. @return Descriptor or nullptr.
     */
    const ParamDescriptor *find(const std::string &key) const;
};

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
