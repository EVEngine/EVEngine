#pragma once
#include "common/Result.h"
#include <string>
namespace eve::procgen {
/** @brief Observable lifecycle of a Pcg spawning progress operation. */
enum class PcgSpawnProgressStatus { Hidden = 0, Active = 1, CancelRequested = 2, Completed = 3 };
/** @brief Caller-owned, deterministic progress state corresponding to Pcg SpawnProgressBar. @thread Spawning thread only. */
class PcgSpawnProgress {
public:
    /** @brief Start or advance a spawner rule and reset its fractional progress. */
    [[nodiscard]] Result<PcgSpawnProgressStatus> updateRule(std::string spawnerName, int totalRuleCount,
                                                             int totalRulesCompleted, int spawnerRuleCount,
                                                             int spawnerRulesCompleted);
    /** @brief Update the current rule fraction in [0,1]. */
    [[nodiscard]] Result<PcgSpawnProgressStatus> updateRuleFraction(double progress);
    /** @brief Mark cancellation requested while retaining the snapshot. */
    [[nodiscard]] Result<PcgSpawnProgressStatus> requestCancel();
    /** @brief Clear the progress state. */
    [[nodiscard]] PcgSpawnProgressStatus clear() noexcept;
    /** @brief Return Pcg's inverse-lerp total progress. */
    [[nodiscard]] double progress() const noexcept;
    /** @brief Return the fixed title. */
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    /** @brief Return the formatted rule subtitle. */
    [[nodiscard]] const std::string& subtitle() const noexcept { return subtitle_; }
    /** @brief Return the lifecycle state. */
    [[nodiscard]] PcgSpawnProgressStatus status() const noexcept { return status_; }
private:
    std::string title_;
    std::string subtitle_;
    int totalRuleCount_ = 0;
    int totalRulesCompleted_ = 0;
    int spawnerRuleCount_ = 0;
    int spawnerRulesCompleted_ = 0;
    double ruleProgress_ = 0;
    PcgSpawnProgressStatus status_ = PcgSpawnProgressStatus::Hidden;
};
}  // namespace eve::procgen
