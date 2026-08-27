#pragma once

/**
 * @file Container.h
 * @brief Low-level container membership, zone and atomic transfer contracts.
 *
 * This layer deliberately knows nothing about cards, inventory, UI, ECS or
 * rendering. Domain adapters own their objects and implement the small
 * borrowed-container interface below. TransferService performs a complete
 * preflight followed by one observable commit; adapters must fully construct
 * candidate state before exposing a staged, non-throwing commit.
 */

#include "common/Generation.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Revision.h"

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace eve::game_event {
struct GameEvent;
}

namespace eve::container {

struct ContainerObject;

/** @brief Strong identity of one runtime or persistent container. */
class ContainerId {
public:
    ContainerId() = default;
    explicit ContainerId(std::string value) : value_(std::move(value)) {}

    /** @brief Construct a valid ID, rejecting an empty value. */
    [[nodiscard]] static std::optional<ContainerId> from(std::string_view value) {
        if (value.empty()) return std::nullopt;
        return ContainerId(std::string(value));
    }
    /** @brief Return whether this ID is usable by a transfer request. */
    [[nodiscard]] bool isValid() const noexcept { return !value_.empty(); }
    /** @brief Return the owned stable spelling. */
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    friend bool                      operator==(const ContainerId&, const ContainerId&) noexcept = default;

private:
    std::string value_;
};

/** @brief Strong identity of an object which can have one container membership. */
class MembershipId {
public:
    MembershipId() = default;
    explicit MembershipId(std::string value) : value_(std::move(value)) {}

    /** @brief Construct a valid membership ID, rejecting an empty value. */
    [[nodiscard]] static std::optional<MembershipId> from(std::string_view value) {
        if (value.empty()) return std::nullopt;
        return MembershipId(std::string(value));
    }
    /** @brief Return whether this identity is usable. */
    [[nodiscard]] bool isValid() const noexcept { return !value_.empty(); }
    /** @brief Return the owned stable spelling. */
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    friend bool                      operator==(const MembershipId&, const MembershipId&) noexcept = default;

private:
    std::string value_;
};

/** @brief Non-negative capacity measured in membership slots. */
class Capacity {
public:
    /** @brief Construct a fixed capacity. */
    [[nodiscard]] static constexpr Capacity fixed(std::size_t value) noexcept { return Capacity(value, false); }
    /** @brief Construct an unbounded capacity. */
    [[nodiscard]] static constexpr Capacity unlimited() noexcept { return Capacity(0, true); }
    [[nodiscard]] constexpr bool            isUnlimited() const noexcept { return unlimited_; }
    [[nodiscard]] constexpr std::size_t     value() const noexcept { return value_; }

private:
    constexpr Capacity(std::size_t value, bool unlimited) noexcept : value_(value), unlimited_(unlimited) {}
    std::size_t value_     = 0;
    bool        unlimited_ = false;
};

/** @brief Stable order semantics owned by a container adapter. */
enum class Ordering : std::uint8_t {
    Unordered,
    Insertion,
    Stack,
    Queue,
    ExplicitSlots,
};

/** @brief Explicit slot number; it never implicitly converts to an integer. */
class SlotIndex {
public:
    constexpr SlotIndex() noexcept = default;
    explicit constexpr SlotIndex(std::int32_t value) noexcept : value_(value) {}
    [[nodiscard]] static constexpr SlotIndex invalid() noexcept { return SlotIndex(-1); }
    [[nodiscard]] constexpr bool             isValid() const noexcept { return value_ >= 0; }
    [[nodiscard]] constexpr std::int32_t     value() const noexcept { return value_; }
    friend constexpr bool                    operator==(const SlotIndex&, const SlotIndex&) noexcept  = default;
    friend constexpr auto                    operator<=>(const SlotIndex&, const SlotIndex&) noexcept = default;

private:
    std::int32_t value_ = -1;
};

/** @brief Extensible filter result used by container and zone acceptance rules. */
class Filter {
public:
    using Predicate = std::function<Result<void>(const ContainerObject&)>;

    Filter() = default;
    explicit Filter(Predicate predicate) : predicate_(std::move(predicate)) {}

    /** @brief Evaluate the filter; an empty filter accepts every object. */
    [[nodiscard]] Result<void> evaluate(const struct ContainerObject& object) const {
        if (!predicate_) return Result<void>::success();
        return predicate_(object);
    }
    [[nodiscard]] bool empty() const noexcept { return !predicate_; }

private:
    Predicate predicate_;
};

/** @brief Opaque, owning payload supplied by a domain adapter during transfer. */
struct ContainerObjectPayload {
    virtual ~ContainerObjectPayload() = default;
};

/** @brief Generic object facts used by filters and transfer diagnostics. */
struct ContainerObject {
    MembershipId                                  id;
    std::string                                   type;
    std::vector<std::string>                      tags;
    std::uint32_t                                 quantity = 1;
    std::shared_ptr<const ContainerObjectPayload> payload;
};

/** @brief One object-to-slot relationship, including the observed generation. */
struct Membership {
    MembershipId object;
    SlotIndex    slot;
    Generation   generation = Generation(1);
};

/** @brief Complete adapter-owned state used to build a candidate replacement. */
struct MembershipEntry {
    Membership      membership;
    ContainerObject object;
};

/** @brief Lossless snapshot of one container at one revision. */
struct ContainerSnapshot {
    ContainerId                  id;
    Revision                     revision = Revision(0);
    std::vector<MembershipEntry> entries;
};

/** @brief Static metadata and acceptance policy of a container. */
struct ContainerDescriptor {
    ContainerId id;
    Capacity    capacity = Capacity::unlimited();
    Ordering    ordering = Ordering::Unordered;
    Filter      filter;
};

/** @brief Lifecycle outcome represented by every container-domain event. */
enum class ContainerEventKind : std::uint8_t {
    Enter,
    Exit,
    Accepted,
    Rejected,
};

/**
 * @brief Stable event type spelling shared by all container adapters.
 * @return Borrowed, non-null, null-terminated static text for `kind`.
 * @ownership Borrowed; the returned string is not allocated and must not be freed.
 * @nullable No; unknown enum values return the static unknown spelling.
 * @lifetime Static for the process lifetime.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not invoke callbacks or mutate container state.
 */
[[nodiscard]] inline const char* containerEventTypeName(ContainerEventKind kind) noexcept {
    switch (kind) {
        case ContainerEventKind::Enter: return "container.enter";
        case ContainerEventKind::Exit: return "container.exit";
        case ContainerEventKind::Accepted: return "container.accepted";
        case ContainerEventKind::Rejected: return "container.rejected";
    }
    return "container.rejected";
}

/** @brief Canonical schema name for container lifecycle envelopes. */
inline constexpr std::string_view containerEventSchemaName = "container:event";

/**
 * @brief Callback receiving a canonical game_event envelope.
 *
 * The forward declaration keeps the common layer independent of the
 * game_event implementation. Adapters that emit events include
 * `game_event/GameEvent.h` in their implementation file and must provide a
 * fully populated `game_event::GameEvent`.
 *
 * @remarks Callbacks are observational, synchronous and non-owning. They must
 * not retain the envelope reference, throw, or re-enter the emitting adapter.
 */
using GameEventSink = std::function<void(const game_event::GameEvent&)>;

/**
 * @brief Derives a deterministic event identity for an adapter lifecycle event.
 * @param container Stable container identity.
 * @param object Stable member identity, possibly empty for malformed input.
 * @param kind Lifecycle event kind.
 * @param serial Adapter-local monotonically increasing attempt serial.
 * @return A non-nil domain-separated EventId.
 * @remarks This is identity derivation, not gameplay randomness. The serial is
 *          owned by the adapter and is not persisted as membership state.
 */
[[nodiscard]] inline EventId deterministicContainerEventId(const ContainerId& container, const MembershipId& object,
                                                           ContainerEventKind kind, std::uint64_t serial) noexcept {
    auto mix = [](std::uint64_t value, std::string_view text) noexcept {
        for (const unsigned char byte : text) {
            value ^= byte;
            value *= 1099511628211ull;
        }
        return value;
    };
    std::uint64_t first  = mix(14695981039346656037ull, container.value());
    first                = mix(first, object.value());
    first                = mix(first, std::string_view(containerEventTypeName(kind)));
    std::uint64_t second = mix(1099511628211ull ^ serial, object.value());
    second               = mix(second, container.value());
    second ^= serial + 0x9e3779b97f4a7c15ull;

    EventId::Bytes bytes{};
    for (std::size_t index = 0; index < sizeof(first); ++index)
        bytes[index] = static_cast<std::uint8_t>((first >> (index * 8u)) & 0xffu);
    for (std::size_t index = 0; index < sizeof(second); ++index)
        bytes[sizeof(first) + index] = static_cast<std::uint8_t>((second >> (index * 8u)) & 0xffu);
    // Mark the derived value as a conventional UUID variant/version while
    // retaining deterministic bytes.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x50u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);
    return EventId(bytes);
}

/** @brief Borrowed container implementation owned by a domain adapter. */
class IContainer {
public:
    /**
     * @brief Detached state prepared for an atomic container replacement.
     *
     * `commit()` and `rollback()` are deliberately non-throwing. A prepared
     * state owns every allocation needed by commit, so a coordinator can
     * prepare all participants before making any membership observable.
     */
    class PreparedState {
    public:
        virtual ~PreparedState() = default;

        /** @brief Publish the staged state; the operation is non-throwing. */
        virtual void commit() noexcept = 0;

        /** @brief Abandon an unpublished stage; the operation is non-throwing. */
        virtual void rollback() noexcept = 0;
    };

    virtual ~IContainer() = default;

    /** @brief Return immutable container metadata. */
    [[nodiscard]] virtual const ContainerDescriptor& descriptor() const noexcept = 0;
    /** @brief Return current lossless state without retaining adapter pointers. */
    [[nodiscard]] virtual Result<ContainerSnapshot> snapshot() const = 0;
    /** @brief Validate insertion while optionally ignoring one moved object. */
    [[nodiscard]] virtual Result<void> validateInsert(
        const ContainerObject& object, std::optional<SlotIndex> destination,
        std::optional<MembershipId> ignoredObject = std::nullopt) const = 0;

    /**
     * @brief Prepare an exact replacement from `expected` to `candidate`.
     * @param expected Snapshot observed during transfer preflight.
     * @param candidate Fully constructed post-transfer snapshot.
     * @return A detached state whose commit and rollback operations cannot
     *         fail; no observable state may change before commit.
     * @remarks The adapter must reject a stale `expected` snapshot. The
     *          adapter is borrowed and must outlive the returned state; the
     *          caller must externally serialize access to the adapter.
     */
    [[nodiscard]] virtual Result<std::unique_ptr<PreparedState>> prepare(const ContainerSnapshot& expected,
                                                                         const ContainerSnapshot& candidate) = 0;
};

/** @brief Strong event payload emitted only after a transfer is fully committed. */
struct TransferEvent {
    ContainerId  source;
    ContainerId  destination;
    MembershipId object;
    SlotIndex    sourceSlot;
    SlotIndex    destinationSlot;
    Revision     sourceRevision;
    Revision     destinationRevision;
};

/**
 * @brief Synchronous callback receiving a committed transfer payload.
 * @remarks This is an observational callback: it must not throw, re-enter
 *          TransferService, or mutate either participating container. A
 *          throwing callback is caught and reported as a warning after the
 *          transfer has already committed.
 */
using TransferEventSink = std::function<void(const TransferEvent&)>;

/** @brief Requested optimistic-concurrency transfer. All pointers are borrowed. */
struct TransferRequest {
    IContainer*              source      = nullptr;
    IContainer*              destination = nullptr;
    MembershipId             object;
    std::optional<SlotIndex> sourceSlot;
    std::optional<SlotIndex> destinationSlot;
    std::optional<Revision>  expectedSourceRevision;
    std::optional<Revision>  expectedDestinationRevision;
};

/** @brief Observable receipt for one committed or explicit no-op transfer. */
struct TransferReceipt {
    ContainerId  source;
    ContainerId  destination;
    MembershipId object;
    SlotIndex    sourceSlot;
    SlotIndex    destinationSlot;
    Revision     sourceRevision;
    Revision     destinationRevision;
};

/**
 * @brief Atomic preflight/prepare/commit coordinator for one or two adapters.
 * @remarks A same-container transfer creates one participant and one staged
 *          replacement. No post-commit snapshot or fallible restore is used.
 */
class TransferService {
public:
    /**
     * @brief Preflight and atomically transfer one object.
     * @remarks No adapter is mutated or event emitted until every check and
     *          participant preparation passes. Same-container moves are
     *          supported as one participant. A callback warning does not make
     *          an already committed transfer appear to have failed.
     */
    [[nodiscard]] static Result<TransferReceipt> transfer(const TransferRequest& request,
                                                          TransferEventSink      eventSink = {});
};

// ---- Strong coordinate-space zone contract --------------------------------

/** @brief Coordinate-space tags; distinct types prevent accidental mixing. */
struct ScreenSpace {};
struct World2DSpace {};
struct World3DSpace {};
struct GridSpace {};

template <class Space>
struct Coordinate {
    float x = 0.f;
    float y = 0.f;
};

/** @brief Three-dimensional coordinate; z is intentionally mandatory. */
template <>
struct Coordinate<World3DSpace> {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

template <class Space>
struct Rectangle {
    Coordinate<Space> origin;
    float             width  = 0.f;
    float             height = 0.f;
};

template <class Space>
struct Circle {
    Coordinate<Space> center;
    float             radius = 0.f;
};

/** @brief Axis-aligned 3D box for World3DSpace zones. */
struct Box3D {
    Coordinate<World3DSpace> origin;
    float                    width  = 0.f;
    float                    height = 0.f;
    float                    depth  = 0.f;
};

/** @brief Spherical 3D shape for World3DSpace zones. */
struct Sphere3D {
    Coordinate<World3DSpace> center;
    float                    radius = 0.f;
};

template <class Space>
struct ZoneShapeTraits {
    using type = std::variant<Rectangle<Space>, Circle<Space>>;
};

template <>
struct ZoneShapeTraits<World3DSpace> {
    using type = std::variant<Box3D, Sphere3D>;
};

template <class Space>
using ZoneShape = typename ZoneShapeTraits<Space>::type;

/** @brief Accepted-condition callback for a zone, shared with container filters. */
using AcceptedCondition = Filter;

/**
 * @brief Shape-bounded, capacity-limited zone in one compile-time coordinate space.
 * @tparam Space ScreenSpace, World2DSpace, World3DSpace or GridSpace.
 */
template <class Space>
class Zone {
public:
    /**
     * @brief Validate and construct a named zone.
     * @param id Stable zone/container identity; it must not be empty.
     * @param shape Shape expressed in exactly `Space`.
     * @param capacity Maximum number of memberships.
     * @param accepted Side-effect-free condition evaluated before entry.
     * @return A valid zone or a structured validation failure.
     */
    [[nodiscard]] static Result<Zone> create(ContainerId id, ZoneShape<Space> shape,
                                             Capacity          capacity = Capacity::unlimited(),
                                             AcceptedCondition accepted = {}) {
        if (!id.isValid()) {
            return Result<Zone>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "zone id must not be empty"));
        }
        if (!validShape(shape)) {
            return Result<Zone>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "zone shape must have positive dimensions"));
        }
        return Result<Zone>::success(Zone(std::move(id), std::move(shape), capacity, std::move(accepted)));
    }

    /**
     * @brief Validate and construct an anonymous compatibility zone.
     * @remarks New persisted or cross-module zones should use the named
     *          overload. The compatibility identity is local to this value and
     *          must not be used as a registry key.
     */
    [[nodiscard]] static Result<Zone> create(ZoneShape<Space> shape, Capacity capacity = Capacity::unlimited(),
                                             AcceptedCondition accepted = {}) {
        return create(ContainerId("zone:anonymous"), std::move(shape), capacity, std::move(accepted));
    }

    /** @brief Return whether this factory-created zone contains valid geometry. */
    [[nodiscard]] bool isValid() const noexcept { return valid_; }

    /** @brief Return the stable identity supplied at construction. */
    [[nodiscard]] const ContainerId& id() const noexcept { return id_; }

    /** @brief Return whether this zone has a usable stable identity. */
    [[nodiscard]] bool hasIdentity() const noexcept { return id_.isValid(); }

    /** @brief Test a point from exactly this zone's coordinate space. */
    [[nodiscard]] bool contains(Coordinate<Space> point) const noexcept {
        return std::visit(
            [point](const auto& shape) {
                using Shape = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<Space, World3DSpace>) {
                    if constexpr (std::is_same_v<Shape, Box3D>) {
                        return point.x >= shape.origin.x && point.y >= shape.origin.y && point.z >= shape.origin.z &&
                               point.x <= shape.origin.x + shape.width && point.y <= shape.origin.y + shape.height &&
                               point.z <= shape.origin.z + shape.depth;
                    } else {
                        const float dx = point.x - shape.center.x;
                        const float dy = point.y - shape.center.y;
                        const float dz = point.z - shape.center.z;
                        return dx * dx + dy * dy + dz * dz <= shape.radius * shape.radius;
                    }
                } else if constexpr (std::is_same_v<Shape, Rectangle<Space>>) {
                    return point.x >= shape.origin.x && point.y >= shape.origin.y &&
                           point.x <= shape.origin.x + shape.width && point.y <= shape.origin.y + shape.height;
                } else {
                    const float dx = point.x - shape.center.x;
                    const float dy = point.y - shape.center.y;
                    return dx * dx + dy * dy <= shape.radius * shape.radius;
                }
            },
            shape_);
    }

    /** @brief Test the zone's accepted-condition without mutating membership. */
    [[nodiscard]] Result<void> accepts(const ContainerObject& object) const { return accepted_.evaluate(object); }
    /** @brief Return the side-effect-free accepted condition. */
    [[nodiscard]] const AcceptedCondition& acceptedCondition() const noexcept { return accepted_; }
    /** @brief Return the maximum number of memberships accepted by this zone. */
    [[nodiscard]] Capacity capacity() const noexcept { return capacity_; }
    /** @brief Return the immutable shape in this zone's coordinate space. */
    [[nodiscard]] const ZoneShape<Space>& shape() const noexcept { return shape_; }

private:
    Zone(ContainerId id, ZoneShape<Space> shape, Capacity capacity, AcceptedCondition accepted)
        : id_(std::move(id)),
          shape_(std::move(shape)),
          capacity_(capacity),
          accepted_(std::move(accepted)),
          valid_(true) {}

    [[nodiscard]] static bool validShape(const ZoneShape<Space>& shape) noexcept {
        return std::visit(
            [](const auto& value) {
                using Shape = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Shape, Rectangle<Space>>) {
                    return std::isfinite(value.origin.x) && std::isfinite(value.origin.y) &&
                           std::isfinite(value.width) && std::isfinite(value.height) && value.width > 0.f &&
                           value.height > 0.f;
                } else if constexpr (std::is_same_v<Shape, Box3D>) {
                    return std::isfinite(value.origin.x) && std::isfinite(value.origin.y) &&
                           std::isfinite(value.origin.z) && std::isfinite(value.width) && std::isfinite(value.height) &&
                           std::isfinite(value.depth) && value.width > 0.f && value.height > 0.f && value.depth > 0.f;
                } else if constexpr (std::is_same_v<Space, World3DSpace>) {
                    return std::isfinite(value.center.x) && std::isfinite(value.center.y) &&
                           std::isfinite(value.center.z) && std::isfinite(value.radius) && value.radius > 0.f;
                } else {
                    return std::isfinite(value.center.x) && std::isfinite(value.center.y) &&
                           std::isfinite(value.radius) && value.radius > 0.f;
                }
            },
            shape);
    }

    ContainerId       id_;
    ZoneShape<Space>  shape_;
    Capacity          capacity_;
    AcceptedCondition accepted_;
    bool              valid_ = false;
};

}  // namespace eve::container

namespace std {
template <>
struct hash<eve::container::ContainerId> {
    size_t operator()(const eve::container::ContainerId& value) const noexcept { return hash<string>{}(value.value()); }
};
template <>
struct hash<eve::container::MembershipId> {
    size_t operator()(const eve::container::MembershipId& value) const noexcept {
        return hash<string>{}(value.value());
    }
};
}  // namespace std
