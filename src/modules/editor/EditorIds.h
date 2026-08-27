#pragma once

#include "common/Identity.h"

#include <cstddef>

namespace eve::editor {

/**
 * @brief Compatibility alias for the common UUID-backed editor identifier.
 * @tparam Tag Empty editor tag that keeps unrelated domains non-interchangeable.
 * @remarks Canonical UUID parsing, hashing and ordering live in
 *          `eve::UuidIdAdapter`. Non-UUID editor strings remain accepted as
 *          compatibility spellings and are never treated as persistent UUID text.
 */
template <class Tag>
using StrongEditorId = eve::UuidIdAdapter<Tag>;

/** @brief Hash functor for a strong editor identifier. */
template <class Id>
struct StrongEditorIdHash {
    [[nodiscard]] std::size_t operator()(const Id& id) const noexcept {
        return static_cast<std::size_t>(id.hash());
    }
};

/**
 * @brief Explicitly projects an editor identifier to its common UUID value.
 * @tparam Tag Editor domain tag.
 * @param id Compatibility-aware editor identifier.
 * @return Canonical UUID, including a deterministic projection for legacy text.
 */
template <class Tag>
[[nodiscard]] eve::StrongUuid<Tag> canonicalUuid(const StrongEditorId<Tag>& id) noexcept {
    return id.uuid();
}

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
