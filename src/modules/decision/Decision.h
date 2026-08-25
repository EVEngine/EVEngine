#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "common/Module.h"
namespace eve::decision {
/** @brief Data-oriented blackboard, FSM, utility and influence-map workspace. */
class DecisionContext {
public:
    /** @brief Sets a scalar JSON value (null, boolean, number or string) on a named board. */ bool set(
        const std::string& board, const std::string& key, const std::string& valueJson);
    /** @brief Gets canonical scalar JSON or the supplied fallback. */ std::string get(
        const std::string& board, const std::string& key, const std::string& fallbackJson) const;
    /** @brief Adds/replaces a finite-state transition. */ bool addTransition(const std::string& machine,
                                                                              const std::string& from,
                                                                              const std::string& trigger,
                                                                              const std::string& to);
    /** @brief Sets current state explicitly. */ bool setState(const std::string& machine, const std::string& state);
    /** @brief Applies an explicit trigger and returns whether a transition fired. */ bool trigger(
        const std::string& machine, const std::string& trigger);
    /** @brief Gets current machine state. */ std::string state(const std::string& machine) const;
    /** @brief Scores CSV considerations formatted value:weight using a weighted mean. */ static float utility(
        const std::string& considerationsCsv);
    /** @brief Selects an option from name=considerations; entries; ties use lexical name. */ static std::string choose(
        const std::string& options);
    /** @brief Creates or replaces a zero-filled 2D influence grid. */ bool newGrid(const std::string& name, int width,
                                                                                    int height, float cellSize,
                                                                                    float originX, float originY);
    /** @brief Sets/adds a grid cell. */ bool setCell(const std::string& name, int x, int y, float value);
    bool                                      addCell(const std::string& name, int x, int y, float delta);
    /** @brief Samples the containing cell, returning fallback outside the grid. */ float sample(
        const std::string& name, float worldX, float worldY, float fallback) const;
    /** @brief Exports deterministic JSON. */ std::string   snapshotJson() const;
    /** @brief Restores a snapshot transactionally. */ bool restoreJson(const std::string& json);
    /** @brief Last validation error. */ const std::string& lastError() const { return lastError_; }

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
    std::string                                                                       lastError_;
};
/** @brief Script factory for independent decision contexts. */
class Decision : public Module {
public:
    Module_REG(Decision);
    DecisionContext* newContext();

private:
    std::vector<std::unique_ptr<DecisionContext>> contexts_;
};
}  // namespace eve::decision
