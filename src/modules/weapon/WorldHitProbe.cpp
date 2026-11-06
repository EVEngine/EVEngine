// eve-hot-path
#include "weapon/WorldHitProbe.h"

#include <exception>
#include <utility>
#include "common/Diagnostic.h"

namespace eve::weapon {
WorldHitProbe::WorldHitProbe(std::unique_ptr<IWorldSphereSweep> sweep) : sweep_(std::move(sweep)) {}
WorldHitProbe::WorldHitProbe(std::unique_ptr<IWorldSphereSweep> sweep, std::unique_ptr<IWorldHitTargetMapper> mapper)
    : sweep_(std::move(sweep)), mapper_(std::move(mapper)) {}

Result<std::vector<ICarrierHitProbe::Contact>> WorldHitProbe::query(CarrierHandle, const CarrierMotion& previous,
                                                                    const CarrierMotion& current) const {
    using Output = Result<std::vector<Contact>>;
    if (!sweep_)
        return Output::failure(Diagnostic::error(DiagnosticCode::Unsupported, "No world sweep session", "worldSweep"));
    auto hits = sweep_->query(previous.position.x, previous.position.y, previous.position.z, current.position.x,
                              current.position.y, current.position.z);
    if (!hits) return Output::failure(hits.status());
    std::vector<Contact> contacts;
    contacts.reserve(hits.value().size());
    for (const auto& hit : hits.value()) {
        Contact contact;
        contact.point        = {hit.x, hit.y, hit.z};
        contact.normal       = {hit.nx, hit.ny, hit.nz};
        contact.fraction     = hit.fraction;
        contact.worldContact = hit;
        if (mapper_) {
            try {
                auto mapped = mapper_->target(hit);
                if (!mapped) return Output::failure(mapped.status());
                if (mapped.value()) {
                    if (mapped.value()->id == 0 || mapped.value()->generation == 0)
                        return Output::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument,
                            "Mapped target requires a non-null generation-qualified entity", "target"));
                    contact.target = *mapped.value();
                }
            } catch (const std::exception& error) {
                return Output::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "targetMapper"));
            } catch (...) {
                return Output::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "World target mapper threw", "targetMapper"));
            }
        }
        contacts.push_back(std::move(contact));
    }
    return Output::success(std::move(contacts));
}
}  // namespace eve::weapon
