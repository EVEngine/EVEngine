#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/StateValue.h"
#include "common/Value.h"

#include <squirrel.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace eve::avatar {
class AvatarInstance;
}
#include <utility>
#include <vector>

namespace ssq {
class Object;
}

namespace eve::dialogue {

/**
 * @brief Canonical data value used by dialogue pools, conditions and scripts.
 *
 * These aliases intentionally do not add another storage tree. The factory
 * names and scalar helpers are retained by `eve::Value` for source-compatible
 * dialogue scripts and tests, while all new cross-module data uses the common
 * deterministic value protocol.
 */
using DataValue = eve::Value;
using VarValue  = eve::Value;

/**
 * @brief Visual-novel style dialogue stage.
 * Script: `dlg <- eve.Dialogue();`
 *
 * Dialogue scripts remain Squirrel (functions / generators). This module only
 * owns speaker lines, typewriter, choices, and avatar stage slots.
 */
class EVENGINE_API_ORCHESTRATION Dialogue : public Module {
public:
    // Keep the historical nested names as zero-cost aliases for C++ callers.
    using DataValue = eve::Value;
    using VarValue  = eve::Value;

    Module_REG(Dialogue);
    /** @brief Dialogue. */
    Dialogue();
    /** @brief Dialogue. */
    ~Dialogue() override;

    /** @brief 角色：注册 / 查询 / 绑定 Avatar。 */
    bool registerCharacter(const std::string &id, const std::string &displayName);
    /** @brief True when character. */
    bool hasCharacter(const std::string &id) const;
    /** @brief Returns the display name. */
    std::string getDisplayName(const std::string &id) const;
    /** @brief Binds avatar. */
    bool bindAvatar(const std::string &id, avatar::AvatarInstance *av);
    /** @brief Returns the avatar. */
    avatar::AvatarInstance *getAvatar(const std::string &id) const;
    /** @brief Returns the character count. */
    int getCharacterCount() const;
    /** @brief Returns the character id. */
    std::string getCharacterId(int index) const;

    /** @brief 舞台：显示/隐藏角色、槽位与表情/动作。 */
    bool show(const std::string &id, const std::string &slot);
    /** @brief Hide. */
    bool hide(const std::string &id);
    /** @brief True when shown. */
    bool isShown(const std::string &id) const;
    /** @brief Returns the slot. */
    std::string getSlot(const std::string &id) const;
    /** @brief Sets the slot x. */
    void setSlotX(const std::string &slot, float xNorm);
    /** @brief Returns the slot x. */
    float getSlotX(const std::string &slot) const;
    /** @brief Sets the expression. */
    bool setExpression(const std::string &id, const std::string &expression);
    /** @brief Sets the motion. */
    bool setMotion(const std::string &id, const std::string &motion);
    /** Place visible avatars using normalized slot X * stageWidth. */
    /** @brief Synchronizes stage. */
    void syncStage(float stageWidth, float stageHeight);

    /** @brief 台词：说话/旁白、打字机效果与推进。 */
    void say(const std::string &speakerId, const std::string &text);
    /** @brief Narrate. */
    void narrate(const std::string &text);
    /** @brief Sets the type speed. */
    void setTypeSpeed(float charsPerSecond);
    /** @brief Returns the type speed. */
    float getTypeSpeed() const { return typeSpeed_; }
    /** @brief Skip typing. */
    void skipTyping();
    /** @brief True when typing. */
    bool isTyping() const;
    /** @brief True when waiting advance. */
    bool isWaitingAdvance() const;
    /** @brief True when idle. */
    bool isIdle() const;
    /** @brief Advance. */
    void advance();

    /** @brief Returns the speaker id. */
    std::string getSpeakerId() const { return speakerId_; }
    /** @brief Returns the speaker name. */
    std::string getSpeakerName() const;
    /** @brief Returns the full text. */
    std::string getFullText() const { return fullText_; }
    /** @brief Returns the visible text. */
    std::string getVisibleText() const;
    /** @brief Returns the phase. */
    std::string getPhase() const;

    /** @brief 口型同步：打字时驱动说话者 Avatar 参数。 */
    void setLipSyncEnabled(bool enabled);
    /** @brief True when lip sync enabled. */
    bool isLipSyncEnabled() const { return lipSyncEnabled_; }
    /** @brief Sets the lip sync parameter. */
    void setLipSyncParameter(const std::string &name);
    /** @brief Returns the lip sync parameter. */
    std::string getLipSyncParameter() const { return lipSyncParameter_; }
    /** @brief Sets the lip sync amplitude. */
    void setLipSyncAmplitude(float amplitude);
    /** @brief Returns the lip sync amplitude. */
    float getLipSyncAmplitude() const { return lipSyncAmplitude_; }
    /** @brief Returns the lip sync value. */
    float getLipSyncValue() const { return lipSyncValue_; }

    /** @brief 选项：清空/添加/展示与选择。 */
    void clearChoices();
    /** @brief Adds choice. */
    bool addChoice(const std::string &id, const std::string &label);
    /** @brief Present choices. */
    void presentChoices();
    /** @brief True when waiting choice. */
    bool isWaitingChoice() const;
    /** @brief Returns the choice count. */
    int getChoiceCount() const;
    /** @brief Returns the choice id. */
    std::string getChoiceId(int index) const;
    /** @brief Returns the choice label. */
    std::string getChoiceLabel(int index) const;
    /** @brief Select choice. */
    bool selectChoice(int index);
    /** @brief Returns the selected choice id. */
    std::string getSelectedChoiceId() const { return selectedChoiceId_; }

    // ---- variables (global / scene; scene auto-cleared on scene switch) ----
    /** Squirrel-facing setter: value may be int/float/bool/string. */
    /** @brief Sets the var. */
    bool setVar(const std::string &name, ssq::Object value, const std::string &scope);
    /** C++ core setter (also used by unit tests). */
    /** @brief Sets the var value. */
    bool setVarValue(const std::string &name, const VarValue &value, const std::string &scope);
    /** @brief Returns the var value. */
    VarValue getVarValue(const std::string &name, const std::string &scope) const;
    /** @brief Returns the var type. */
    std::string getVarType(const std::string &name, const std::string &scope) const;
    /** @brief Returns the var int. */
    int getVarInt(const std::string &name, int defaultValue, const std::string &scope) const;
    /** @brief Returns the var float. */
    float getVarFloat(const std::string &name, float defaultValue, const std::string &scope) const;
    /** @brief Returns the var bool. */
    bool getVarBool(const std::string &name, bool defaultValue, const std::string &scope) const;
    /** @brief Returns the var string. */
    std::string getVarString(const std::string &name, const std::string &defaultValue,
                             const std::string &scope) const;
    /** @brief True when var. */
    bool hasVar(const std::string &name, const std::string &scope) const;
    /** @brief Clears var. */
    bool clearVar(const std::string &name, const std::string &scope);
    /** @brief Clears vars. */
    void clearVars(const std::string &scope);

    // ---- conditions ----
    /** Register a Squirrel predicate: fn(ctx) -> bool, ctx = {vars, params, lineId}. */
    /** @brief Registers condition. */
    bool registerCondition(const std::string &name, ssq::Object fn);
    /** @brief Unregisters condition. */
    bool unregisterCondition(const std::string &name);
    /** @brief Eval condition. */
    bool evalCondition(ssq::Object table);
    /** @brief Eval condition data. */
    bool evalConditionData(const DataValue &cond);

    // ---- content pools ----
    /** @brief Loads pools from table. */
    int loadPoolsFromTable(ssq::Object table);
    /** @brief Loads pools from data. */
    int loadPoolsFromData(const DataValue &root);
    /** @brief Atomically replace all pools from a validated unified dnut workspace. */
    int replacePoolsFromData(const DataValue& root);
    /** @brief Clears pools. */
    void clearPools();
    /** @brief Returns the pool count. */
    int getPoolCount() const;
    /** @brief Returns the pool id. */
    std::string getPoolId(int index) const;
    /** @brief True when pool. */
    bool hasPool(const std::string &id) const;
    /** @brief Returns the last pools error. */
    std::string getLastPoolsError() const { return lastPoolsError_; }

    // ---- rng (weighted selection determinism) ----
    /** Reseed the weighted picker; also resets per-pool no-repeat history. */
    /** @brief Sets the random seed. */
    void setRandomSeed(int seed);
    /** @brief Returns the random seed. */
    int getRandomSeed() const { return int(rngState_); }

    // ---- generated-line selection / play ----
    /** @brief Pick line. */
    std::string pickLine(const std::string &poolId, ssq::Object params);
    /** @brief Pick line with params. */
    std::string pickLineWithParams(const std::string &poolId,
                                   const std::unordered_map<std::string, VarValue> &params);
    /** @brief Play line. */
    bool playLine(const std::string &lineId, ssq::Object params);
    /** @brief Play line with params. */
    bool playLineWithParams(const std::string &lineId,
                            const std::unordered_map<std::string, VarValue> &params);
    /** @brief Play pool. */
    bool playPool(const std::string &poolId, ssq::Object params);
    /** @brief Play pool with params. */
    bool playPoolWithParams(const std::string &poolId,
                            const std::unordered_map<std::string, VarValue> &params);
    /** @brief Returns the current line id. */
    std::string getCurrentLineId() const { return currentLineId_; }
    /** @brief Returns the current line meta. */
    std::string getCurrentLineMeta(const std::string &field) const;
    /** @brief Returns the current line tags. */
    std::vector<std::string> getCurrentLineTags() const;

    /** @brief 推进打字机 / 口型同步 / 阶段机；每帧调用。 */
    void update(float dt);
    /** @brief 重置舞台与台词状态。 */
    void reset();

    /** @brief Serialize conversation state (vars, rng, phase, choices, stage). */
    bool captureState(StateValue& out) const;

    /**
     * @brief Restore conversation state captured by captureState().
     * @return false when the captured
     * state is malformed; the reload session
     *         then falls back to resetToDefaults().
     */
    bool restoreState(const StateValue& in, std::string* err = nullptr);

    /** @brief Reset stage and line state (restore fallback). */
    bool resetToDefaults();

private:
    int applyPoolsFromData(const DataValue& root, bool replace);
    struct Character {
        std::string id;
        std::string displayName;
        avatar::AvatarInstance *avatar = nullptr;
        std::optional<size_t> avatarHook;  // destroy-hook id on the bound avatar
        std::string slot;
        bool shown = false;
    };

    struct Choice {
        std::string id;
        std::string label;
    };

    enum class Phase { Idle, Typing, WaitingAdvance, WaitingChoice };

    Character *findCharacter(const std::string &id);
    const Character *findCharacter(const std::string &id) const;
    void beginLine(const std::string &speakerId, const std::string &text);

    std::vector<Character> characters_;
    std::unordered_map<std::string, float> slotX_;  // normalized 0..1

    Phase phase_ = Phase::Idle;
    std::string speakerId_;
    std::string fullText_;
    float typeSpeed_ = 40.f;  // chars / second
    float typed_ = 0.f;

    std::vector<Choice> choices_;
    std::string selectedChoiceId_;

    bool lipSyncEnabled_ = true;
    std::string lipSyncParameter_ = "mouthOpen";
    float lipSyncAmplitude_ = 0.85f;
    float lipSyncValue_ = 0.f;
    float lipSyncTime_ = 0.f;

    void updateLipSync(float dt);
    void applyLipSyncToSpeaker();

    // ---- procedural content (variables / conditions / pools) ----
    struct Condition {
        /** @brief Kind public API. */
        enum class Kind { Always, Cmp, All, Any, Not, Script };

        Kind kind = Kind::Always;
        std::string var;
        std::string op;
        VarValue value;
        std::vector<Condition> children;
        std::string script;
    };

    struct Line {
        std::string id;
        std::string speaker;
        std::string text;
        std::string i18nKey;
        double weight = 1.0;
        Condition when;
        std::unordered_map<std::string, std::string> meta;
        std::vector<std::string> tags;
    };

    struct Pool {
        std::string id;
        int noRepeat = 3;
        std::vector<Line> lines;
        std::vector<size_t> recent;
    };

    std::unordered_map<std::string, VarValue> *varsForScope(const std::string &scope);
    const std::unordered_map<std::string, VarValue> *varsForScope(const std::string &scope) const;
    std::unordered_map<std::string, VarValue> mergedVars(
        const std::unordered_map<std::string, VarValue> &params) const;
    std::unordered_map<std::string, std::string> stringParams(
        const std::unordered_map<std::string, VarValue> &vars) const;
    bool parseCondition(const DataValue &v, Condition &out, std::string &error);
    bool parseLineData(const std::string &poolId, const DataValue &v, int index, Line &line,
                       std::string &error);
    bool evalConditionInternal(const Condition &c,
                               const std::unordered_map<std::string, VarValue> &merged,
                               const std::unordered_map<std::string, VarValue> &params,
                               const std::string &lineId) const;
    bool evalScriptPredicate(const std::string &name,
                             const std::unordered_map<std::string, VarValue> &merged,
                             const std::unordered_map<std::string, VarValue> &params,
                             const std::string &lineId) const;
    bool compareEq(const VarValue &a, const VarValue &b) const;
    bool compareOrder(const std::string &op, const VarValue &a, const VarValue &b) const;
    std::string interpolate(const std::string &tpl,
                            const std::unordered_map<std::string, VarValue> &vars) const;
    Pool *findPool(const std::string &id);
    const Pool *findPool(const std::string &id) const;
    Line *findLine(const std::string &id);
    const Line *findLine(const std::string &id) const;
    void applyLineMeta(const Line &line);
    void pollSceneChange();
    uint32_t nextRandom();
    double nextUnit();

    std::unordered_map<std::string, VarValue> globalVars_;
    std::unordered_map<std::string, VarValue> sceneVars_;
    std::unordered_map<std::string, HSQOBJECT> predicates_;
    std::vector<Pool> pools_;
    std::string lastPoolsError_;
    uint32_t rngState_ = 0x9E3779B9u;
    std::string lastSceneName_;
    std::string currentLineId_;
    std::unordered_map<std::string, std::string> currentLineMeta_;
    std::vector<std::string> currentLineTags_;
    HSQUIRRELVM vm_ = nullptr;
};

}  // namespace eve::dialogue
