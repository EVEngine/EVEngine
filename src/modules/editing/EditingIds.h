#pragma once

#include "common/Identity.h"

#include <cstddef>

namespace eve::editing {

/**
 * @brief UUID-backed identifier used by authoring contracts.
 * @tparam Tag Empty domain tag preventing unrelated identifiers from mixing.
 */
template <class Tag>
using StrongId = eve::UuidIdAdapter<Tag>;

/** @brief Hash functor for a strong authoring identifier. */
template <class Id>
struct StrongIdHash {
    [[nodiscard]] std::size_t operator()(const Id& id) const noexcept { return static_cast<std::size_t>(id.hash()); }
};

/**
 * @brief Project an authoring identifier to its canonical UUID value.
 * @tparam Tag Authoring domain tag.
 * @param id Compatibility-aware identifier.
 * @return Canonical UUID, including a deterministic projection for legacy text.
 */
template <class Tag>
[[nodiscard]] eve::StrongUuid<Tag> canonicalUuid(const StrongId<Tag>& id) noexcept {
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

using SessionId     = StrongId<SessionIdTag>;
using TargetId      = StrongId<TargetIdTag>;
using CommandId     = StrongId<CommandIdTag>;
using ToolId        = StrongId<ToolIdTag>;
using RuleId        = StrongId<RuleIdTag>;
using CapabilityId  = StrongId<CapabilityIdTag>;
using TransactionId = StrongId<TransactionIdTag>;
using PlanId        = StrongId<PlanIdTag>;
using StableId      = StrongId<StableIdTag>;
using PropertyPath  = StrongId<PropertyPathTag>;
using ObjectId      = StrongId<ObjectIdTag>;
using AssetGuid     = StrongId<AssetGuidTag>;
using DocumentId    = StrongId<DocumentIdTag>;
using GraphNodeId   = StrongId<GraphNodeIdTag>;
using GraphPinId    = StrongId<GraphPinIdTag>;
using TaskId        = StrongId<TaskIdTag>;

}  // namespace eve::editing
