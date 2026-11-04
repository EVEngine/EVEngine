#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRigInternal.h"

namespace eve::animation {
using namespace rig_detail;

struct ControlRig::Impl {
    struct Node {
        RigOperation operation;
        int          control, bone, middle, tip;
        float        weight;
    };
    RigDefinition     definition;
    AnimSkeleton      skeleton;
    std::vector<int>  order, parents;
    std::vector<Node> nodes;
    std::vector<int>  jointControls;
};

ControlRig::ControlRig(std::unique_ptr<Impl> implementation) : impl_(std::move(implementation)) {}
ControlRig::~ControlRig() = default;

Result<void> validateRigDefinition(const RigDefinition& d) {
    if (!std::isfinite(d.duration) || d.duration <= 0 || d.duration > 36000 || d.controls.size() > 4096 ||
        d.nodes.size() > 4096)
        return error<void>("invalid duration or rig capacity");
    std::set<std::string> names;
    for (const auto& c : d.controls) {
        if (c.id.empty() || !names.insert(c.id).second || !validTRS(c.initial) || !std::isfinite(c.shapeSize) ||
            c.shapeSize <= 0 || c.keys.size() > 100000)
            return error<void>("invalid/duplicate control or transform: " + c.id);
        auto constrained = constrainRigControl(c.limits, c.initial);
        if (!constrained.ok()) return Result<void>::failure(constrained.status());
        double previous = -1;
        for (const auto& k : c.keys) {
            if (!std::isfinite(k.time) || k.time < 0 || k.time > d.duration || k.time <= previous ||
                !validTRS(k.transform))
                return error<void>("invalid or unordered key: " + c.id);
            previous = k.time;
        }
    }
    auto order = controlOrder(d);
    if (!order.ok()) return Result<void>::failure(order.status());
    names.clear();
    for (const auto& n : d.nodes) {
        if (n.id.empty() || !names.insert(n.id).second || controlIndex(d, n.control) < 0 || n.bone.empty() ||
            !std::isfinite(n.weight) || n.weight < 0 || n.weight > 1 ||
            (n.operation != RigOperation::FK && n.operation != RigOperation::Aim &&
             n.operation != RigOperation::TwoBoneIK))
            return error<void>("invalid solve node: " + n.id);
        if (n.operation == RigOperation::TwoBoneIK && (n.middle.empty() || n.tip.empty()))
            return error<void>("IK requires middle and tip bones: " + n.id);
        std::set<std::string> dependencies;
        for (const auto& dependency : n.after)
            if (!dependencies.insert(dependency).second) return error<void>("duplicate solve dependency: " + n.id);
    }
    std::set<std::string> ready;
    while (ready.size() < d.nodes.size()) {
        bool progress = false;
        for (const auto& n : d.nodes) {
            if (ready.contains(n.id)) continue;
            bool available = true;
            for (const auto& dependency : n.after) {
                if (!names.contains(dependency)) return error<void>("missing solve dependency: " + dependency);
                if (!ready.contains(dependency)) available = false;
            }
            if (available) {
                ready.insert(n.id);
                progress = true;
            }
        }
        if (!progress) return error<void>("solve graph cycle");
    }
    return Result<void>::success();
}

Result<std::unique_ptr<ControlRig>> ControlRig::compile(const RigDefinition& d, const AnimSkeleton& skeleton) {
    auto valid = validateRigDefinition(d);
    if (!valid.ok()) return Result<std::unique_ptr<ControlRig>>::failure(valid.status());
    if (skeleton.getBoneCount() <= 0) return error<std::unique_ptr<ControlRig>>("empty skeleton");
    auto p        = std::make_unique<Impl>();
    p->definition = d;
    for (auto& c : p->definition.controls) {
        auto initial = constrainRigControl(c.limits, c.initial);
        if (!initial.ok()) return Result<std::unique_ptr<ControlRig>>::failure(initial.status());
        c.initial = initial.value();
        for (auto& key : c.keys) {
            auto projected = constrainRigControl(c.limits, key.transform);
            if (!projected.ok()) return Result<std::unique_ptr<ControlRig>>::failure(projected.status());
            key.transform = projected.value();
        }
    }
    std::set<std::string> boneNames;
    for (int i = 0; i < skeleton.getBoneCount(); ++i) {
        const auto  name   = skeleton.getBoneName(i);
        const int   parent = skeleton.getParent(i);
        const auto& t      = skeleton.bindLocal(i);
        if (!boneNames.insert(name).second || parent >= i || parent < -1 || !validTRS(t))
            return error<std::unique_ptr<ControlRig>>(
                "skeleton requires unique names, ordered parents and uniform positive TRS");
        const int index = p->skeleton.addBone(name, parent);
        p->skeleton.setBindPosition(index, t.px, t.py, t.pz);
        p->skeleton.setBindRotation(index, t.qx, t.qy, t.qz, t.qw);
        p->skeleton.setBindScale(index, t.sx, t.sy, t.sz);
    }
    auto order = controlOrder(d);
    if (!order.ok()) return Result<std::unique_ptr<ControlRig>>::failure(order.status());
    p->order = std::move(order).takeValue();
    for (const auto& c : d.controls) p->parents.push_back(controlIndex(d, c.parent));
    p->jointControls.assign(static_cast<size_t>(skeleton.getBoneCount()), -1);
    for (const auto& n : d.nodes) {
        if (n.operation != RigOperation::FK) continue;
        const int ci = controlIndex(d, n.control), bone = skeleton.findBone(n.bone);
        if (bone < 0) continue;
        const auto& c      = d.controls[static_cast<size_t>(ci)];
        const auto& l      = c.limits;
        const bool  active = l.extension || l.scale.mode != RigAxisMode::Free ||
                            std::any_of(l.translation.begin(), l.translation.end(),
                                        [](const auto& a) { return a.mode != RigAxisMode::Free; }) ||
                            std::any_of(l.rotation.begin(), l.rotation.end(),
                                        [](const auto& a) { return a.mode != RigAxisMode::Free; });
        if (!active) continue;
        const int parent  = skeleton.getParent(bone);
        const int pc      = controlIndex(d, c.parent);
        bool      matched = parent < 0 && pc < 0;
        for (const auto& pn : d.nodes)
            if (parent >= 0 && pn.operation == RigOperation::FK && pn.bone == skeleton.getBoneName(parent) &&
                pn.control == c.parent && pn.weight == 1)
                matched = true;
        const auto bindings = std::count_if(d.nodes.begin(), d.nodes.end(), [&](const auto& other) {
            return other.operation == RigOperation::FK && (other.bone == n.bone || other.control == n.control);
        });
        if (!matched || n.weight != 1 || bindings != 1 || p->jointControls[static_cast<size_t>(bone)] >= 0)
            return error<std::unique_ptr<ControlRig>>(
                "limited joints need unique full-weight FK and matching parent hierarchy");
        p->jointControls[static_cast<size_t>(bone)] = ci;
    }
    std::set<std::string> ready;
    while (ready.size() < d.nodes.size()) {
        for (const auto& n : d.nodes) {
            if (ready.contains(n.id)) continue;
            if (!std::all_of(n.after.begin(), n.after.end(), [&](const auto& id) { return ready.contains(id); }))
                continue;
            const int bone = skeleton.findBone(n.bone), middle = skeleton.findBone(n.middle),
                      tip = skeleton.findBone(n.tip);
            if (bone < 0) return error<std::unique_ptr<ControlRig>>("missing bone: " + n.bone);
            if (n.operation == RigOperation::TwoBoneIK &&
                (middle < 0 || tip < 0 || skeleton.getParent(middle) != bone || skeleton.getParent(tip) != middle))
                return error<std::unique_ptr<ControlRig>>("IK requires direct root-middle-tip chain: " + n.id);
            p->nodes.push_back({n.operation, controlIndex(d, n.control), bone, middle, tip, n.weight});
            ready.insert(n.id);
        }
    }
    return Result<std::unique_ptr<ControlRig>>::success(std::unique_ptr<ControlRig>(new ControlRig(std::move(p))));
}

Result<std::vector<TransformTRS>> ControlRig::sampleControls(double seconds) const {
    if (!std::isfinite(seconds)) return error<std::vector<TransformTRS>>("nonfinite sample time");
    seconds = std::clamp(seconds, 0.0, impl_->definition.duration);
    std::vector<TransformTRS> result;
    for (const auto& c : impl_->definition.controls) {
        if (c.keys.empty()) {
            result.push_back(c.initial);
        } else {
            const auto upper = std::upper_bound(c.keys.begin(), c.keys.end(), seconds,
                                                [](double t, const RigKey& k) { return t < k.time; });
            if (upper == c.keys.begin())
                result.push_back(upper->transform);
            else if (upper == c.keys.end())
                result.push_back(c.keys.back().transform);
            else {
                const auto& a = *(upper - 1);
                result.push_back(blendTRS(a.transform, upper->transform,
                                          static_cast<float>((seconds - a.time) / (upper->time - a.time))));
            }
        }
        auto projected = constrainRigControl(c.limits, result.back());
        if (!projected.ok()) return Result<std::vector<TransformTRS>>::failure(projected.status());
        result.back() = projected.value();
    }
    return Result<std::vector<TransformTRS>>::success(std::move(result));
}

Result<std::vector<TransformTRS>> ControlRig::controlWorlds(const std::vector<TransformTRS>& locals) const {
    if (locals.size() != impl_->parents.size()) return error<std::vector<TransformTRS>>("control count mismatch");
    for (const auto& t : locals)
        if (!validTRS(t)) return error<std::vector<TransformTRS>>("invalid control transform");
    auto worlds      = locals;
    auto constrained = locals;
    for (size_t i = 0; i < locals.size(); ++i) {
        auto projected = constrainRigControl(impl_->definition.controls[i].limits, locals[i]);
        if (!projected.ok()) return Result<std::vector<TransformTRS>>::failure(projected.status());
        worlds[i] = constrained[i] = projected.value();
    }
    for (int i : impl_->order) {
        const int parent = impl_->parents[static_cast<size_t>(i)];
        if (parent >= 0)
            worlds[static_cast<size_t>(i)] =
                detail::mulTRS(worlds[static_cast<size_t>(parent)], constrained[static_cast<size_t>(i)]);
        if (!validTRS(worlds[static_cast<size_t>(i)]))
            return error<std::vector<TransformTRS>>("control hierarchy overflow");
    }
    return Result<std::vector<TransformTRS>>::success(std::move(worlds));
}

Result<void> ControlRig::evaluate(double seconds, const AnimPose& input, AnimPose& output,
                                  const std::vector<TransformTRS>& overrides) const {
    if (!std::isfinite(seconds) || input.getBoneCount() != impl_->skeleton.getBoneCount())
        return error<void>("pose/time mismatch");
    for (int i = 0; i < input.getBoneCount(); ++i)
        if (!validTRS(input.local(i))) return error<void>("invalid input bone TRS");
    auto sampled = overrides.empty() ? sampleControls(seconds) : Result<std::vector<TransformTRS>>::success(overrides);
    if (!sampled.ok()) return Result<void>::failure(sampled.status());
    auto worlds = controlWorlds(sampled.value());
    if (!worlds.ok()) return Result<void>::failure(worlds.status());
    AnimPose candidate;
    candidate.copyFrom(&input);
    candidate.computeWorld(&impl_->skeleton);
    for (int i = 0; i < candidate.getBoneCount(); ++i)
        if (!validTRS(candidate.world(i))) return error<void>("input hierarchy overflow");
    for (const auto& n : impl_->nodes) {
        if (n.weight == 0) continue;
        const auto& target = worlds.value()[static_cast<size_t>(n.control)];
        if (n.operation == RigOperation::FK) {
            const int parent        = impl_->skeleton.getParent(n.bone);
            auto      local         = parent < 0 ? target : relative(candidate.world(parent), target);
            candidate.local(n.bone) = blendTRS(candidate.local(n.bone), local, n.weight);
        } else if (n.operation == RigOperation::Aim) {
            if (!candidate.aimBone(&impl_->skeleton, n.bone, target.px, target.py, target.pz, n.weight))
                return error<void>("aim target coincides with bone");
        } else if (!candidate.solveTwoBoneIK(&impl_->skeleton, n.bone, n.middle, n.tip, target.px, target.py, target.pz,
                                             n.weight)) {
            return error<void>("degenerate IK chain");
        }
        for (int bone = 0; bone < candidate.getBoneCount(); ++bone) {
            const int ci = impl_->jointControls[static_cast<size_t>(bone)];
            if (ci < 0) continue;
            auto projected =
                constrainRigControl(impl_->definition.controls[static_cast<size_t>(ci)].limits, candidate.local(bone));
            if (!projected.ok()) return Result<void>::failure(projected.status());
            candidate.local(bone) = projected.value();
        }
        candidate.computeWorld(&impl_->skeleton);
    }
    for (int i = 0; i < candidate.getBoneCount(); ++i)
        if (!validTRS(candidate.local(i)) || !validTRS(candidate.world(i)))
            return error<void>("solve produced invalid transform");
    output = std::move(candidate);
    return Result<void>::success();
}

Result<TransformTRS> ControlRig::localFromWorld(int index, const TransformTRS& world, double seconds) const {
    if (index < 0 || index >= static_cast<int>(impl_->parents.size()) || !validTRS(world))
        return error<TransformTRS>("invalid world edit");
    auto sampled = sampleControls(seconds);
    if (!sampled.ok()) return Result<TransformTRS>::failure(sampled.status());
    auto worlds = controlWorlds(sampled.value());
    if (!worlds.ok()) return Result<TransformTRS>::failure(worlds.status());
    const int parent = impl_->parents[static_cast<size_t>(index)];
    return constrainRigControl(impl_->definition.controls[static_cast<size_t>(index)].limits,
                               parent < 0 ? world : relative(worlds.value()[static_cast<size_t>(parent)], world));
}

Result<std::vector<TransformTRS>> ControlRig::matchFK(const AnimPose& pose) const {
    if (pose.getBoneCount() != impl_->skeleton.getBoneCount())
        return error<std::vector<TransformTRS>>("pose count mismatch");
    for (int i = 0; i < pose.getBoneCount(); ++i)
        if (!validTRS(pose.local(i))) return error<std::vector<TransformTRS>>("invalid pose");
    auto sampled = sampleControls(0);
    if (!sampled.ok()) return sampled;
    auto worlds = controlWorlds(sampled.value());
    if (!worlds.ok()) return worlds;
    AnimPose copy;
    copy.copyFrom(&pose);
    copy.computeWorld(&impl_->skeleton);
    for (int i = 0; i < copy.getBoneCount(); ++i)
        if (!validTRS(copy.world(i))) return error<std::vector<TransformTRS>>("input hierarchy overflow");
    std::set<int> controls, bones;
    for (const auto& n : impl_->nodes) {
        if (n.operation != RigOperation::FK || n.weight != 1 || !controls.insert(n.control).second ||
            !bones.insert(n.bone).second)
            return error<std::vector<TransformTRS>>("backward matching requires unique full-weight FK bindings");
        worlds.value()[static_cast<size_t>(n.control)] = copy.world(n.bone);
    }
    auto locals = worlds.value();
    for (size_t i = 0; i < locals.size(); ++i) {
        if (impl_->parents[i] >= 0)
            locals[i] = relative(worlds.value()[static_cast<size_t>(impl_->parents[i])], worlds.value()[i]);
        auto projected = constrainRigControl(impl_->definition.controls[i].limits, locals[i]);
        if (!projected.ok()) return Result<std::vector<TransformTRS>>::failure(projected.status());
        locals[i] = projected.value();
    }
    return Result<std::vector<TransformTRS>>::success(std::move(locals));
}

Result<std::unique_ptr<AnimClip>> ControlRig::bake(double rate, const AnimPose& input) const {
    if (!std::isfinite(rate) || rate <= 0 || rate > 240)
        return error<std::unique_ptr<AnimClip>>("sample rate must be in (0,240]");
    const double frameCount = std::ceil(impl_->definition.duration * rate);
    if ((frameCount + 1) * impl_->skeleton.getBoneCount() > 2000000)
        return error<std::unique_ptr<AnimClip>>("bake exceeds two million bone samples");
    auto clip = std::make_unique<AnimClip>("ControlRig bake");
    clip->setDuration(static_cast<float>(impl_->definition.duration));
    clip->setLoop(false);
    clip->setSampleRate(static_cast<float>(rate));
    AnimPose pose;
    for (int frame = 0; frame <= static_cast<int>(frameCount); ++frame) {
        const double time      = std::min(static_cast<double>(frame) / rate, impl_->definition.duration);
        auto         evaluated = evaluate(time, input, pose);
        if (!evaluated.ok()) return Result<std::unique_ptr<AnimClip>>::failure(evaluated.status());
        for (int bone = 0; bone < pose.getBoneCount(); ++bone) {
            const auto& t       = pose.local(bone);
            const float seconds = static_cast<float>(time);
            clip->addPositionKey(bone, seconds, t.px, t.py, t.pz);
            clip->addRotationKey(bone, seconds, t.qx, t.qy, t.qz, t.qw);
            clip->addScaleKey(bone, seconds, t.sx, t.sy, t.sz);
        }
    }
    return Result<std::unique_ptr<AnimClip>>::success(std::move(clip));
}

Result<void> reparentRigControl(RigDefinition& d, std::string_view control, std::string_view parent) {
    auto valid = validateRigDefinition(d);
    if (!valid.ok()) return valid;
    const int index = controlIndex(d, control), newParent = controlIndex(d, parent);
    if (index < 0 || (!parent.empty() && newParent < 0)) return error<void>("unknown control or parent");
    for (const auto& c : d.controls)
        if (!c.keys.empty()) return error<void>("reparent requires unanimated rig; bake animated space changes first");
    auto order = controlOrder(d);
    if (!order.ok()) return Result<void>::failure(order.status());
    std::vector<TransformTRS> worlds(d.controls.size());
    for (int i : order.value()) {
        const auto& c                  = d.controls[static_cast<size_t>(i)];
        const int   p                  = controlIndex(d, c.parent);
        worlds[static_cast<size_t>(i)] = p < 0 ? c.initial : detail::mulTRS(worlds[static_cast<size_t>(p)], c.initial);
    }
    auto  candidate = d;
    auto& changed   = candidate.controls[static_cast<size_t>(index)];
    changed.parent  = std::string(parent);
    changed.initial = newParent < 0
                          ? worlds[static_cast<size_t>(index)]
                          : relative(worlds[static_cast<size_t>(newParent)], worlds[static_cast<size_t>(index)]);
    // A calibration is expressed in the old parent frame; silently retaining it is invalid.
    const auto& limits = changed.limits;
    if (limits.extension || limits.scale.mode != RigAxisMode::Free ||
        std::any_of(limits.translation.begin(), limits.translation.end(),
                    [](const auto& a) { return a.mode != RigAxisMode::Free; }) ||
        std::any_of(limits.rotation.begin(), limits.rotation.end(),
                    [](const auto& a) { return a.mode != RigAxisMode::Free; }))
        return error<void>("disable constraints before reparenting and recalibrate in the new parent space");
    changed.limits.reference = changed.initial;
    auto checked             = validateRigDefinition(candidate);
    if (!checked.ok()) return checked;
    d = std::move(candidate);
    return Result<void>::success();
}
}  // namespace eve::animation
