#include "action_editor/ActionTimelineEditor.h"

#include "editor/EditorProperty.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace eve::editor {
namespace {

constexpr std::string_view kReplaceOperation = "action.timeline.replace";

template <class T = void>
EditorResult<T> editorError(EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(std::move(rule)), std::move(message));
}

std::string commonMessage(const Status& status, std::string fallback) {
    if (!status.diagnostics().empty() && !status.diagnostics().front().message().empty())
        return status.diagnostics().front().message();
    return fallback;
}

action::ActionTrack* findTrack(action::ActionTimeline& timeline, const LogicalId& id) {
    auto found = std::find_if(timeline.tracks.begin(), timeline.tracks.end(),
                              [&](const action::ActionTrack& track) { return track.id == id; });
    return found == timeline.tracks.end() ? nullptr : &*found;
}

const action::ActionTrack* findTrack(const action::ActionTimeline& timeline, const LogicalId& id) {
    auto found = std::find_if(timeline.tracks.begin(), timeline.tracks.end(),
                              [&](const action::ActionTrack& track) { return track.id == id; });
    return found == timeline.tracks.end() ? nullptr : &*found;
}

void sortTrack(action::ActionTrack& track) {
    std::stable_sort(track.notifies.begin(), track.notifies.end(), [](const auto& left, const auto& right) {
        return left.time == right.time ? left.id.format() < right.id.format() : left.time < right.time;
    });
    std::stable_sort(track.states.begin(), track.states.end(), [](const auto& left, const auto& right) {
        return left.start == right.start ? left.id.format() < right.id.format() : left.start < right.start;
    });
}

EditorValue replacementPayload(const std::string& json) {
    EditorValue::Object payload;
    payload["json"] = json;
    return EditorValue(std::move(payload));
}

const std::string* jsonPayload(const EditorValue& value) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find("json");
    return found == object->end() ? nullptr : found->second.getIf<std::string>();
}

action::ActionTimeline seedTimeline(const std::string& targetId) {
    action::ActionTimeline timeline;
    if (auto parsed = LogicalId::parse(targetId)) {
        timeline.actionId = std::move(*parsed);
    } else if (auto fromName = LogicalId::fromParts("action", targetId.empty() ? "untitled" : targetId)) {
        timeline.actionId = std::move(*fromName);
    } else {
        timeline.actionId = *LogicalId::fromParts("action", "untitled");
    }
    timeline.duration = Duration::fromNanoseconds(1'000'000'000);
    return timeline;
}

PropertyDescriptor property(const char* path, const char* label, PropertyType type, EditorValue defaultValue,
                            double minimum = 0.0, double maximum = 1e9) {
    PropertyDescriptor descriptor;
    descriptor.path           = PropertyPath(path);
    descriptor.displayNameKey = label;
    descriptor.category       = "timeline";
    descriptor.type           = type;
    descriptor.flags          = PropertyFlag::Runtime;
    descriptor.defaultValue   = std::move(defaultValue);
    descriptor.numeric.minimum = minimum;
    descriptor.numeric.maximum = maximum;
    return descriptor;
}

}  // namespace

ActionTimelineTarget::ActionTimelineTarget(std::string targetId) {
    timeline_ = seedTimeline(targetId);
    targetId_ = std::move(targetId);
}

ActionTimelineTarget::ActionTimelineTarget(std::string targetId, action::ActionTimeline timeline)
    : targetId_(std::move(targetId)), timeline_(std::move(timeline)) {}

TargetDescriptor ActionTimelineTarget::describe() const {
    return {TargetId(targetId_), "action-timeline", revision_, false,
            {propertyCapabilityId(), eve::editing::IEditingSnapshotProvider::editingCapabilityId()}};
}

void* ActionTimelineTarget::queryCapability(const CapabilityId& capability) {
    if (capability == propertyCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (capability == eve::editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<eve::editing::IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorResult<void> ActionTimelineTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target.value() != targetId_)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.target-mismatch",
                           "Action timeline operation targets another document");
    if (operation.type != kReplaceOperation)
        return editorError(EditorStatus::Unsupported, "editor.action.timeline.operation-unsupported",
                           "Action timeline target only accepts canonical replace operations");
    const std::string* json = jsonPayload(operation.payload);
    if (!json)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.payload-invalid",
                           "Action timeline replacement requires a JSON string");
    auto value = Value::fromJson(*json);
    if (!value)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.json-invalid",
                           commonMessage(value.status(), "Action timeline JSON is invalid"));
    auto decoded = action::ActionTimeline::fromValue(value.value());
    if (!decoded)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.asset-invalid",
                           commonMessage(decoded.status(), "Action timeline asset is invalid"));
    timeline_ = std::move(decoded).takeValue();
    ++revision_;
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> ActionTimelineTarget::cloneDomainState() const {
    auto clone       = std::make_unique<ActionTimelineTarget>(targetId_, timeline_);
    clone->revision_ = revision_;
    return clone;
}

EditorResult<void> ActionTimelineTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<ActionTimelineTarget*>(candidate.get());
    if (!typed || typed->targetId_ != targetId_)
        return editorError(EditorStatus::Conflict, "editor.action.timeline.candidate-mismatch",
                           "Action timeline candidate belongs to another target");
    auto valid = typed->timeline_.validate();
    if (!valid)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.candidate-invalid",
                           commonMessage(valid.status(), "Action timeline candidate is invalid"));
    timeline_ = std::move(typed->timeline_);
    ++revision_;
    return eve::editing::applied<void>();
}

bool ActionTimelineTarget::matches(const SelectionSnapshot& selection) const {
    if (selection.items.empty()) return false;
    for (const auto& item : selection.items) {
        if (item.target != TargetId(targetId_)) return false;
    }
    return true;
}

EditorResult<void> ActionTimelineTarget::assign(action::ActionTimeline candidate) {
    auto valid = candidate.validate();
    if (!valid)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.asset-invalid",
                           commonMessage(valid.status(), "Action timeline asset is invalid"));
    timeline_ = std::move(candidate);
    ++revision_;
    return eve::editing::applied<void>();
}

EditorResult<DomainOperation> ActionTimelineTarget::replacement(const action::ActionTimeline& candidate,
                                                                std::string                   property) const {
    auto beforeValue = timeline_.toValue();
    auto afterValue  = candidate.toValue();
    if (!beforeValue || !afterValue)
        return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.encode-failed",
                                            "Could not encode the action timeline edit");
    auto beforeJson = beforeValue.value().toJson();
    auto afterJson  = afterValue.value().toJson();
    if (!beforeJson || !afterJson)
        return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.encode-failed",
                                            "Could not serialize the action timeline edit");
    DomainOperation operation;
    operation.type        = std::string(kReplaceOperation);
    operation.inverseType = std::string(kReplaceOperation);
    operation.target      = TargetId(targetId_);
    operation.payload     = replacementPayload(afterJson.value());
    operation.inverse     = replacementPayload(beforeJson.value());
    operation.hasInverse  = true;
    operation.affectedObjects.push_back({operation.target, timeline_.actionId.format(), revision_});
    if (!property.empty()) operation.affectedProperties.push_back(std::move(property));
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

eve::Result<eve::Revision> ActionTimelineTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Action timeline selection mismatch", "editor.action.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema ActionTimelineTarget::schema(const SelectionSnapshot&) const {
    PropertySchema schema;
    schema.typeId                  = "action.timeline";
    schema.version                 = 1;
    auto animation                 = property("timeline.animationUri", "editor.action.animation-uri",
                                              PropertyType::AssetRef, "");
    animation.assetTypeFilters     = {"animation", "model3d"};
    schema.properties              = {property("timeline.actionId", "editor.action.action-id", PropertyType::String,
                                               seedTimeline(targetId_).actionId.format()),
                                      property("timeline.durationSeconds", "editor.action.duration", PropertyType::Float,
                                               1.0, 0.0, 3600.0),
                                      std::move(animation)};
    return schema;
}

PropertyReadResult ActionTimelineTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!matches(selection) || !schema(selection).find(path)) return {};
    if (path == PropertyPath("timeline.actionId"))
        return {PropertyReadState::Value, timeline_.actionId.format(), {}};
    if (path == PropertyPath("timeline.durationSeconds"))
        return {PropertyReadState::Value, timeline_.duration.seconds(), {}};
    return {PropertyReadState::Value, timeline_.animationUri, {}};
}

EditorResult<DomainOperation> ActionTimelineTarget::makeSet(const SelectionSnapshot& selection,
                                                            const PropertyPath& path, const EditorValue& value,
                                                            PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    const auto descriptor = schema(selection).find(path);
    if (!matches(selection) || !descriptor || mode != PropertySetMode::Absolute ||
        !validatePropertyValue(*descriptor, value).ok())
        return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.property-invalid",
                                            "Action timeline property assignment is invalid");
    action::ActionTimeline candidate = timeline_;
    if (path == PropertyPath("timeline.actionId")) {
        const auto* text   = value.getIf<std::string>();
        auto        parsed = text ? LogicalId::parse(*text) : std::optional<LogicalId>{};
        if (!parsed)
            return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.action-id",
                                                "Action id must be a namespace:name logical id");
        candidate.actionId = std::move(*parsed);
    } else if (path == PropertyPath("timeline.durationSeconds")) {
        const auto* seconds = value.getIf<double>();
        if (!seconds)
            return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.duration",
                                                "Timeline duration must be a number of seconds");
        auto duration = Duration::fromSeconds(*seconds);
        if (!duration)
            return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.duration",
                                                commonMessage(duration.status(), "Timeline duration is invalid"));
        candidate.duration = std::move(duration).takeValue();
    } else {
        const auto* uri = value.getIf<std::string>();
        if (!uri)
            return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.animation-uri",
                                                "Animation URI must be a string");
        candidate.animationUri = *uri;
    }
    auto valid = candidate.validate();
    if (!valid)
        return editorError<DomainOperation>(EditorStatus::Rejected, "editor.action.timeline.asset-invalid",
                                            commonMessage(valid.status(), "Action timeline property edit is invalid"));
    return replacement(candidate, path.value());
}

EditorResult<DomainOperation> ActionTimelineTarget::makeReset(const SelectionSnapshot& selection,
                                                              const PropertyPath&      path) const {
    const auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return editorError<DomainOperation>(EditorStatus::Unsupported, "editor.action.timeline.property-unsupported",
                                            "Unknown action timeline property");
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorValue ActionTimelineTarget::snapshotValue() const {
    auto encoded = timeline_.toValue();
    if (!encoded) return {};
    return toEditorValue(encoded.value());
}

EditorResult<void> ActionTimelineTarget::loadSnapshot(const EditorValue& snapshot) {
    if (const std::string* json = jsonPayload(snapshot)) {
        auto value = Value::fromJson(*json);
        if (!value)
            return editorError(EditorStatus::Rejected, "editor.action.timeline.json-invalid",
                               commonMessage(value.status(), "Action timeline JSON is invalid"));
        auto decoded = action::ActionTimeline::fromValue(value.value());
        if (!decoded)
            return editorError(EditorStatus::Rejected, "editor.action.timeline.asset-invalid",
                               commonMessage(decoded.status(), "Action timeline asset is invalid"));
        return assign(std::move(decoded).takeValue());
    }
    auto decoded = action::ActionTimeline::fromValue(toPresentationValue(snapshot));
    if (!decoded)
        return editorError(EditorStatus::Rejected, "editor.action.timeline.asset-invalid",
                           commonMessage(decoded.status(), "Action timeline asset is invalid"));
    return assign(std::move(decoded).takeValue());
}

ActionTimelineEditor::ActionTimelineEditor(std::string targetId, action::ActionTimeline timeline)
    : target_(std::move(targetId), std::move(timeline)), authority_(&target_), transactions_(&authority_) {}

EditorResult<void> ActionTimelineEditor::rejected(std::string rule, std::string message) {
    return editorError(EditorStatus::Rejected, std::move(rule), std::move(message));
}

EditorResult<void> ActionTimelineEditor::configureWorkspace(EditorWorkspace& workspace) const {
    EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"action.assets", "Actions", "left", "list", 100},
        {"action.preview", "Action Preview", "center", "preview", 100},
        {"action.inspector", "Action Inspector", "right", "inspector", 100},
        {"action.timeline", "Action Timeline", "bottom", "timeline", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "action.timeline") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return rejected("editor.action.timeline.workspace-conflict",
                            "Could not install the action editor workspace composition");
    }
    if (!candidate.activatePanel("action.timeline"))
        return rejected("editor.action.timeline.workspace-activate-failed",
                        "Could not activate the action timeline panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineEditor::commit(action::ActionTimeline candidate, std::string label,
                                                std::string mergeKey) {
    auto valid = candidate.validate();
    if (!valid)
        return rejected("editor.action.timeline.candidate-invalid",
                        commonMessage(valid.status(), "Action timeline edit is invalid"));
    auto beforeValue = target_.timeline().toValue();
    auto afterValue  = candidate.toValue();
    if (!beforeValue || !afterValue)
        return rejected("editor.action.timeline.encode-failed", "Could not encode the action timeline edit");
    auto beforeJson = beforeValue.value().toJson();
    auto afterJson  = afterValue.value().toJson();
    if (!beforeJson || !afterJson)
        return rejected("editor.action.timeline.encode-failed", "Could not serialize the action timeline edit");
    if (beforeJson.value() == afterJson.value()) {
        return eve::editing::noOp();
    }

    const std::string operationMergeKey = mergeKey;
    TransactionSpec   transaction;
    transaction.id           = TransactionId("action.timeline.tx." + std::to_string(++transactionSequence_));
    transaction.label        = std::move(label);
    transaction.target       = TargetId(target_.targetId());
    transaction.baseRevision = target_.revision();
    transaction.mergeKey     = std::move(mergeKey);
    auto begun               = transactions_.begin(std::move(transaction));
    if (!begun.ok())
        return editorError(begun.code(), "editor.action.timeline.begin-failed", "Could not begin action edit");

    DomainOperation operation;
    operation.type        = std::string(kReplaceOperation);
    operation.inverseType = std::string(kReplaceOperation);
    operation.target      = TargetId(target_.targetId());
    operation.payload     = replacementPayload(afterJson.value());
    operation.inverse     = replacementPayload(beforeJson.value());
    operation.hasInverse  = true;
    operation.affectedObjects.push_back({operation.target, target_.timeline().actionId.format(), target_.revision()});
    operation.affectedProperties.push_back("timeline");
    operation.mergeKey = operationMergeKey;
    auto appended      = transactions_.append(std::move(operation));
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        (void)discarded;
        return appended;
    }
    auto committed = transactions_.commit();
    if (!committed.ok())
        return editorError(committed.code(), "editor.action.timeline.commit-failed", "Action edit was rejected");
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineEditor::addTrack(action::ActionTrack track) {
    action::ActionTimeline candidate = target_.timeline();
    candidate.tracks.push_back(std::move(track));
    return commit(std::move(candidate), "Add action track", "action.timeline.track.add");
}

EditorResult<void> ActionTimelineEditor::addNotify(const LogicalId& trackId, action::ActionNotify notify) {
    action::ActionTimeline candidate = target_.timeline();
    auto*                  track     = findTrack(candidate, trackId);
    if (!track) return rejected("editor.action.timeline.track-not-found", "Action track was not found");
    if (track->locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
    track->notifies.push_back(std::move(notify));
    sortTrack(*track);
    return commit(std::move(candidate), "Add action notify", "action.timeline.notify.add");
}

EditorResult<void> ActionTimelineEditor::addState(const LogicalId& trackId, action::ActionNotifyState state) {
    action::ActionTimeline candidate = target_.timeline();
    auto*                  track     = findTrack(candidate, trackId);
    if (!track) return rejected("editor.action.timeline.track-not-found", "Action track was not found");
    if (track->locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
    track->states.push_back(std::move(state));
    sortTrack(*track);
    return commit(std::move(candidate), "Add action notify state", "action.timeline.state.add");
}

EditorResult<void> ActionTimelineEditor::moveItem(const LogicalId& itemId, Duration delta) {
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        for (auto& notify : track.notifies) {
            if (notify.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            auto moved = notify.time.tryAdd(delta);
            if (!moved) return rejected("editor.action.timeline.time-overflow", "Notify time overflowed");
            notify.time = std::move(moved).takeValue();
            sortTrack(track);
            return commit(std::move(candidate), "Move action notify", "action.timeline.item.move");
        }
        for (auto& state : track.states) {
            if (state.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            auto start = state.start.tryAdd(delta);
            auto end   = state.end.tryAdd(delta);
            if (!start || !end) return rejected("editor.action.timeline.time-overflow", "Notify-state time overflowed");
            state.start = std::move(start).takeValue();
            state.end   = std::move(end).takeValue();
            sortTrack(track);
            return commit(std::move(candidate), "Move action notify state", "action.timeline.item.move");
        }
    }
    return rejected("editor.action.timeline.item-not-found", "Timeline item was not found");
}

EditorResult<void> ActionTimelineEditor::resizeState(const LogicalId& itemId, Duration start, Duration end) {
    if (start < Duration::zero() || end < start || end > target_.timeline().duration)
        return rejected("editor.action.timeline.state-range", "Notify-state range is outside the timeline");
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        auto found = std::find_if(track.states.begin(), track.states.end(),
                                  [&](const auto& value) { return value.id == itemId; });
        if (found == track.states.end()) continue;
        if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
        found->start = start;
        found->end   = end;
        sortTrack(track);
        return commit(std::move(candidate), "Resize action notify state", "action.timeline.state.resize");
    }
    return rejected("editor.action.timeline.state-not-found", "Notify state was not found");
}

EditorResult<void> ActionTimelineEditor::updateItem(const LogicalId& itemId, LogicalId type, Value::Object payload) {
    if (!type.isValid()) return rejected("editor.action.timeline.type-invalid", "Timeline item type is invalid");
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        for (auto& notify : track.notifies) {
            if (notify.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            notify.type    = std::move(type);
            notify.payload = std::move(payload);
            return commit(std::move(candidate), "Edit action notify", "action.timeline.item.properties");
        }
        for (auto& state : track.states) {
            if (state.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            state.type    = std::move(type);
            state.payload = std::move(payload);
            return commit(std::move(candidate), "Edit action notify state", "action.timeline.item.properties");
        }
    }
    return rejected("editor.action.timeline.item-not-found", "Timeline item was not found");
}

EditorResult<void> ActionTimelineEditor::editItem(const LogicalId& itemId, Duration start, Duration end, LogicalId type,
                                                  Value::Object payload) {
    if (!type.isValid()) return rejected("editor.action.timeline.type-invalid", "Timeline item type is invalid");
    if (start < Duration::zero() || end < start || end > target_.timeline().duration)
        return rejected("editor.action.timeline.item-range", "Timeline item range is outside the timeline");
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        for (auto& notify : track.notifies) {
            if (notify.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            if (start != end)
                return rejected("editor.action.timeline.notify-range", "Instant notify start and end must match");
            notify.time    = start;
            notify.type    = std::move(type);
            notify.payload = std::move(payload);
            sortTrack(track);
            return commit(std::move(candidate), "Edit action notify", "action.timeline.item.edit");
        }
        for (auto& state : track.states) {
            if (state.id != itemId) continue;
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            state.start   = start;
            state.end     = end;
            state.type    = std::move(type);
            state.payload = std::move(payload);
            sortTrack(track);
            return commit(std::move(candidate), "Edit action notify state", "action.timeline.item.edit");
        }
    }
    return rejected("editor.action.timeline.item-not-found", "Timeline item was not found");
}

EditorResult<void> ActionTimelineEditor::removeItem(const LogicalId& itemId) {
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        const auto notify = std::find_if(track.notifies.begin(), track.notifies.end(),
                                         [&](const auto& value) { return value.id == itemId; });
        if (notify != track.notifies.end()) {
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            track.notifies.erase(notify);
            selection_.erase(itemId.format());
            return commit(std::move(candidate), "Delete action notify", "action.timeline.item.delete");
        }
        const auto state = std::find_if(track.states.begin(), track.states.end(),
                                        [&](const auto& value) { return value.id == itemId; });
        if (state != track.states.end()) {
            if (track.locked) return rejected("editor.action.timeline.track-locked", "Action track is locked");
            track.states.erase(state);
            selection_.erase(itemId.format());
            return commit(std::move(candidate), "Delete action notify state", "action.timeline.item.delete");
        }
    }
    return rejected("editor.action.timeline.item-not-found", "Timeline item was not found");
}

EditorResult<void> ActionTimelineEditor::setTrackMuted(const LogicalId& trackId, bool muted) {
    action::ActionTimeline candidate = target_.timeline();
    auto*                  track     = findTrack(candidate, trackId);
    if (!track) return rejected("editor.action.timeline.track-not-found", "Action track was not found");
    track->muted = muted;
    return commit(std::move(candidate), muted ? "Mute action track" : "Unmute action track",
                  "action.timeline.track.mute");
}

EditorResult<std::size_t> ActionTimelineEditor::boxSelect(Duration start, Duration end) {
    if (start < Duration::zero() || end < start || end > target_.timeline().duration)
        return eve::editing::failed<std::size_t>(EditorStatus::Rejected,
                                                RuleId("editor.action.timeline.selection-range"),
                                                "Selection range is outside the timeline");
    selection_.clear();
    for (const auto& track : target_.timeline().tracks) {
        for (const auto& notify : track.notifies)
            if (notify.time >= start && notify.time <= end) selection_.insert(notify.id.format());
        for (const auto& state : track.states)
            if (state.end >= start && state.start <= end) selection_.insert(state.id.format());
    }
    return eve::editing::applied<std::size_t>(selection_.size());
}

EditorResult<void> ActionTimelineEditor::selectItem(const LogicalId& itemId, bool additive) {
    bool found = false;
    for (const auto& track : target_.timeline().tracks) {
        found = found || std::any_of(track.notifies.begin(), track.notifies.end(),
                                     [&](const auto& item) { return item.id == itemId; });
        found = found || std::any_of(track.states.begin(), track.states.end(),
                                     [&](const auto& item) { return item.id == itemId; });
    }
    if (!found) return rejected("editor.action.timeline.item-not-found", "Timeline item was not found");
    if (!additive) selection_.clear();
    selection_.insert(itemId.format());
    return eve::editing::applied<void>();
}

std::vector<LogicalId> ActionTimelineEditor::selectedItemIds() const {
    std::vector<LogicalId> result;
    result.reserve(selection_.size());
    for (const auto& value : selection_) {
        auto parsed = LogicalId::parse(value);
        if (parsed) result.push_back(std::move(*parsed));
    }
    return result;
}

EditorResult<std::size_t> ActionTimelineEditor::copySelection() {
    clipboard_.clear();
    for (const auto& track : target_.timeline().tracks) {
        for (const auto& notify : track.notifies)
            if (selection_.contains(notify.id.format())) clipboard_.push_back({track.id, false, notify, {}});
        for (const auto& state : track.states)
            if (selection_.contains(state.id.format())) clipboard_.push_back({track.id, true, {}, state});
    }
    return eve::editing::applied<std::size_t>(clipboard_.size());
}

LogicalId ActionTimelineEditor::copiedId(const LogicalId& source) {
    const std::string name = std::string(source.name()) + ".copy." + std::to_string(++copySequence_);
    auto              id   = LogicalId::fromParts(source.namespaceName(), name);
    return id ? std::move(*id) : LogicalId{};
}

EditorResult<std::size_t> ActionTimelineEditor::paste(Duration offset) {
    if (clipboard_.empty())
        return eve::editing::failed<std::size_t>(EditorStatus::Rejected,
                                                RuleId("editor.action.timeline.clipboard-empty"),
                                                "Action timeline clipboard is empty");
    action::ActionTimeline candidate = target_.timeline();
    std::set<std::string>  pastedSelection;
    for (const auto& item : clipboard_) {
        auto* track = findTrack(candidate, item.trackId);
        if (!track)
            return eve::editing::failed<std::size_t>(EditorStatus::Conflict,
                                                    RuleId("editor.action.timeline.track-not-found"),
                                                    "Clipboard track no longer exists");
        if (track->locked)
            return eve::editing::failed<std::size_t>(
                EditorStatus::Rejected, RuleId("editor.action.timeline.track-locked"), "Clipboard track is locked");
        if (item.state) {
            auto copy  = item.notifyState;
            copy.id    = copiedId(copy.id);
            auto start = copy.start.tryAdd(offset);
            auto end   = copy.end.tryAdd(offset);
            if (!start || !end)
                return eve::editing::failed<std::size_t>(EditorStatus::Rejected,
                                                        RuleId("editor.action.timeline.time-overflow"),
                                                        "Pasted notify-state time overflowed");
            copy.start = std::move(start).takeValue();
            copy.end   = std::move(end).takeValue();
            pastedSelection.insert(copy.id.format());
            track->states.push_back(std::move(copy));
        } else {
            auto copy = item.notify;
            copy.id   = copiedId(copy.id);
            auto time = copy.time.tryAdd(offset);
            if (!time)
                return eve::editing::failed<std::size_t>(EditorStatus::Rejected,
                                                        RuleId("editor.action.timeline.time-overflow"),
                                                        "Pasted notify time overflowed");
            copy.time = std::move(time).takeValue();
            pastedSelection.insert(copy.id.format());
            track->notifies.push_back(std::move(copy));
        }
        sortTrack(*track);
    }
    const std::size_t count = pastedSelection.size();
    auto committed          = commit(std::move(candidate), "Paste action timeline items", "action.timeline.item.paste");
    if (!committed.ok())
        return eve::editing::failed<std::size_t>(committed.code(), RuleId("editor.action.timeline.paste-failed"),
                                                "Could not paste action timeline items");
    selection_ = std::move(pastedSelection);
    return eve::editing::applied<std::size_t>(count);
}

EditorResult<void> ActionTimelineEditor::deleteSelection() {
    if (selection_.empty()) return rejected("editor.action.timeline.selection-empty", "No timeline items are selected");
    action::ActionTimeline candidate = target_.timeline();
    for (auto& track : candidate.tracks) {
        const bool touchesLocked =
            track.locked && (std::any_of(track.notifies.begin(), track.notifies.end(),
                                         [&](const auto& item) { return selection_.contains(item.id.format()); }) ||
                             std::any_of(track.states.begin(), track.states.end(),
                                         [&](const auto& item) { return selection_.contains(item.id.format()); }));
        if (touchesLocked)
            return rejected("editor.action.timeline.track-locked", "Selection contains an item on a locked track");
        std::erase_if(track.notifies, [&](const auto& item) { return selection_.contains(item.id.format()); });
        std::erase_if(track.states, [&](const auto& item) { return selection_.contains(item.id.format()); });
    }
    auto result = commit(std::move(candidate), "Delete action timeline selection", "action.timeline.item.delete");
    if (result.ok()) selection_.clear();
    return result;
}

EditorResult<TransactionReceipt> ActionTimelineEditor::undo() { return transactions_.undo(); }

EditorResult<TransactionReceipt> ActionTimelineEditor::redo() { return transactions_.redo(); }

EditorResult<void> ActionTimelineEditor::seek(Duration time) {
    if (time < Duration::zero() || time > target_.timeline().duration)
        return rejected("editor.action.timeline.seek-range", "Preview seek is outside the timeline");
    previewTime_    = time;
    previewStarted_ = false;
    previewEvents_.clear();
    return eve::editing::applied<void>();
}

EditorResult<std::size_t> ActionTimelineEditor::update(Duration delta) {
    auto plan = planPreview(delta);
    if (!plan.ok())
        return eve::editing::failed<std::size_t>(plan.code(), RuleId("editor.action.timeline.preview-plan"),
                                                "Could not prepare preview advance");
    if (plan.code() == EditorStatus::NoOp) {
        previewEvents_.clear();
        return EditorResult<std::size_t>::success(0, Status::success(EditorStatus::NoOp));
    }
    return applyPreviewPlan(std::move(plan).takeValue());
}

EditorResult<ActionTimelinePreviewPlan> ActionTimelineEditor::planPreview(Duration delta) const {
    if (!playing_) {
        ActionTimelinePreviewPlan plan;
        plan.timelineRevision = target_.revision();
        plan.previous         = previewTime_;
        plan.current          = previewTime_;
        return EditorResult<ActionTimelinePreviewPlan>::success(
            std::move(plan), Status::success(EditorStatus::NoOp));
    }
    if (delta < Duration::zero())
        return eve::editing::failed<ActionTimelinePreviewPlan>(EditorStatus::Rejected,
                                                              RuleId("editor.action.timeline.preview-delta"),
                                                              "Preview delta must be non-negative");
    auto next = previewTime_.tryAdd(delta);
    if (!next)
        return eve::editing::failed<ActionTimelinePreviewPlan>(
            EditorStatus::Rejected, RuleId("editor.action.timeline.preview-overflow"), "Preview time overflowed");
    Duration current = std::move(next).takeValue();
    if (current > target_.timeline().duration) current = target_.timeline().duration;
    auto sampled = target_.timeline().sample(previewTime_, current, !previewStarted_);
    if (!sampled)
        return eve::editing::failed<ActionTimelinePreviewPlan>(
            EditorStatus::Rejected, RuleId("editor.action.timeline.preview-invalid"),
            commonMessage(sampled.status(), "Could not sample action timeline"));
    ActionTimelinePreviewPlan plan;
    plan.timelineRevision = target_.revision();
    plan.previous         = previewTime_;
    plan.current          = current;
    plan.includeStart     = !previewStarted_;
    plan.reachesEnd       = current == target_.timeline().duration;
    plan.events           = std::move(sampled).takeValue();
    return eve::editing::applied<ActionTimelinePreviewPlan>(std::move(plan));
}

EditorResult<std::size_t> ActionTimelineEditor::applyPreviewPlan(ActionTimelinePreviewPlan plan) {
    if (!playing_ || plan.timelineRevision != target_.revision() || plan.previous != previewTime_ ||
        plan.includeStart == previewStarted_)
        return eve::editing::failed<std::size_t>(EditorStatus::Conflict,
                                                RuleId("editor.action.timeline.preview-plan-stale"),
                                                "Prepared preview advance no longer matches transport state");
    auto expected = target_.timeline().sample(plan.previous, plan.current, plan.includeStart);
    if (!expected || expected.value() != plan.events ||
        plan.reachesEnd != (plan.current == target_.timeline().duration))
        return eve::editing::failed<std::size_t>(EditorStatus::Rejected,
                                                RuleId("editor.action.timeline.preview-plan-invalid"),
                                                "Prepared preview advance does not match the authoritative timeline");
    previewEvents_  = std::move(plan.events);
    previewTime_    = plan.current;
    previewStarted_ = true;
    if (plan.reachesEnd) playing_ = false;
    return eve::editing::applied<std::size_t>(previewEvents_.size());
}

}  // namespace eve::editor
