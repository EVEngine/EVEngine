#pragma once

#include "animation/AnimPose.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;

/**
 * @brief Clip-driven animation state machine with float/bool/trigger parameters
 * and cross-fade transitions. Script type: `AnimStateMachine`.
 */
class AnimStateMachine {
public:
    explicit AnimStateMachine(AnimSkeleton *skeleton);
    ~AnimStateMachine() = default;

    AnimStateMachine(const AnimStateMachine &)            = delete;
    AnimStateMachine &operator=(const AnimStateMachine &) = delete;

    AnimSkeleton *getSkeleton() const { return skeleton_; }

    void addState(const std::string &name, AnimClip *clip);
    void setEntry(const std::string &name);
    bool hasState(const std::string &name) const;
    std::string getCurrentState() const { return currentState_; }
    int getStateCount() const { return static_cast<int>(states_.size()); }

    /**
     * @brief Add transition from → to with blend duration.
     * @return transition id
     */
    int addTransition(const std::string &from, const std::string &to, float blendSeconds);

    /** @brief Comparison op: ">", ">=", "<", "<=", "==", "!=". */
    void addFloatCondition(int transitionId, const std::string &param, const std::string &op,
                           float threshold);
    void addBoolCondition(int transitionId, const std::string &param, bool value);
    void addTriggerCondition(int transitionId, const std::string &param);
    /** @brief Optional: require normalized local time in [0,1] >= exitTime before transition. */
    void setExitTime(int transitionId, float normalizedTime);
    void setHasExitTime(int transitionId, bool enabled);

    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;
    void setBool(const std::string &name, bool value);
    bool getBool(const std::string &name) const;
    void setTrigger(const std::string &name);
    void resetTrigger(const std::string &name);

    AnimPose *getPose();
    float getStateTime() const { return stateTime_; }
    bool isBlending() const { return blending_; }

    void update(float dt);

private:
    enum class CondKind { Float, Bool, Trigger };
    enum class FloatOp { Gt, Ge, Lt, Le, Eq, Ne };

    struct Condition {
        CondKind    kind = CondKind::Float;
        std::string param;
        FloatOp     op        = FloatOp::Gt;
        float       threshold = 0.f;
        bool        boolValue = true;
    };

    struct Transition {
        std::string           from;
        std::string           to;
        float                 blendSeconds = 0.2f;
        bool                  hasExitTime  = false;
        float                 exitTime     = 0.f;
        std::vector<Condition> conditions;
    };

    struct State {
        std::string name;
        AnimClip   *clip = nullptr;
    };

    void        requireState(const std::string &name) const;
    Transition &requireTransition(int id);
    static FloatOp parseFloatOp(const std::string &op);
    bool           conditionsMet(const Transition &tr) const;
    bool           tryTransition();

    AnimSkeleton *skeleton_ = nullptr;
    AnimPose      pose_;
    AnimPose      fromPose_;
    AnimPose      toPose_;

    std::unordered_map<std::string, State> states_;
    std::vector<std::string>               stateOrder_;
    std::vector<Transition>                transitions_;

    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, bool>  bools_;
    std::unordered_map<std::string, bool>  triggers_;

    std::string currentState_;
    std::string nextState_;
    float       stateTime_     = 0.f;
    float       blendDuration_ = 0.f;
    float       blendElapsed_  = 0.f;
    bool        blending_      = false;
    bool        started_       = false;
};

}  // namespace eve::animation
