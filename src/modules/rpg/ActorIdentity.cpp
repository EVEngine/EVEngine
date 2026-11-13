// eve-hot-path
#include "rpg/ActorIdentity.h"
#include "rpg/RPGActor.h"

// Registration phase: View<RPGActor>, read ActorIdentity, then end the View before one component write/admission.
// No identity index, gameplay events, time/RNG reads or external callbacks. The caller owns stable-ID allocation.
namespace eve::rpg {
std::optional<SubjectRef> actorSubject(const RPGActor& actor) noexcept {
    auto* identities = actor.getComponentManager().getComponentBuffer<ActorIdentity>();
    if (!identities || !identities->has(actor.id)) return {};
    const auto subject = identities->ptr(actor.id)->subject_;
    return subject.isValid() ? std::optional<SubjectRef>{subject} : std::nullopt;
}
Result<void> bindActorIdentity(RPGActor& actor, SubjectRef subject) {
    auto fail = [](DiagnosticCode code, const char* message) {
        return Result<void>::failure(Diagnostic::error(code, message, "rpg.identity"));
    };
    if (!subject.isValid()) return fail(DiagnosticCode::InvalidArgument, "Actor subject must be non-nil");
    if (!actor.table || ecs::try_get(ecs::handle_of(&actor)) != &actor)
        return fail(DiagnosticCode::StaleHandle, "Identity registration requires a live actor");
    if (actor.table->is_deferred())
        return fail(DiagnosticCode::PreconditionViolation, "Identity registration requires a Table outside Views");
    const auto current = actorSubject(actor);
    if (current && *current != subject)
        return fail(DiagnosticCode::Conflict, "Actor incarnation cannot change its registered subject");
    {
        auto view = ecs::View<RPGActor>(*actor.table);
        for (auto it = view.begin(); it != view.end(); ++it) {
            if (it.entity() == &actor) continue;
            const auto registered = actorSubject(*it.entity());
            if (registered && *registered == subject)
                return fail(DiagnosticCode::Conflict, "Another live RPG actor already owns this subject");
        }
    }
    if (current) return Result<void>::success();
    // emplace only admits absent storage; an initialized actor already has an unregistered value.
    auto& identity    = actor.identity().emplace();
    identity.subject_ = subject;
    return Result<void>::success();
}
}  // namespace eve::rpg
