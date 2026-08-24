#pragma once

#include <compare>
#include <functional>
#include <string>
#include <utility>

namespace eve::editor {

/**
 * @brief Strong string identifier used at editor API and persistence boundaries.
 * @tparam Tag Empty tag type that prevents identifiers from unrelated domains
 *             from being mixed accidentally.
 */
template <class Tag>
class StrongEditorId {
public:
    StrongEditorId() = default;
    explicit StrongEditorId(const char* value) : value_(value ? value : "") {}
    explicit StrongEditorId(std::string value) : value_(std::move(value)) {}

    /** @brief Return the stable textual representation. */
    const std::string& value() const { return value_; }
    /** @brief True when no identity has been assigned. */
    bool     empty() const { return value_.empty(); }
    explicit operator bool() const { return !empty(); }

    auto operator<=>(const StrongEditorId&) const = default;

private:
    std::string value_;
};

/** @brief Hash functor for a strong editor identifier. */
template <class Id>
struct StrongEditorIdHash {
    size_t operator()(const Id& id) const noexcept { return std::hash<std::string>{}(id.value()); }
};

struct SessionIdTag;
struct TargetIdTag;
struct CommandIdTag;
struct ToolIdTag;
struct RuleIdTag;
struct CapabilityIdTag;
struct TransactionIdTag;
struct PlanIdTag;
struct StableIdTag;
struct PropertyPathTag;
struct ObjectIdTag;
struct AssetGuidTag;
struct DocumentIdTag;
struct GraphNodeIdTag;
struct GraphPinIdTag;
struct TaskIdTag;

using SessionId     = StrongEditorId<SessionIdTag>;
using TargetId      = StrongEditorId<TargetIdTag>;
using CommandId     = StrongEditorId<CommandIdTag>;
using ToolId        = StrongEditorId<ToolIdTag>;
using RuleId        = StrongEditorId<RuleIdTag>;
using CapabilityId  = StrongEditorId<CapabilityIdTag>;
using TransactionId = StrongEditorId<TransactionIdTag>;
using PlanId        = StrongEditorId<PlanIdTag>;
using StableId      = StrongEditorId<StableIdTag>;
using PropertyPath  = StrongEditorId<PropertyPathTag>;
using ObjectId      = StrongEditorId<ObjectIdTag>;
using AssetGuid     = StrongEditorId<AssetGuidTag>;
using DocumentId    = StrongEditorId<DocumentIdTag>;
using GraphNodeId   = StrongEditorId<GraphNodeIdTag>;
using GraphPinId    = StrongEditorId<GraphPinIdTag>;
using TaskId        = StrongEditorId<TaskIdTag>;

}  // namespace eve::editor
