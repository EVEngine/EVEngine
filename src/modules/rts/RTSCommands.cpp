#include "rts/RTS.h"

namespace eve::rts {
Result<FanOutReceipt> RTS::fanOut(Player::Selection& selection, const CommandSpec& command,
                                  const FormationSpec& formation) const {
    auto result = CommandFanOutSystem::fanOut(selection.units, command, formation);
    if (!result) return result;
    ++selection.revision;
    return result;
}

Result<FanOutReceipt> RTS::commandUnits(std::span<const SubjectRef> subjects, const CommandSpec& command,
                                        const FormationSpec& formation) const {
    Player::Selection selection;
    selection.units.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        Unit* unit = findUnit(subjects[index]);
        if (unit == nullptr) {
            return Result<FanOutReceipt>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                                    "RTS command unit identity was not found",
                                                                    "subjects[" + std::to_string(index) + "]"));
        }
        selection.units.push_back(ecs::handle_of(unit));
    }
    return fanOut(selection, command, formation);
}

}  // namespace eve::rts
