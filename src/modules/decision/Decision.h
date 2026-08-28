#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "common/Module.h"
#include "common/Result.h"
#include "common/SquirrelOwnership.h"
namespace eve::decision {
/** @brief Handle domain for module-owned decision contexts. */
struct DecisionContextHandleTag {};
/** @brief Generation- and module-epoch-qualified decision context reference. */
using DecisionContextHandleRef = eve::script::RuntimeHandleRef<DecisionContextHandleTag>;
/** @brief Data-oriented blackboard, FSM, utility and influence-map workspace. */
class DecisionContext {
public:
    /**
     * @brief Sets a scalar JSON value on a named blackboard.
     * @return Success when the value is valid and the board entry was written;
     *         otherwise a structured validation diagnostic.
     */
    [[nodiscard]] eve::Result<void> set(const std::string& board, const std::string& key, const std::string& valueJson);
    /** @brief Gets canonical scalar JSON or the supplied fallback. */ std::string get(
        const std::string& board, const std::string& key, const std::string& fallbackJson) const;
    /**
     * @brief Adds or replaces a finite-state transition.
     * @return Success when the transition was stored; invalid names return a
     *         structured validation diagnostic.
     */
    [[nodiscard]] eve::Result<void> addTransition(const std::string& machine, const std::string& from,
                                                  const std::string& trigger, const std::string& to);
    /**
     * @brief Sets the current state explicitly.
     * @return Success when the state was stored, otherwise a validation diagnostic.
     */
    [[nodiscard]] eve::Result<void> setState(const std::string& machine, const std::string& state);
    /**
     * @brief Applies an explicit trigger.
     * @return A successful result with `true`/`Applied` when a transition fired,
     *         or `false`/`NoOp` when no transition matches. Missing machines and
     *         invalid arguments are reported as structured failures.
     */
    [[nodiscard]] eve::Result<bool> trigger(const std::string& machine, const std::string& trigger);
    /** @brief Gets current machine state. */ std::string state(const std::string& machine) const;
    /** @brief Scores CSV considerations formatted value:weight using a weighted mean. */ static float utility(
        const std::string& considerationsCsv);
    /** @brief Selects an option from name=considerations; entries; ties use lexical name. */ static std::string choose(
        const std::string& options);
    /**
     * @brief Creates or replaces a zero-filled 2D influence grid.
     * @return Success when the grid dimensions and geometry are valid.
     */
    [[nodiscard]] eve::Result<void> newGrid(const std::string& name, int width, int height, float cellSize,
                                            float originX, float originY);
    /** @brief Sets one grid cell, or returns a structured not-found/validation failure. */
    [[nodiscard]] eve::Result<void> setCell(const std::string& name, int x, int y, float value);
    /** @brief Adds to one grid cell, or returns a structured not-found/validation failure. */
    [[nodiscard]] eve::Result<void> addCell(const std::string& name, int x, int y, float delta);
    /** @brief Samples the containing cell, returning fallback outside the grid. */ float sample(
        const std::string& name, float worldX, float worldY, float fallback) const;
    /** @brief Exports deterministic JSON. */ std::string   snapshotJson() const;
    /**
     * @brief Restores a version-1 snapshot transactionally.
     * @return Success after the complete snapshot has been validated and
     *         published; malformed input leaves the current context unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

private:
    struct Grid {
        int                w = 0, h = 0;
        float              cell = 1, ox = 0, oy = 0;
        std::vector<float> values;
    };
    std::map<std::string, std::map<std::string, std::string>>                         boards_;
    std::map<std::string, std::string>                                                states_;
    std::map<std::string, std::map<std::pair<std::string, std::string>, std::string>> transitions_;
    std::map<std::string, Grid>                                                       grids_;
};
/** @brief Script factory for independent decision contexts. */
class Decision : public Module {
public:
    Module_REG(Decision);
    /** @brief Allocates a module-owned decision context and returns its handle. */
    [[nodiscard]] static eve::Result<DecisionContextHandleRef> newContext();
    /** @brief Resolves a live context as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<DecisionContext> resolve(DecisionContextHandleRef reference) noexcept;
    /** @brief Releases a module-owned decision context. */
    [[nodiscard]] static eve::Result<void> release(DecisionContextHandleRef reference);
    /** @brief Reports whether a decision context reference is stale. */
    [[nodiscard]] static bool isStale(DecisionContextHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<DecisionContext, DecisionContextHandleTag> contexts_;
};
}  // namespace eve::decision
