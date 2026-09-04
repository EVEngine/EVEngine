#include "animation_editing/AnimationEditingCommands.h"

#include "animation_editing/AnimationClip.h"

#include <cstdint>

namespace eve::animation_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool readNumber(const editing::Value& value, const char* key, double& out) {
    const editing::Value* entry = field(value, key);
    if (!entry) return false;
    if (const auto* real = entry->getIf<double>()) {
        out = *real;
        return true;
    }
    if (const auto* integer = entry->getIf<std::int64_t>()) {
        out = static_cast<double>(*integer);
        return true;
    }
    return false;
}

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

IAnimationClipEditTarget* clipTarget(editing::IEditableTarget& target) {
    return static_cast<IAnimationClipEditTarget*>(
        target.queryCapability(IAnimationClipEditTarget::editingCapabilityId()));
}

editing::Result<void> registerOne(editing::IEditingCommandRegistry& registry, const char* id,
                                  const char* displayName, editing::EditingCommandPlanner planner) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId(id);
    descriptor.ownerModule       = "animation_editing";
    descriptor.displayName       = displayName;
    descriptor.category          = "Animation";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto settings = registerOne(
        registry, "animation.clip.settings.set.v1", "Set animation clip settings",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = clipTarget(target);
            double duration = 0, sampleRate = 0;
            const editing::Value* loopEntry = field(request.payload, "loop");
            const auto* loop = loopEntry ? loopEntry->getIf<bool>() : nullptr;
            if (!capability || !readNumber(request.payload, "duration", duration) ||
                !readNumber(request.payload, "sampleRate", sampleRate) || !loop)
                return error<editing::CommandPlan>(editing::Status::Rejected, "animation.editing.settings-payload",
                                                   "Clip settings require duration, sampleRate and loop");
            auto operation = capability->makeSetSettings(duration, sampleRate, *loop);
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "animation.editing.settings-operation",
                                                   "Animation clip rejected the settings");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary = editing::Value::Object{{"duration", duration}, {"sampleRate", sampleRate}, {"loop", *loop}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!settings.ok()) return settings;

    auto setTrack = registerOne(
        registry, "animation.clip.track.set.v1", "Set animation clip bone track",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = clipTarget(target);
            if (!capability)
                return error<editing::CommandPlan>(editing::Status::Rejected, "animation.editing.track-target",
                                                   "Animation clip track requires a clip target");
            auto parsed = parseAnimationBoneTrack(request.payload);
            if (!parsed.ok())
                return error<editing::CommandPlan>(parsed.code(), "animation.editing.track-payload",
                                                   "Bone track requires stable id, bone name and keys");
            auto operation = capability->makeSetTrack(parsed.value());
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "animation.editing.track-operation",
                                                   "Animation clip rejected the bone track");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary = editing::Value::Object{{"id", parsed.value().id.value()}, {"bone", parsed.value().bone}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!setTrack.ok()) return setTrack;

    auto deleteTrack = registerOne(
        registry, "animation.clip.track.delete.v1", "Delete animation clip bone track",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = clipTarget(target);
            const editing::Value* idValue = field(request.payload, "id");
            const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
            if (!capability || !id || id->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "animation.editing.track-id",
                                                   "Track deletion requires a clip target and stable id");
            auto operation = capability->makeDeleteTrack(editing::StableId(*id));
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "animation.editing.track-delete",
                                                   "Animation clip could not delete the bone track");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary = editing::Value::Object{{"id", *id}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!deleteTrack.ok()) return deleteTrack;

    auto setEvent = registerOne(
        registry, "animation.clip.event.set.v1", "Set animation clip event",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = clipTarget(target);
            if (!capability)
                return error<editing::CommandPlan>(editing::Status::Rejected, "animation.editing.event-target",
                                                   "Animation clip event requires a clip target");
            auto parsed = parseAnimationEventRecord(request.payload);
            if (!parsed.ok())
                return error<editing::CommandPlan>(parsed.code(), "animation.editing.event-payload",
                                                   "Event requires stable id, time, name and payload");
            auto operation = capability->makeSetEvent(parsed.value());
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "animation.editing.event-operation",
                                                   "Animation clip rejected the event");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary = editing::Value::Object{{"id", parsed.value().id.value()}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!setEvent.ok()) return setEvent;

    auto deleteEvent = registerOne(
        registry, "animation.clip.event.delete.v1", "Delete animation clip event",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = clipTarget(target);
            const editing::Value* idValue = field(request.payload, "id");
            const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
            if (!capability || !id || id->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "animation.editing.event-id",
                                                   "Event deletion requires a clip target and stable id");
            auto operation = capability->makeDeleteEvent(editing::StableId(*id));
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "animation.editing.event-delete",
                                                   "Animation clip could not delete the event");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary = editing::Value::Object{{"id", *id}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!deleteEvent.ok()) return deleteEvent;

    return registerOne(registry, "animation.clip.mask.set.v1", "Set animation clip bone mask",
                       [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                           auto* capability = clipTarget(target);
                           const editing::Value* boneValue = field(request.payload, "bone");
                           const auto* bone = boneValue ? boneValue->getIf<std::string>() : nullptr;
                           double weight = 0;
                           if (!capability || !bone || bone->empty() || !readNumber(request.payload, "weight", weight))
                               return error<editing::CommandPlan>(editing::Status::Rejected,
                                                                  "animation.editing.mask-payload",
                                                                  "Mask requires a clip target, bone and weight");
                           auto operation = capability->makeSetMask({*bone, weight});
                           if (!operation.ok())
                               return error<editing::CommandPlan>(operation.code(), "animation.editing.mask-operation",
                                                                  "Animation clip rejected the mask");
                           editing::CommandPlan plan;
                           plan.operations.push_back(std::move(operation).takeValue());
                           plan.summary = editing::Value::Object{{"bone", *bone}, {"weight", weight}};
                           return eve::editing::applied<editing::CommandPlan>(std::move(plan));
                       });
}

}  // namespace eve::animation_editing
