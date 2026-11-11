#include "rpg/ActionKillReward.h"

#include "rpg/ActionDamageSink.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/EncounterVictoryInternal.h"
#include "rpg/RPGActor.h"
#include "rpg/WorldState.h"

#include <cmath>
#include <string>

namespace eve::rpg {
Result<BattleVictoryReceipt> ActionKillReward::settle(const RPGDamageTarget& recipient, GameState& gameState,
                                                      Tracker& tracker, const RPGActionDamageReceipt& death,
                                                      std::string_view encounterId) {
    if (!death.killed || !std::isfinite(death.applied) || death.applied <= 0.0 || !death.source.isValid() ||
        !death.target.isValid() || death.source == death.target || !recipient.subject.isValid() ||
        recipient.subject != death.source)
        return Result<BattleVictoryReceipt>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Reward requires a committed kill credited to the recipient"));
    auto* actor = dynamic_cast<RPGActor*>(ecs::try_get(recipient.actor));
    if (!actor)
        return Result<BattleVictoryReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Reward recipient is no longer a live RPG actor"));

    constexpr auto scope  = "rpg.action-kill.v1";
    const auto     victim = death.target.format();
    WorldState     world(gameState);
    if (world.isObjectConsumed(scope, victim))
        return Result<BattleVictoryReceipt>::success({}, Status::success(StatusCode::NoOp));
    const auto* definition = EncounterCatalogue::find(std::string(encounterId));
    if (!definition)
        return Result<BattleVictoryReceipt>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Action kill reward template is not registered"));
    if (definition->members.size() != 1)
        return Result<BattleVictoryReceipt>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "One kill cannot settle a multi-member encounter"));

    const auto request = detail::encounterVictoryRequest(*definition, scope, victim);
    return BattleVictory::settle(*actor, gameState, tracker, request);
}
}  // namespace eve::rpg
