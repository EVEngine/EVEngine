#include "vehicle/ContainerAdapters.h"

#include "game_event/GameEvent.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::vehicle {
namespace {

constexpr std::string_view kOccupantPrefix = "vehicle:occupant:";

[[nodiscard]] VehicleEntity::Seats* seatsOf(VehicleEntity* value) noexcept {
    return value == nullptr ? nullptr : &*value->seats();
}

[[nodiscard]] const VehicleEntity::Seats* seatsOf(const VehicleEntity* value) noexcept {
    return value == nullptr ? nullptr : &*const_cast<VehicleEntity*>(value)->seats();
}

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

[[nodiscard]] int parseOccupant(const eve::container::ContainerObject& object) noexcept {
    if (object.type != "vehicle.occupant" || object.quantity != 1u) return 0;
    if (object.id.value().size() <= kOccupantPrefix.size() ||
        object.id.value().compare(0, kOccupantPrefix.size(), kOccupantPrefix) != 0)
        return 0;
    const auto text   = object.id.value().substr(kOccupantPrefix.size());
    int        value  = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0) return 0;
    return value;
}

[[nodiscard]] int payloadOccupant(const eve::container::ContainerObject& object) noexcept {
    const auto* payload = dynamic_cast<const VehicleSeatContainerObject*>(object.payload.get());
    if (payload == nullptr) return parseOccupant(object);
    const auto expectedId =
        eve::container::MembershipId(std::string(kOccupantPrefix) + std::to_string(payload->occupantId));
    if (payload->occupantId <= 0 || object.id != expectedId) return 0;
    return parseOccupant(object) == payload->occupantId ? payload->occupantId : 0;
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
            left.object.type != right->object.type || left.object.quantity != right->object.quantity)
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

class VehicleSeatContainerAdapter::PreparedState final : public eve::container::IContainer::PreparedState {
public:
    PreparedState(VehicleSeatContainerAdapter& owner, std::vector<std::optional<int>> staged, eve::Revision revision)
        : owner_(owner), staged_(std::move(staged)), revision_(revision) {}

    void commit() noexcept override {
        if (committed_) return;
        auto* vehicle = owner_.vehicle();
        auto* seats   = seatsOf(vehicle);
        if (seats == nullptr || seats->list.size() != staged_.size()) std::terminate();
        for (std::size_t index = 0; index < staged_.size(); ++index) {
            auto& seat    = seats->list[index];
            seat.occupied = staged_[index].has_value();
            seat.occupant = staged_[index].value_or(0);
        }
        seats->revision = revision_;
        committed_      = true;
    }

    void rollback() noexcept override {
        if (!committed_) staged_.clear();
    }

private:
    VehicleSeatContainerAdapter&    owner_;
    std::vector<std::optional<int>> staged_;
    eve::Revision                   revision_;
    bool                            committed_ = false;
};

VehicleSeatContainerAdapter::VehicleSeatContainerAdapter(eve::container::ContainerId id, VehicleEntity* vehicle,
                                                         eve::container::AcceptedCondition accepted)
    : descriptor_{std::move(id),
                  vehicle != nullptr ? eve::container::Capacity::fixed(seatsOf(vehicle)->list.size())
                                     : eve::container::Capacity::fixed(0),
                  eve::container::Ordering::ExplicitSlots, std::move(accepted)},
      vehicle_(vehicle != nullptr ? ecs::handle_of(vehicle) : ecs::EntityHandle{}) {}

VehicleEntity* VehicleSeatContainerAdapter::vehicle() const noexcept {
    return static_cast<VehicleEntity*>(ecs::try_get(vehicle_));
}

eve::Revision VehicleSeatContainerAdapter::revision() const noexcept {
    const auto* value = vehicle();
    const auto* seats = seatsOf(value);
    return seats == nullptr ? eve::Revision::zero() : seats->revision;
}

std::uint64_t VehicleSeatContainerAdapter::nextEventSerial() noexcept {
    if (eventSerial_ != std::numeric_limits<std::uint64_t>::max()) ++eventSerial_;
    return eventSerial_;
}

eve::container::MembershipId VehicleSeatContainerAdapter::occupantId(int occupantId) {
    return eve::container::MembershipId(std::string(kOccupantPrefix) + std::to_string(occupantId));
}

eve::container::ContainerObject VehicleSeatContainerAdapter::describe(int occupantId, std::string driver) {
    eve::container::ContainerObject object;
    object.id           = VehicleSeatContainerAdapter::occupantId(occupantId);
    object.type         = "vehicle.occupant";
    object.quantity     = 1;
    auto payload        = std::make_shared<VehicleSeatContainerObject>();
    payload->occupantId = occupantId;
    payload->driver     = std::move(driver);
    object.payload      = std::move(payload);
    return object;
}

eve::Result<eve::container::ContainerSnapshot> VehicleSeatContainerAdapter::snapshot() const {
    const auto* value = vehicle();
    const auto* seats = seatsOf(value);
    if (!descriptor_.id.isValid() || seats == nullptr)
        return eve::Result<eve::container::ContainerSnapshot>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle seat adapter is stale or unbound"));
    eve::container::ContainerSnapshot                result{descriptor_.id, seats->revision, {}};
    std::unordered_set<eve::container::MembershipId> identities;
    result.entries.reserve(seats->list.size());
    for (std::size_t index = 0; index < seats->list.size(); ++index) {
        const auto& seat = seats->list[index];
        if (!seat.occupied) continue;
        if (seat.occupant <= 0 || index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "vehicle seat contains an invalid occupant"));
        const auto id = occupantId(seat.occupant);
        if (!identities.insert(id).second)
            return eve::Result<eve::container::ContainerSnapshot>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "vehicle occupant appears in multiple seats"));
        result.entries.push_back({{id, eve::container::SlotIndex(static_cast<std::int32_t>(index)), eve::Generation(1)},
                                  describe(seat.occupant, seat.driver)});
    }
    return eve::Result<eve::container::ContainerSnapshot>::success(std::move(result));
}

eve::Result<void> VehicleSeatContainerAdapter::validateInsert(
    const eve::container::ContainerObject& object, std::optional<eve::container::SlotIndex> destination,
    std::optional<eve::container::MembershipId> ignoredObject) const {
    const auto* value = vehicle();
    const auto* seats = seatsOf(value);
    if (!descriptor_.id.isValid() || seats == nullptr)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle seat adapter is stale or unbound"));
    if (!object.id.isValid() || object.type != "vehicle.occupant" || object.quantity != 1u)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "vehicle seat object facts are invalid"));
    const int occupant = payloadOccupant(object);
    if (occupant <= 0)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle occupant identity is stale"));

    const auto occupiedBy = [&](std::size_t index) {
        const auto& seat = seats->list[index];
        return seat.occupied && occupantId(seat.occupant) == object.id;
    };
    for (std::size_t index = 0; index < seats->list.size(); ++index) {
        if (occupiedBy(index) && (!ignoredObject || *ignoredObject != object.id))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "vehicle occupant is already seated"));
    }
    if (destination) {
        if (!destination->isValid() || static_cast<std::size_t>(destination->value()) >= seats->list.size())
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "vehicle seat slot is out of range"));
        const auto& seat = seats->list[static_cast<std::size_t>(destination->value())];
        if (seat.occupied && (!ignoredObject || occupantId(seat.occupant) != *ignoredObject))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "vehicle seat slot is occupied"));
    }
    auto accepted = descriptor_.filter.evaluate(object);
    if (!accepted) return accepted;
    std::size_t count = 0;
    for (const auto& seat : seats->list)
        if (seat.occupied && (!ignoredObject || occupantId(seat.occupant) != *ignoredObject)) ++count;
    if (count >= seats->list.size())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "vehicle has no free seat"));
    return eve::Result<void>::success();
}

eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> VehicleSeatContainerAdapter::prepare(
    const eve::container::ContainerSnapshot& expected, const eve::container::ContainerSnapshot& candidate) {
    const auto* value = vehicle();
    const auto* seats = seatsOf(value);
    if (!descriptor_.id.isValid() || seats == nullptr)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle seat adapter is stale or unbound"));
    if (expected.id != descriptor_.id || candidate.id != descriptor_.id)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "vehicle seat prepare targets another container"));
    auto current = snapshot();
    if (!current)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(current.status());
    if (!sameLayout(current.value(), expected))
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle seat expected snapshot is stale"));
    const auto next = expected.revision.incremented();
    if (!next || candidate.revision != *next)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "vehicle seat candidate revision is not the next revision"));
    if (candidate.entries.size() > seats->list.size())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "vehicle seat candidate exceeds capacity"));

    std::vector<std::optional<int>>                  staged(seats->list.size());
    std::unordered_set<eve::container::MembershipId> identities;
    for (const auto& entry : candidate.entries) {
        if (!entry.membership.object.isValid() || !entry.membership.slot.isValid() ||
            static_cast<std::size_t>(entry.membership.slot.value()) >= staged.size() ||
            entry.membership.generation.isZero() || entry.object.id != entry.membership.object ||
            entry.object.type != "vehicle.occupant" || entry.object.quantity != 1u ||
            !identities.insert(entry.membership.object).second)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "vehicle seat candidate contains invalid membership"));
        const int occupant = payloadOccupant(entry.object);
        if (occupant <= 0)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "vehicle seat candidate payload is stale"));
        auto& slot = staged[static_cast<std::size_t>(entry.membership.slot.value())];
        if (slot)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "vehicle seat candidate contains duplicate slots"));
        slot = occupant;
    }
    std::unique_ptr<eve::container::IContainer::PreparedState> prepared =
        std::make_unique<PreparedState>(*this, std::move(staged), *next);
    return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::success(std::move(prepared));
}

eve::Result<eve::container::MembershipId> VehicleSeatContainerAdapter::enter(eve::container::SlotIndex     seatIndex,
                                                                             int                           occupant,
                                                                             eve::container::GameEventSink sink,
                                                                             eve::SimulationTick           tick) {
    const auto serial        = nextEventSerial();
    const auto objectIdValue = occupantId(occupant);
    auto       current       = snapshot();
    if (!current) {
        const auto  status     = current.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Rejected, seatIndex, revision(),
               serial, tick, diagnostic == nullptr ? "stale_container" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto object   = describe(occupant);
    auto accepted = validateInsert(object, seatIndex, std::nullopt);
    if (!accepted) {
        const auto  status     = accepted.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, object.id, eve::container::ContainerEventKind::Rejected, seatIndex,
               current.value().revision, serial, tick, diagnostic == nullptr ? "rejected" : diagnostic->message(),
               ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto       candidate = current.value();
    const auto next      = candidate.revision.incremented();
    if (!next) {
        const auto status = eve::Status::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "vehicle seat revision exhausted"));
        bool ignored = false;
        notify(sink, descriptor_.id, object.id, eve::container::ContainerEventKind::Rejected, seatIndex,
               candidate.revision, serial, tick, "revision_exhausted", ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    candidate.revision = *next;
    candidate.entries.push_back({{object.id, seatIndex, eve::Generation(1)}, std::move(object)});
    auto prepared = prepare(current.value(), candidate);
    if (!prepared) {
        const auto  status     = prepared.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Rejected, seatIndex,
               current.value().revision, serial, tick, diagnostic == nullptr ? "prepare_failed" : diagnostic->message(),
               ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto staged = std::move(prepared).takeValue();
    staged->commit();
    bool callbackFailed = false;
    notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Accepted, seatIndex, *next, serial,
           tick, {}, callbackFailed);
    notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Enter, seatIndex, *next, serial,
           tick, {}, callbackFailed);
    return eve::Result<eve::container::MembershipId>::success(objectIdValue, appliedStatus(callbackFailed));
}

eve::Result<eve::container::MembershipId> VehicleSeatContainerAdapter::exit(eve::container::SlotIndex     seatIndex,
                                                                            eve::container::GameEventSink sink,
                                                                            eve::SimulationTick           tick) {
    const auto                   serial  = nextEventSerial();
    auto                         current = snapshot();
    eve::container::MembershipId objectIdValue;
    if (current) {
        for (const auto& entry : current.value().entries)
            if (entry.membership.slot == seatIndex) objectIdValue = entry.membership.object;
    }
    if (!current || !objectIdValue.isValid()) {
        const auto  status     = current ? eve::Status::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                                       "vehicle seat is already empty"))
                                         : current.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Rejected, seatIndex, revision(),
               serial, tick, diagnostic == nullptr ? "rejected" : diagnostic->message(), ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto candidate = current.value();
    candidate.entries.erase(std::remove_if(candidate.entries.begin(), candidate.entries.end(),
                                           [&](const auto& entry) { return entry.membership.slot == seatIndex; }),
                            candidate.entries.end());
    const auto next = candidate.revision.incremented();
    if (!next) {
        const auto status = eve::Status::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "vehicle seat revision exhausted"));
        bool ignored = false;
        notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Rejected, seatIndex,
               current.value().revision, serial, tick, "revision_exhausted", ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    candidate.revision = *next;
    auto prepared      = prepare(current.value(), candidate);
    if (!prepared) {
        const auto  status     = prepared.status();
        bool        ignored    = false;
        const auto* diagnostic = status.primaryDiagnostic();
        notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Rejected, seatIndex,
               current.value().revision, serial, tick, diagnostic == nullptr ? "prepare_failed" : diagnostic->message(),
               ignored);
        return eve::Result<eve::container::MembershipId>::failure(status);
    }
    auto staged = std::move(prepared).takeValue();
    staged->commit();
    bool callbackFailed = false;
    notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Accepted, seatIndex, *next, serial,
           tick, {}, callbackFailed);
    notify(sink, descriptor_.id, objectIdValue, eve::container::ContainerEventKind::Exit, seatIndex, *next, serial,
           tick, {}, callbackFailed);
    return eve::Result<eve::container::MembershipId>::success(objectIdValue, appliedStatus(callbackFailed));
}

}  // namespace eve::vehicle
