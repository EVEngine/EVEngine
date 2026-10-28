#pragma once

#include "editing/EditingIds.h"

namespace eve::editor {

/**
 * @brief Compatibility alias for the common UUID-backed editor identifier.
 * @tparam Tag Empty editor tag that keeps unrelated domains non-interchangeable.
 */
template <class Tag>
using StrongEditorId = eve::editing::StrongId<Tag>;

/** @brief Hash functor for a strong editor identifier. */
template <class Id>
using StrongEditorIdHash = eve::editing::StrongIdHash<Id>;

/**
 * @brief Explicitly projects an editor identifier to its common UUID value.
 * @param id Compatibility-aware editor identifier.
 * @return Canonical UUID, including a deterministic projection for legacy text.
 * @remarks Canonical UUID parsing, hashing and ordering live in
 *          `eve::UuidIdAdapter`. Non-UUID editor strings remain accepted as
 *          compatibility spellings and are never treated as persistent UUID text.
 */
using eve::editing::canonicalUuid;

using SessionId     = eve::editing::SessionId;
using TargetId      = eve::editing::TargetId;
using CommandId     = eve::editing::CommandId;
using ToolId        = eve::editing::ToolId;
using RuleId        = eve::editing::RuleId;
using CapabilityId  = eve::editing::CapabilityId;
using TransactionId = eve::editing::TransactionId;
using PlanId        = eve::editing::PlanId;
using StableId      = eve::editing::StableId;
using PropertyPath  = eve::editing::PropertyPath;
using ObjectId      = eve::editing::ObjectId;
using AssetGuid     = eve::editing::AssetGuid;
using DocumentId    = eve::editing::DocumentId;
using GraphNodeId   = eve::editing::GraphNodeId;
using GraphPinId    = eve::editing::GraphPinId;
using TaskId        = eve::editing::TaskId;

}  // namespace eve::editor
