#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <map>
#include <string>
#include <vector>

namespace ssq {
class Table;
}
namespace eve::graphics {
class Mesh;
}

namespace eve::avatar {

/**
 * @brief Renderer-independent character appearance authoring and runtime state.
 * Owns its validated definition, parameters, wardrobe selections and bounded history.
 * All calls are owner-thread affine, synchronous and callback-free. Inputs are copied;
 * returned Values own their storage. No renderer, skeleton or equipment pointers are retained.
 * Definition changes reset history; failed changes preserve all observable state.
 */
class EVENGINE_API_DOMAINS Appearance {
public:
    /** @brief Validate and replace a complete eve.appearance.definition v1 document.
     * @cost Linear in document size plus reference validation, once per asset load.
     * @return InvalidArgument on malformed schemas, references, ranges or defaults.
     */
    [[nodiscard]] Result<void> configure(const Value& definition);
    /** @brief Atomically patch parameters and selections; reject unknown ids, conflicts and out-of-range values.
     * Equal patches do not create history. Up to 64 previous states are retained.
     * @cost Copies current state and validates wardrobe pairs, once per user edit, never per frame.
     */
    [[nodiscard]] Result<void> apply(const Value& patch);
    /** @brief Restore an exact eve.appearance.state v1 snapshot for the currently loaded profile.
     * Unknown fields, missing values, versions and foreign profile ids are rejected atomically.
     * @cost Linear in snapshot size plus wardrobe validation, once per restore.
     */
    [[nodiscard]] Result<void> restore(const Value& snapshot);
    /** @brief Undo the last applied edit; NotFound when history is empty. */
    [[nodiscard]] Result<void> undo();
    /** @brief Redo the last undone edit; NotFound when history is empty. */
    [[nodiscard]] Result<void> redo();
    /** @brief Query whether an undo state exists, without evaluating anything. */
    bool canUndo() const { return !undo_.empty(); }
    /** @brief Query whether a redo state exists, without evaluating anything. */
    bool canRedo() const { return !redo_.empty(); }
    /** @brief Return an owning versioned snapshot of authoritative parameter and selection state.
     * @cost Copies all current values once per save or inspector refresh.
     */
    Value snapshot() const;
    /** @brief Evaluate additive channels and multi-input corrective drivers, and derive wardrobe visibility.
     * Channels are ordered by id; driver activation is clamped linear interpolation from start to end.
     * Inputs in a driver multiply, drivers on the same output add. No state is mutated.
     * @cost Linear in driver inputs plus selected wardrobe parts, once per changed appearance.
     */
    Value evaluate() const;
    /** @brief Validate all driven morph names before writing weights to a borrowed mesh.
     * Only outputs beginning with prefix are applied, with the prefix removed from morph names.
     * Unrelated expression morphs remain unchanged. Graphics::bakeMeshMorph uploads the result.
     * No references survive the call. Render-thread only, no callbacks. Missing targets leave the mesh unchanged.
     * @cost Evaluates all drivers and checks selected target names, once per changed appearance.
     */
    [[nodiscard]] Result<void> applyMorphWeights(graphics::Mesh& mesh, std::string_view prefix) const;

private:
    struct Parameter {
        double minimum = 0, maximum = 1, initial = 0;
    };
    struct Input {
        std::string parameter;
        double      start = 0, end = 1;
    };
    struct Driver {
        std::string        output;
        double             scale = 1;
        std::vector<Input> inputs;
    };
    struct Item {
        std::string              slot;
        std::vector<std::string> parts, hides, excludes;
    };
    struct State {
        std::map<std::string, double>      parameters;
        std::map<std::string, std::string> selections;
        bool                               operator==(const State&) const = default;
    };
    [[nodiscard]] Result<void>       validate(const State& candidate) const;
    [[nodiscard]] Result<void>       publish(State candidate);
    std::string                      profile_;
    std::map<std::string, Parameter> parameters_;
    std::map<std::string, Item>      items_;
    std::vector<Driver>              drivers_;
    State                            state_;
    std::vector<State>               undo_, redo_;
};

/** @brief Register VM-owned Appearance objects. Table is borrowed only during registration.
 * @thread VM owner thread; registered methods do not invoke script callbacks.
 */
void exposeAppearance(ssq::Table& table);

}  // namespace eve::avatar
