#include <algorithm>
#include <cmath>
#include "fluids/VolumeFluid.h"

namespace eve::fluids {
namespace {
bool keyLess(const VolumeFluidContact& a, const VolumeFluidContact& b) {
    return a.colliderLabel < b.colliderLabel || (a.colliderLabel == b.colliderLabel && a.actorGroup < b.actorGroup);
}
bool sameKey(const VolumeFluidContact& a, const VolumeFluidContact& b) {
    return a.colliderLabel == b.colliderLabel && a.actorGroup == b.actorGroup;
}
bool finiteContact(const VolumeFluidContact& contact) {
    const auto finite3 = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    return finite3(contact.point) && finite3(contact.normal) && finite3(contact.impulse) &&
           std::isfinite(contact.distance);
}
}  // namespace

Result<std::vector<VolumeFluidContactEvent>> VolumeFluidContactTracker::advance(
    std::span<const VolumeFluidContact> contacts, float distanceThreshold) {
    using Output = Result<std::vector<VolumeFluidContactEvent>>;
    if (contacts.size() > 65536 || !std::isfinite(distanceThreshold) || distanceThreshold < 0.f ||
        distanceThreshold > 1.f ||
        std::any_of(contacts.begin(), contacts.end(), [](const auto& contact) { return !finiteContact(contact); }))
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid contact event batch",
                                                 "fluids.volume.contacts"));

    scratch_.clear();
    scratch_.reserve(std::max(scratch_.capacity(), contacts.size()));
    for (const auto& contact : contacts)
        if (contact.distance <= distanceThreshold) scratch_.push_back(contact);
    std::stable_sort(scratch_.begin(), scratch_.end(), keyLess);
    scratch_.erase(std::unique(scratch_.begin(), scratch_.end(), sameKey), scratch_.end());

    std::vector<VolumeFluidContactEvent> events;
    events.reserve(scratch_.size() + previous_.size());
    size_t current = 0, previous = 0;
    while (current < scratch_.size() && previous < previous_.size()) {
        if (keyLess(scratch_[current], previous_[previous]))
            events.push_back({VolumeFluidContactEventType::Enter, scratch_[current++]});
        else if (keyLess(previous_[previous], scratch_[current]))
            events.push_back({VolumeFluidContactEventType::Exit, previous_[previous++]});
        else {
            events.push_back({VolumeFluidContactEventType::Stay, scratch_[current++]});
            ++previous;
        }
    }
    while (current < scratch_.size()) events.push_back({VolumeFluidContactEventType::Enter, scratch_[current++]});
    while (previous < previous_.size()) events.push_back({VolumeFluidContactEventType::Exit, previous_[previous++]});
    previous_ = scratch_;
    return Output::success(std::move(events));
}

void VolumeFluidContactTracker::reset() {
    previous_.clear();
    scratch_.clear();
}
}  // namespace eve::fluids
