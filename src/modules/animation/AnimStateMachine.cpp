#include "animation/AnimStateMachine.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Capability.h"
#include "common/Exception.h"
#include "common/IStateProvider.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::animation {
namespace {

std::vector<AnimStateMachine*>& machines() {
    static std::vector<AnimStateMachine*> instances;
    return instances;
}

double stateNumber(const StateValue& v) { return v.isInt() ? static_cast<double>(v.asInt()) : v.asDouble(); }

/** @brief IStateProvider over every live AnimStateMachine (module registry). */
class AnimStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "anim"; }

    bool captureState(StateValue& out) override {
        StateValue arr = StateValue::array();
        for (AnimStateMachine* m : machines()) {
            StateValue sv;
            if (m->captureState(sv)) arr.pushBack(std::move(sv));
        }
        out = std::move(arr);
        return true;
    }

    bool restoreState(const StateValue& in, std::string* err) override {
        if (!in.isArray()) {
            if (err) *err = "anim: expected array of state machines";
            return false;
        }
        const size_t n = std::min(in.arraySize(), machines().size());
        for (size_t i = 0; i < n; ++i) {
            if (!machines()[i]->restoreState(in.at(i), err)) return false;
        }
        return true;
    }

    bool resetToDefaults() override {
        for (AnimStateMachine* m : machines()) m->resetToDefaults();
        return true;
    }
};

struct Register {
    Register() {
        static AnimStateProvider provider;
        eve::cap::addListener<eve::caps::IStateProvider>(&provider);
    }
} g_register;

}  // namespace

AnimStateMachine::AnimStateMachine(AnimSkeleton *skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimStateMachine: skeleton is null");
    pose_.resize(skeleton_->getBoneCount());
    fromPose_.resize(skeleton_->getBoneCount());
    toPose_.resize(skeleton_->getBoneCount());
    skeleton_->applyBindPose(&pose_);
    machines().push_back(this);
}

AnimStateMachine::~AnimStateMachine() {
    machines().erase(std::remove(machines().begin(), machines().end(), this), machines().end());
}

void AnimStateMachine::requireState(const std::string &name) const {
    if (states_.find(name) == states_.end()) {
        throw Exception("AnimStateMachine: unknown state '%s'", name.c_str());
    }
}

AnimStateMachine::Transition &AnimStateMachine::requireTransition(int id) {
    if (id < 0 || id >= static_cast<int>(transitions_.size())) {
        throw Exception("AnimStateMachine: invalid transition id %d", id);
    }
    return transitions_[static_cast<size_t>(id)];
}

AnimStateMachine::FloatOp AnimStateMachine::parseFloatOp(const std::string &op) {
    if (op == ">") return FloatOp::Gt;
    if (op == ">=") return FloatOp::Ge;
    if (op == "<") return FloatOp::Lt;
    if (op == "<=") return FloatOp::Le;
    if (op == "==") return FloatOp::Eq;
    if (op == "!=") return FloatOp::Ne;
    throw Exception("AnimStateMachine: unknown float op '%s'", op.c_str());
}

void AnimStateMachine::addState(const std::string &name, AnimClip *clip) {
    if (name.empty()) throw Exception("AnimStateMachine.addState: name empty");
    if (!clip) throw Exception("AnimStateMachine.addState: clip is null");
    if (states_.count(name)) {
        throw Exception("AnimStateMachine.addState: duplicate state '%s'", name.c_str());
    }
    states_[name] = State{name, clip};
    stateOrder_.push_back(name);
}

void AnimStateMachine::setEntry(const std::string &name) {
    requireState(name);
    currentState_ = name;
    stateTime_    = 0.f;
    blending_     = false;
    started_      = true;
    states_[name].clip->sample(0.f, &pose_, skeleton_);
}

bool AnimStateMachine::hasState(const std::string &name) const {
    return states_.count(name) > 0;
}

int AnimStateMachine::addTransition(const std::string &from, const std::string &to,
                                    float blendSeconds) {
    requireState(from);
    requireState(to);
    if (blendSeconds < 0.f) {
        throw Exception("AnimStateMachine.addTransition: blendSeconds must be >= 0");
    }
    Transition tr;
    tr.from         = from;
    tr.to           = to;
    tr.blendSeconds = blendSeconds;
    transitions_.push_back(tr);
    return static_cast<int>(transitions_.size()) - 1;
}

void AnimStateMachine::addFloatCondition(int transitionId, const std::string &param,
                                         const std::string &op, float threshold) {
    if (param.empty()) throw Exception("AnimStateMachine.addFloatCondition: param empty");
    Transition &tr = requireTransition(transitionId);
    Condition c;
    c.kind      = CondKind::Float;
    c.param     = param;
    c.op        = parseFloatOp(op);
    c.threshold = threshold;
    tr.conditions.push_back(c);
    floats_.emplace(param, 0.f);
}

void AnimStateMachine::addBoolCondition(int transitionId, const std::string &param, bool value) {
    if (param.empty()) throw Exception("AnimStateMachine.addBoolCondition: param empty");
    Transition &tr = requireTransition(transitionId);
    Condition c;
    c.kind      = CondKind::Bool;
    c.param     = param;
    c.boolValue = value;
    tr.conditions.push_back(c);
    bools_.emplace(param, false);
}

void AnimStateMachine::addTriggerCondition(int transitionId, const std::string &param) {
    if (param.empty()) throw Exception("AnimStateMachine.addTriggerCondition: param empty");
    Transition &tr = requireTransition(transitionId);
    Condition c;
    c.kind  = CondKind::Trigger;
    c.param = param;
    tr.conditions.push_back(c);
    triggers_.emplace(param, false);
}

void AnimStateMachine::setExitTime(int transitionId, float normalizedTime) {
    Transition &tr = requireTransition(transitionId);
    tr.exitTime    = clampf(normalizedTime, 0.f, 1.f);
    tr.hasExitTime = true;
}

void AnimStateMachine::setHasExitTime(int transitionId, bool enabled) {
    requireTransition(transitionId).hasExitTime = enabled;
}

void AnimStateMachine::setFloat(const std::string &name, float value) { floats_[name] = value; }

float AnimStateMachine::getFloat(const std::string &name) const {
    auto it = floats_.find(name);
    return it == floats_.end() ? 0.f : it->second;
}

void AnimStateMachine::setBool(const std::string &name, bool value) { bools_[name] = value; }

bool AnimStateMachine::getBool(const std::string &name) const {
    auto it = bools_.find(name);
    return it == bools_.end() ? false : it->second;
}

void AnimStateMachine::setTrigger(const std::string &name) { triggers_[name] = true; }

void AnimStateMachine::resetTrigger(const std::string &name) { triggers_[name] = false; }

AnimPose *AnimStateMachine::getPose() { return &pose_; }

bool AnimStateMachine::conditionsMet(const Transition &tr) const {
    for (const Condition &c : tr.conditions) {
        switch (c.kind) {
            case CondKind::Float: {
                const float v = getFloat(c.param);
                bool ok       = false;
                switch (c.op) {
                    case FloatOp::Gt: ok = v > c.threshold; break;
                    case FloatOp::Ge: ok = v >= c.threshold; break;
                    case FloatOp::Lt: ok = v < c.threshold; break;
                    case FloatOp::Le: ok = v <= c.threshold; break;
                    case FloatOp::Eq: ok = std::fabs(v - c.threshold) < 1e-5f; break;
                    case FloatOp::Ne: ok = std::fabs(v - c.threshold) >= 1e-5f; break;
                }
                if (!ok) return false;
                break;
            }
            case CondKind::Bool: {
                if (getBool(c.param) != c.boolValue) return false;
                break;
            }
            case CondKind::Trigger: {
                auto it = triggers_.find(c.param);
                if (it == triggers_.end() || !it->second) return false;
                break;
            }
        }
    }
    return true;
}

bool AnimStateMachine::tryTransition() {
    if (currentState_.empty() || blending_) return false;
    const State &cur = states_.at(currentState_);
    const float dur  = cur.clip ? cur.clip->getDuration() : 0.f;
    const float norm = dur > 1e-8f ? clampf(stateTime_ / dur, 0.f, 1.f) : 1.f;

    for (const Transition &tr : transitions_) {
        if (tr.from != currentState_) continue;
        if (tr.hasExitTime && norm < tr.exitTime) continue;
        if (!conditionsMet(tr)) continue;

        // Consume triggers used by this transition.
        for (const Condition &c : tr.conditions) {
            if (c.kind == CondKind::Trigger) triggers_[c.param] = false;
        }

        nextState_     = tr.to;
        blendDuration_ = tr.blendSeconds;
        blendElapsed_  = 0.f;
        stateTime_     = 0.f;
        if (blendDuration_ <= 1e-6f) {
            currentState_ = nextState_;
            blending_     = false;
            states_.at(currentState_).clip->sample(0.f, &pose_, skeleton_);
        } else {
            // Switch logical state immediately; pose cross-fades from previous.
            fromPose_.copyFrom(&pose_);
            currentState_ = nextState_;
            blending_     = true;
            states_.at(currentState_).clip->sample(0.f, &toPose_, skeleton_);
            pose_.blendFrom(&fromPose_, &toPose_, 0.f);
        }
        return true;
    }
    return false;
}

void AnimStateMachine::update(float dt) {
    if (dt < 0.f) throw Exception("AnimStateMachine.update: dt must be >= 0");
    if (!started_) {
        if (!stateOrder_.empty()) setEntry(stateOrder_.front());
        else return;
    }
    if (currentState_.empty()) return;

    if (blending_) {
        blendElapsed_ += dt;
        stateTime_ += dt;
        const State &dst = states_.at(currentState_);
        dst.clip->sample(stateTime_, &toPose_, skeleton_);
        float t = blendDuration_ > 1e-8f ? blendElapsed_ / blendDuration_ : 1.f;
        if (t >= 1.f) {
            blending_ = false;
            pose_.copyFrom(&toPose_);
        } else {
            // Keep fromPose frozen at transition start for stable crossfade.
            pose_.blendFrom(&fromPose_, &toPose_, t);
        }
        // Allow chained transitions after blend completes next frame.
        if (!blending_) tryTransition();
        return;
    }

    stateTime_ += dt;
    const State &st = states_.at(currentState_);
    st.clip->sample(stateTime_, &pose_, skeleton_);
    tryTransition();
}

bool AnimStateMachine::captureState(StateValue& out) const {
    out = StateValue::object();
    out.set("currentState", StateValue::string(currentState_));
    out.set("nextState", StateValue::string(nextState_));
    out.set("stateTime", StateValue::number(stateTime_));
    out.set("blendDuration", StateValue::number(blendDuration_));
    out.set("blendElapsed", StateValue::number(blendElapsed_));
    out.set("blending", StateValue::boolean(blending_));
    out.set("started", StateValue::boolean(started_));

    StateValue floats = StateValue::object();
    for (const auto& kv : floats_) floats.set(kv.first, StateValue::number(kv.second));
    out.set("floats", std::move(floats));

    StateValue bools = StateValue::object();
    for (const auto& kv : bools_) bools.set(kv.first, StateValue::boolean(kv.second));
    out.set("bools", std::move(bools));

    StateValue triggers = StateValue::object();
    for (const auto& kv : triggers_) triggers.set(kv.first, StateValue::boolean(kv.second));
    out.set("triggers", std::move(triggers));
    return true;
}

bool AnimStateMachine::restoreState(const StateValue& in, std::string* err) {
    if (!in.isObject()) {
        if (err) *err = "anim: state is not an object";
        return false;
    }
    const StateValue* current = in.find("currentState");
    const StateValue* started = in.find("started");
    if (!current || !current->isString() || !started || !started->isBool()) {
        if (err) *err = "anim: missing currentState/started";
        return false;
    }
    const std::string stateName  = current->asString();
    const bool        wasStarted = started->asBool();
    if (wasStarted && !stateName.empty() && !hasState(stateName)) {
        if (err) *err = "anim: unknown state '" + stateName + "'";
        return false;
    }

    floats_.clear();
    bools_.clear();
    triggers_.clear();

    if (const StateValue* floats = in.find("floats"); floats && floats->isObject()) {
        for (const auto& key : floats->keys()) {
            const StateValue* v = floats->find(key);
            if (v && (v->isInt() || v->isFloat())) floats_[key] = static_cast<float>(stateNumber(*v));
        }
    }
    if (const StateValue* bools = in.find("bools"); bools && bools->isObject()) {
        for (const auto& key : bools->keys()) {
            const StateValue* v = bools->find(key);
            if (v && v->isBool()) bools_[key] = v->asBool();
        }
    }
    if (const StateValue* triggers = in.find("triggers"); triggers && triggers->isObject()) {
        for (const auto& key : triggers->keys()) {
            const StateValue* v = triggers->find(key);
            if (v && v->isBool()) triggers_[key] = v->asBool();
        }
    }

    currentState_ = stateName;
    if (const StateValue* ns = in.find("nextState"); ns && ns->isString()) nextState_ = ns->asString();
    if (const StateValue* st = in.find("stateTime"); st && (st->isInt() || st->isFloat()))
        stateTime_ = static_cast<float>(stateNumber(*st));
    if (const StateValue* bd = in.find("blendDuration"); bd && (bd->isInt() || bd->isFloat()))
        blendDuration_ = static_cast<float>(stateNumber(*bd));
    if (const StateValue* be = in.find("blendElapsed"); be && (be->isInt() || be->isFloat()))
        blendElapsed_ = static_cast<float>(stateNumber(*be));
    if (const StateValue* bl = in.find("blending"); bl && bl->isBool()) blending_ = bl->asBool();
    started_ = wasStarted;

    if (started_ && !currentState_.empty()) {
        states_.at(currentState_).clip->sample(stateTime_, &pose_, skeleton_);
    }
    return true;
}

bool AnimStateMachine::resetToDefaults() {
    floats_.clear();
    bools_.clear();
    triggers_.clear();
    blending_ = false;
    nextState_.clear();
    stateTime_     = 0.f;
    blendDuration_ = 0.f;
    blendElapsed_  = 0.f;
    if (!stateOrder_.empty()) {
        setEntry(stateOrder_.front());
        return true;
    }
    started_ = false;
    currentState_.clear();
    return true;
}

}  // namespace eve::animation
