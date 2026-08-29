#include "building/ContainerAdapters.h"

#include "game_event/GameEvent.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::building {
namespace {

[[nodiscard]] std::string quote(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (byte < 0x20u) {
                    result += "\\u00";
                    constexpr char digits[] = "0123456789abcdef";
                    result.push_back(digits[(byte >> 4u) & 0x0fu]);
                    result.push_back(digits[byte & 0x0fu]);
                } else {
                    result.push_back(static_cast<char>(byte));
                }
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] const GarrisonMember* payloadMember(const eve::container::ContainerObject& object,
                                                  GarrisonMember&                        fallback) {
    const auto* payload = dynamic_cast<const BuildingGarrisonContainerObject*>(object.payload.get());
    if (payload != nullptr) {
        if (payload->member.id != object.id.value() || payload->member.type != object.type ||
            payload->member.tags != object.tags)
            return nullptr;
        return &payload->member;
    }
    if (!object.id.isValid() || object.type.empty()) return nullptr;
    fallback.id   = object.id.value();
    fallback.type = object.type;
    fallback.tags = object.tags;
    return &fallback;
}

[[nodiscard]] bool sameLayout(const eve::container::ContainerSnapshot& lhs,
                              const eve::container::ContainerSnapshot& rhs) {
    if (lhs.id != rhs.id || lhs.revision != rhs.revision || lhs.entries.size() != rhs.entries.size()) return false;
    for (const auto& left : lhs.entries) {
        const auto right = std::find_if(rhs.entries.begin(), rhs.entries.end(), [&](const auto& entry) {
            return entry.membership.object == left.membership.object;
        });
        if (right == rhs.entries.end() || left.membership.slot != right->membership.slot ||
            left.membership.generation != right->membership.generation || left.object.id != right->object.id ||
            left.object.type != right->object.type || left.object.quantity != right->object.quantity ||
            left.object.tags != right->object.tags)
            return false;
    }
    return true;
}

[[nodiscard]] game_event::GameEvent makeEvent(const eve::container::ContainerId&  container,
                                              const eve::container::MembershipId& object,
                                              eve::container::ContainerEventKind kind, eve::container::SlotIndex slot,
                                              eve::Revision revision, std::uint64_t serial, eve::SimulationTick tick,
                                              std::string_view reason = {}) {
    const auto schema = eve::LogicalId::parse(eve::container::containerEventSchemaName);
    if (!schema) std::terminate();

    game_event::GameEvent event;
    event.eventId       = eve::container::deterministicContainerEventId(container, object, kind, serial);
    event.type          = eve::container::containerEventTypeName(kind);
    event.source        = container.value();
    event.subject       = object.value();
    event.schemaId      = *schema;
    event.schemaVersion = eve::SchemaVersion(1);
    event.tick          = tick;
    event.payload       = "{\"containerId\":" + quote(container.value()) + ",\"objectId\":" + quote(object.value()) +
                    ",\"slot\":" + std::to_string(slot.isValid() ? slot.value() : -1) + ",\"revision\":\"" +
                    std::to_string(revision.value()) + "\"";
    if (!reason.empty()) event.payload += ",\"reason\":" + quote(reason);
    event.payload += '}';
    return event;
}

void notify(const eve::container::GameEventSink& sink, const eve::container::ContainerId& container,
            const eve::container::MembershipId& object, eve::container::ContainerEventKind kind,
            eve::container::SlotIndex slot, eve::Revision revision, std::uint64_t serial, eve::SimulationTick tick,
            std::string_view reason, bool& callbackFailed) noexcept {
    if (!sink) return;
    try {
        const auto event = makeEvent(container, object, kind, slot, revision, serial, tick, reason);
        sink(event);
    } catch (...) {
        callbackFailed = true;
    }
}

[[nodiscard]] eve::Status appliedStatus(bool callbackFailed) {
    if (!callbackFailed) return eve::Status::success(eve::StatusCode::Applied);
    try {
        std::vector<eve::Diagnostic> diagnostics;
        diagnostics.emplace_back(eve::Diagnostic::warning(
            eve::DiagnosticCode::CallbackFailure, "container membership committed but an event observer threw"));
        return eve::Status(eve::StatusCode::Applied, std::move(diagnostics));
    } catch (...) {
        return eve::Status::success(eve::StatusCode::Applied);
    }
}

}  // namespace

class BuildingGarrisonContainerAdapter::PreparedState final : public eve::container::IContainer::PreparedState {
public:
    PreparedState(BuildingGarrisonContainerAdapter& owner, std::vector<GarrisonMember> staged,
                  eve::Revision expectedRevision, std::size_t expectedSize, eve::Revision revision)
        : owner_(owner),
          staged_(std::move(staged)),
          expectedRevision_(expectedRevision),
          expectedSize_(expectedSize),
          revision_(revision) {}

    void commit() noexcept override {
        if (committed_) return;
        auto* building = owner_.building();
        if (building == nullptr || building->garrisonRevision != expectedRevision_ ||
            building->garrison.size() != expectedSize_)
            std::terminate();
        building->garrison.swap(staged_);
        building->garrisonRevision = revision_;
        committed_                 = true;
    }

    void rollback() noexcept override {
        if (!committed_) staged_.clear();
    }

private:
    BuildingGarrisonContainerAdapter& owner_;
    std::vector<GarrisonMember>       staged_;
    eve::Revision                     expectedRevision_;
    std::size_t                       expectedSize_ = 0;
    eve::Revision                     revision_;
    bool                              committed_ = false;
};

BuildingGarrisonContainerAdapter::BuildingGarrisonContainerAdapter(eve::container::ContainerId id,
                                                                   PlacementWorld* world, int instanceId,
                                                                   eve::container::Capacity          capacity,
                                                                   eve::container::AcceptedCondition accepted)
    : descriptor_{std::move(id), capacity, eve::container::Ordering::Insertion, std::move(accepted)},
      world_(world),
      instanceId_(instanceId) {}

const PlacedBuilding* BuildingGarrisonContainerAdapter::building() const noexcept {
    if (world_ == nullptr || instanceId_ <= 0) return nullptr;
    const auto& values = world_->buildings();
    const auto  it     = values.find(instanceId_);
    return it == values.end() ? nullptr : &it->second;
}

PlacedBuilding* BuildingGarrisonContainerAdapter::building() noexcept {
    if (world_ == nullptr || instanceId_ <= 0) return nullptr;
    auto&      values = world_->buildings();
    const auto it     = values.find(instanceId_);
    return it == values.end() ? nullptr : &it->second;
}

eve::Revision BuildingGarrisonContainerAdapter::revision() const noexcept {
    const auto* value = building();
    return value == nullptr ? eve::Revision::zero() : value->garrisonRevision;
}

std::uint64_t BuildingGarrisonContainerAdapter::nextEventSerial() noexcept {
    if (eventSerial_ != std::numeric_limits<std::uint64_t>::max()) ++eventSerial_;
    return eventSerial_;
}

eve::container::ContainerObject BuildingGarrisonContainerAdapter::describe(const GarrisonMember& member) {
    eve::container::ContainerObject object;
    object.id       = eve::container::MembershipId(member.id);
    object.type     = member.type;
    object.tags     = member.tags;
    object.quantity = 1;
    auto payload    = std::make_shared<BuildingGarrisonContainerObject>();
    payload->member = member;
    object.payload  = std::move(payload);
    return object;
}

eve::Result<eve::container::ContainerSnapshot> BuildingGarrisonContainerAdapter::snapshot() const {
    const auto* value = building();
    if (!descriptor_.id.isValid() || value == nullptr)
        return eve::Result<eve::container::ContainerSnapshot>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "building garrison adapter is stale or unbound"));
    if (!descriptor_.capacity.isUnlimited() && value->garrison.size() > descriptor_.capacity.value())
        return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "building garrison exceeds configured capacity"));
    eve::container::ContainerSnapshot result{descriptor_.id, value->garrisonRevision, {}};
    result.entries.reserve(value->garrison.size());
    std::unordered_set<eve::container::MembershipId> identities;
    for (std::size_t index = 0; index < value->garrison.size(); ++index) {
        if (index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "building garrison has too many indexed memberships"));
        const auto& member = value->garrison[index];
        if (!eve::container::MembershipId::from(member.id) || member.type.empty() ||
            !identities.insert(eve::container::MembershipId(member.id)).second)
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "building garrison contains invalid or duplicate member"));
        const auto id = eve::container::MembershipId(member.id);
        result.entries.push_back(
            {{id, eve::container::SlotIndex(static_cast<std::int32_t>(index)), eve::Generation(1)}, describe(member)});
    }
    return eve::Result<eve::container::ContainerSnapshot>::success(std::move(result));
}

eve::Result<void> BuildingGarrisonContainerAdapter::validateInsert(
    const eve::container::ContainerObject& object, std::optional<eve::container::SlotIndex> destination,
    std::optional<eve::container::MembershipId> ignoredObject) const {
    const auto* value = building();
    if (!descriptor_.id.isValid() || value == nullptr)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "building garrison adapter is stale or unbound"));
    if (!object.id.isValid() || object.type.empty() || object.quantity != 1u)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "building garrison object facts are invalid"));
    GarrisonMember fallback;
    if (payloadMember(object, fallback) == nullptr)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "building garrison payload is stale"));
    for (const auto& member : value->garrison) {
        if (member.id == object.id.value() && (!ignoredObject || *ignoredObject != object.id))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "building garrison already contains member"));
    }
    const std::size_t effectiveSize =
        value->garrison.size() -
        ((ignoredObject && std::any_of(value->garrison.begin(), value->garrison.end(),
                                       [&](const auto& member) { return member.id == ignoredObject->value(); }))
             ? 1u
             : 0u);
    if (destination && (!destination->isValid() || static_cast<std::size_t>(destination->value()) > effectiveSize))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "building garrison slot is out of range"));
    auto accepted = descriptor_.filter.evaluate(object);
    if (!accepted) return accepted;
    if (!descriptor_.capacity.isUnlimited() && effectiveSize >= descriptor_.capacity.value())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "building garrison capacity is full"));
    return eve::Result<void>::success();
}

eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> BuildingGarrisonContainerAdapter::prepare(
    const eve::container::ContainerSnapshot& expected, const eve::container::ContainerSnapshot& candidate) {
    const auto* value = building();
    if (!descriptor_.id.isValid() || value == nullptr)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "building garrison adapter is stale or unbound"));
    if (expected.id != descriptor_.id || candidate.id != descriptor_.id)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "building garrison prepare targets another container"));
    auto current = snapshot();
    if (!current)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(current.status());
    if (!sameLayout(current.value(), expected))
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "building garrison expected snapshot is stale"));
    const auto next = expected.revision.incremented();
    if (!next || candidate.revision != *next)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "building garrison candidate revision is not the next revision"));
    if (!descriptor_.capacity.isUnlimited() && candidate.entries.size() > descriptor_.capacity.value())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "building garrison candidate exceeds capacity"));

    std::vector<GarrisonMember> staged;
    staged.reserve(candidate.entries.size());
    std::unordered_set<eve::container::MembershipId> identities;
    for (std::size_t index = 0; index < candidate.entries.size(); ++index) {
        const auto& entry = candidate.entries[index];
        if (!entry.membership.object.isValid() || !entry.membership.slot.isValid() ||
            entry.membership.slot.value() != static_cast<std::int32_t>(index) || entry.membership.generation.isZero() ||
            entry.object.id != entry.membership.object || entry.object.quantity != 1u ||
            !identities.insert(entry.membership.object).second)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "building garrison candidate contains invalid membership"));
        GarrisonMember fallback;
        const auto*    member = payloadMember(entry.object, fallback);
        if (member == nullptr)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                       "building garrison candidate payload is stale"));
        staged.push_back(*member);
    }
    std::unique_ptr<eve::container::IContainer::PreparedState> prepared =
        std::make_unique<PreparedState>(*this, std::move(staged), expected.revision, expected.entries.size(), *next);
    return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::success(std::move(prepared));
}

eve::Result<eve::container::MembershipId> BuildingGarrisonContainerAdapter::enter(std::string              memberId,
                                                                                  std::string              type,
                                                                                  std::vector<std::string> tags,
                                                                                  eve::container::GameEventSink sink,
                                                                                  eve::SimulationTick           tick) {
    const auto                         serial = nextEventSerial();
    const eve::container::MembershipId objectId(std::move(memberId));
    auto                               current = snapshot();
    if (!current) {
        const auto  status     = current.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Rejected,
               eve::container::SlotIndex::invalid(), revision(), serial, tick,
               diagnostic == nullptr ? "stale_container" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    eve::container::ContainerObject object;
    object.id       = objectId;
    object.type     = type.empty() ? "building.garrison.member" : std::move(type);
    object.tags     = std::move(tags);
    object.quantity = 1;
    auto accepted   = validateInsert(object, std::nullopt, std::nullopt);
    if (!accepted) {
        const auto  status     = accepted.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Rejected,
               eve::container::SlotIndex::invalid(), current.value().revision, serial, tick,
               diagnostic == nullptr ? "rejected" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto       candidate = current.value();
    const auto next      = candidate.revision.incremented();
    if (!next) {
        const auto status = eve::Status::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "building garrison revision exhausted"));
        bool ignored = false;
        notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Rejected,
               eve::container::SlotIndex::invalid(), candidate.revision, serial, tick, "revision_exhausted", ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    candidate.revision = *next;
    candidate.entries.push_back(
        {{objectId, eve::container::SlotIndex(static_cast<std::int32_t>(current.value().entries.size())),
          eve::Generation(1)},
         std::move(object)});
    auto prepared = prepare(current.value(), candidate);
    if (!prepared) {
        const auto  status     = prepared.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Rejected,
               eve::container::SlotIndex::invalid(), current.value().revision, serial, tick,
               diagnostic == nullptr ? "prepare_failed" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto staged = std::move(prepared).takeValue();
    staged->commit();
    bool       callbackFailed = false;
    const auto slot           = candidate.entries.back().membership.slot;
    notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Accepted, slot, *next, serial, tick, {},
           callbackFailed);
    notify(sink, descriptor_.id, objectId, eve::container::ContainerEventKind::Enter, slot, *next, serial, tick, {},
           callbackFailed);
    return eve::Result<eve::container::MembershipId>::success(objectId, appliedStatus(callbackFailed));
}

eve::Result<eve::container::MembershipId> BuildingGarrisonContainerAdapter::exit(eve::container::MembershipId  memberId,
                                                                                 eve::container::GameEventSink sink,
                                                                                 eve::SimulationTick           tick) {
    const auto                serial  = nextEventSerial();
    auto                      current = snapshot();
    eve::container::SlotIndex slot    = eve::container::SlotIndex::invalid();
    if (current) {
        for (const auto& entry : current.value().entries) {
            if (entry.membership.object == memberId) {
                slot = entry.membership.slot;
                break;
            }
        }
    }
    if (!current || !slot.isValid()) {
        const auto  status     = current ? eve::Status::failure(eve::Diagnostic::error(
                                          eve::DiagnosticCode::NotFound, "building garrison member was not found"))
                                         : current.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, memberId, eve::container::ContainerEventKind::Rejected, slot, revision(), serial,
               tick, diagnostic == nullptr ? "rejected" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto candidate = current.value();
    candidate.entries.erase(std::remove_if(candidate.entries.begin(), candidate.entries.end(),
                                           [&](const auto& entry) { return entry.membership.object == memberId; }),
                            candidate.entries.end());
    for (std::size_t index = 0; index < candidate.entries.size(); ++index)
        candidate.entries[index].membership.slot = eve::container::SlotIndex(static_cast<std::int32_t>(index));
    const auto next = candidate.revision.incremented();
    if (!next) {
        const auto status = eve::Status::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "building garrison revision exhausted"));
        bool ignored = false;
        notify(sink, descriptor_.id, memberId, eve::container::ContainerEventKind::Rejected, slot,
               current.value().revision, serial, tick, "revision_exhausted", ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    candidate.revision = *next;
    auto prepared      = prepare(current.value(), candidate);
    if (!prepared) {
        const auto  status     = prepared.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, memberId, eve::container::ContainerEventKind::Rejected, slot,
               current.value().revision, serial, tick, diagnostic == nullptr ? "prepare_failed" : diagnostic->message(),
               ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto staged = std::move(prepared).takeValue();
    staged->commit();
    bool callbackFailed = false;
    notify(sink, descriptor_.id, memberId, eve::container::ContainerEventKind::Accepted, slot, *next, serial, tick, {},
           callbackFailed);
    notify(sink, descriptor_.id, memberId, eve::container::ContainerEventKind::Exit, slot, *next, serial, tick, {},
           callbackFailed);
    return eve::Result<eve::container::MembershipId>::success(memberId, appliedStatus(callbackFailed));
}

}  // namespace eve::building
