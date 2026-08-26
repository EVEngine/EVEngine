#pragma once

#include "common/Module.h"
#include "common/StateValue.h"

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
 * @brief Generic JSON-like value tree used by the Squirrel bridge: dialogue pools and
 * conditions arrive as Squirrel tables and are converted into this structure
 * so the core loader/evaluator stays script-agnostic and unit-testable.
 */
struct DataValue {
    struct Member;
    using Object = std::vector<Member>;

    enum class Kind { Null, Int, Float, Bool, String, Array, Object };

    Kind kind = Kind::Null;
    long long i = 0;
    double f = 0.0;
    bool b = false;
    std::string s;
    std::vector<DataValue> arr;
    Object obj;

    DataValue();
    DataValue(const DataValue &);
    DataValue(DataValue &&) noexcept;
    DataValue &operator=(const DataValue &);
    DataValue &operator=(DataValue &&) noexcept;
    ~DataValue();

    static DataValue null();
    static DataValue integer(long long v);
    static DataValue number(double v);
    static DataValue boolean(bool v);
    static DataValue string(std::string v);
    static DataValue array(std::vector<DataValue> v);
    static DataValue object(Object v);

    const DataValue *find(const std::string &key) const;
};

/** @brief One ordered key/value entry in a DataValue object. */
struct DataValue::Member {
    std::string first;
    DataValue second;

    Member(std::string key, DataValue value)
        : first(std::move(key)), second(std::move(value)) {}
};

inline DataValue::DataValue() = default;
inline DataValue::DataValue(const DataValue &) = default;
inline DataValue::DataValue(DataValue &&) noexcept = default;
inline DataValue &DataValue::operator=(const DataValue &) = default;
inline DataValue &DataValue::operator=(DataValue &&) noexcept = default;
inline DataValue::~DataValue() = default;

inline DataValue DataValue::null() { return {}; }

inline DataValue DataValue::integer(long long v) {
    DataValue d;
    d.kind = Kind::Int;
    d.i = v;
    return d;
}

inline DataValue DataValue::number(double v) {
    DataValue d;
    d.kind = Kind::Float;
    d.f = v;
    return d;
}

inline DataValue DataValue::boolean(bool v) {
    DataValue d;
    d.kind = Kind::Bool;
    d.b = v;
    return d;
}

inline DataValue DataValue::string(std::string v) {
    DataValue d;
    d.kind = Kind::String;
    d.s = std::move(v);
    return d;
}

inline DataValue DataValue::array(std::vector<DataValue> v) {
    DataValue d;
    d.kind = Kind::Array;
    d.arr = std::move(v);
    return d;
}

inline DataValue DataValue::object(Object v) {
    DataValue d;
    d.kind = Kind::Object;
    d.obj = std::move(v);
    return d;
}

inline const DataValue *DataValue::find(const std::string &key) const {
    for (const auto &kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

/**
 * @brief Visual-novel style dialogue stage.
 * Script: `dlg <- eve.Dialogue();`
 *
 * Dialogue scripts remain Squirrel (functions / generators). This module only
 * owns speaker lines, typewriter, choices, and avatar stage slots.
 */
class Dialogue : public Module {
public:
    Module_REG(Dialogue);
    Dialogue();
    ~Dialogue() override;

    /** @brief 角色：注册 / 查询 / 绑定 Avatar。 */
    bool registerCharacter(const std::string &id, const std::string &displayName);
    bool hasCharacter(const std::string &id) const;
    std::string getDisplayName(const std::string &id) const;
    bool bindAvatar(const std::string &id, avatar::AvatarInstance *av);
    avatar::AvatarInstance *getAvatar(const std::string &id) const;
    int getCharacterCount() const;
    std::string getCharacterId(int index) const;

    /** @brief 舞台：显示/隐藏角色、槽位与表情/动作。 */
    bool show(const std::string &id, const std::string &slot);
    bool hide(const std::string &id);
    bool isShown(const std::string &id) const;
    std::string getSlot(const std::string &id) const;
    void setSlotX(const std::string &slot, float xNorm);
    float getSlotX(const std::string &slot) const;
    bool setExpression(const std::string &id, const std::string &expression);
    bool setMotion(const std::string &id, const std::string &motion);
    /** Place visible avatars using normalized slot X * stageWidth. */
    void syncStage(float stageWidth, float stageHeight);

    /** @brief 台词：说话/旁白、打字机效果与推进。 */
    void say(const std::string &speakerId, const std::string &text);
    void narrate(const std::string &text);
    void setTypeSpeed(float charsPerSecond);
    float getTypeSpeed() const { return typeSpeed_; }
    void skipTyping();
    bool isTyping() const;
    bool isWaitingAdvance() const;
    bool isIdle() const;
    void advance();

    std::string getSpeakerId() const { return speakerId_; }
    std::string getSpeakerName() const;
    std::string getFullText() const { return fullText_; }
    std::string getVisibleText() const;
    std::string getPhase() const;

    /** @brief 口型同步：打字时驱动说话者 Avatar 参数。 */
    void setLipSyncEnabled(bool enabled);
    bool isLipSyncEnabled() const { return lipSyncEnabled_; }
    void setLipSyncParameter(const std::string &name);
    std::string getLipSyncParameter() const { return lipSyncParameter_; }
    void setLipSyncAmplitude(float amplitude);
    float getLipSyncAmplitude() const { return lipSyncAmplitude_; }
    float getLipSyncValue() const { return lipSyncValue_; }

    /** @brief 选项：清空/添加/展示与选择。 */
    void clearChoices();
    bool addChoice(const std::string &id, const std::string &label);
    void presentChoices();
    bool isWaitingChoice() const;
    int getChoiceCount() const;
    std::string getChoiceId(int index) const;
    std::string getChoiceLabel(int index) const;
    bool selectChoice(int index);
    std::string getSelectedChoiceId() const { return selectedChoiceId_; }

    // ---- variables (global / scene; scene auto-cleared on scene switch) ----
    struct VarValue {
        enum class Type { Int, Float, Bool, String };

        Type type = Type::String;
        long long i = 0;
        double f = 0.0;
        bool b = false;
        std::string s;

        std::string toString() const;
        std::string typeName() const;
        bool isNumeric() const { return type == Type::Int || type == Type::Float; }

        static VarValue integer(long long v);
        static VarValue number(double v);
        static VarValue boolean(bool v);
        static VarValue string(std::string v);
    };

    /** Squirrel-facing setter: value may be int/float/bool/string. */
    bool setVar(const std::string &name, ssq::Object value, const std::string &scope);
    /** C++ core setter (also used by unit tests). */
    bool setVarValue(const std::string &name, const VarValue &value, const std::string &scope);
    VarValue getVarValue(const std::string &name, const std::string &scope) const;
    std::string getVarType(const std::string &name, const std::string &scope) const;
    int getVarInt(const std::string &name, int defaultValue, const std::string &scope) const;
    float getVarFloat(const std::string &name, float defaultValue, const std::string &scope) const;
    bool getVarBool(const std::string &name, bool defaultValue, const std::string &scope) const;
    std::string getVarString(const std::string &name, const std::string &defaultValue,
                             const std::string &scope) const;
    bool hasVar(const std::string &name, const std::string &scope) const;
    bool clearVar(const std::string &name, const std::string &scope);
    void clearVars(const std::string &scope);

    // ---- conditions ----
    /** Register a Squirrel predicate: fn(ctx) -> bool, ctx = {vars, params, lineId}. */
    bool registerCondition(const std::string &name, ssq::Object fn);
    bool unregisterCondition(const std::string &name);
    bool evalCondition(ssq::Object table);
    bool evalConditionData(const DataValue &cond);

    // ---- content pools ----
    int loadPoolsFromTable(ssq::Object table);
    int loadPoolsFromData(const DataValue &root);
    /** 解析 .dnut 源码并注册台词池；path 用于错误信息（可传 ""）。 */
    int loadPoolsFromDnut(const std::string &source, const std::string &path);
    /** 读取 .dnut 文件并解析注册（经 eve.Filesystem）。 */
    int loadPoolsFromDnutFile(const std::string &path);
    void clearPools();
    int getPoolCount() const;
    std::string getPoolId(int index) const;
    bool hasPool(const std::string &id) const;
    std::string getLastPoolsError() const { return lastPoolsError_; }

    // ---- rng (weighted selection determinism) ----
    /** Reseed the weighted picker; also resets per-pool no-repeat history. */
    void setRandomSeed(int seed);
    int getRandomSeed() const { return int(rngState_); }

    // ---- generated-line selection / play ----
    std::string pickLine(const std::string &poolId, ssq::Object params);
    std::string pickLineWithParams(const std::string &poolId,
                                   const std::unordered_map<std::string, VarValue> &params);
    bool playLine(const std::string &lineId, ssq::Object params);
    bool playLineWithParams(const std::string &lineId,
                            const std::unordered_map<std::string, VarValue> &params);
    bool playPool(const std::string &poolId, ssq::Object params);
    bool playPoolWithParams(const std::string &poolId,
                            const std::unordered_map<std::string, VarValue> &params);
    std::string getCurrentLineId() const { return currentLineId_; }
    std::string getCurrentLineMeta(const std::string &field) const;
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
