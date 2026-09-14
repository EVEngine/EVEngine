#pragma once

#include "common/Identity.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

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
using ObjectId      = StrongId<ObjectIdTag>;
using AssetGuid     = StrongId<AssetGuidTag>;
using DocumentId    = StrongId<DocumentIdTag>;
using GraphNodeId   = StrongId<GraphNodeIdTag>;
using GraphPinId    = StrongId<GraphPinIdTag>;
using TaskId        = StrongId<TaskIdTag>;

/**
 * @brief Dotted property path (`layout.size`) compared and persisted as text.
 *
 * This is not a UUID identity. Equality is the exact path spelling, not a hash
 * projection, so persistence must store `value()` rather than a generated UUID.
 */
class PropertyPath {
public:
    PropertyPath() = default;
    explicit PropertyPath(const char* value) : value_(value ? value : "") {}
    explicit PropertyPath(std::string value) : value_(std::move(value)) {}
    explicit PropertyPath(std::string_view value) : value_(value) {}

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool               empty() const noexcept { return value_.empty(); }
    [[nodiscard]] explicit           operator bool() const noexcept { return !empty(); }

    friend bool operator==(const PropertyPath&, const PropertyPath&) = default;
    friend auto operator<=>(const PropertyPath&, const PropertyPath&) = default;

private:
    std::string value_;
};

/** @brief Hash functor for a dotted property path. */
struct PropertyPathHash {
    [[nodiscard]] std::size_t operator()(const PropertyPath& path) const noexcept {
        return std::hash<std::string>{}(path.value());
    }
};

}  // namespace eve::editing
