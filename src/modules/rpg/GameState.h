#pragma once

/**
 * @file GameState.h
 * @brief 全局游戏状态：开关（bool）、变量（数字）、独立变量（按作用域）。
 *
 * RPG Maker 的 $gameSwitches / $gameVariables / self variables 模型。一份
 * GameState 对应一份存档/一局游戏；引擎不解释名字，事件命令/脚本读写即可。
 * 进程级全局单例便于脚本随处访问，也可为多存档建多份实例。
 */

#include "common/Result.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace eve::rpg {

class RPGSaveSession;

/** @brief 一份游戏状态（开关/变量/独立变量）。 */
class GameState {
public:
    GameState() = default;

    /** @brief 设置开关。 */
    void setSwitch(const std::string &name, bool on);
    void switchOn(const std::string &name);
    void switchOff(const std::string &name);
    /** @brief 开关是否打开。 */
    bool isSwitchOn(const std::string &name) const;

    /** @brief 设置/读取/增减变量。 */
    void setVariable(const std::string &name, double value);
    double getVariable(const std::string &name) const;
    void addVariable(const std::string &name, double delta);

    /** @brief 独立变量：scope（如 "map:1:event:3"）下按名字存数字。 */
    void setSelfVariable(const std::string &scope, const std::string &name, double value);
    double getSelfVariable(const std::string &scope, const std::string &name) const;
    bool hasSelfVariable(const std::string &scope, const std::string &name) const;

    /** @brief 清空全部状态。 */
    void clear();

    /**
     * @brief Serialize this state as the canonical versioned RPG game-state JSON document.
     * @return Owning deterministic JSON, or a structured serialization failure.
     * @remarks Schema `eve.rpg.game-state` version 1 preserves switches, numeric
     * variables and scoped variables. Unknown fields are reserved and ignored
     * when restoring the same version.
     * @thread Call on the owning simulation thread; no internal synchronization is performed.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<std::string> snapshotJson() const;

    /**
     * @brief Validate and atomically restore a canonical RPG game-state JSON document.
     * @param json UTF-8 JSON produced by snapshotJson().
     * @return Success after one commit, or a structured parse/schema/version failure.
     * @remarks A failed restore leaves every current value unchanged. Version 1
     * accepts unknown fields but rejects unknown schema ids and versions.
     * @thread Call on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view json);

    /** @brief 进程级全局单例（脚本便利入口）。 */
    static GameState &global();

private:
    friend class RPGSaveSession;
    std::unordered_map<std::string, bool> switches_;
    std::unordered_map<std::string, double> variables_;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> selfVariables_;
};

}  // namespace eve::rpg
