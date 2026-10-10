#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "rts/RTS.h"
#include "rts/RTSScriptInternal.h"

namespace eve::rts::script_internal {
namespace {
Result<std::vector<SubjectRef>> readSubjectArray(ssq::Array array) {
    try {
        std::vector<std::string> texts;
        texts.reserve(array.size());
        for (size_t index = 0; index < array.size(); ++index) texts.push_back(array.get<std::string>(index));
        return parseScriptSubjects(texts);
    } catch (const std::exception& error) {
        return Result<std::vector<SubjectRef>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "subjects"));
    }
}
Result<FormationSpec> parseFormation(const std::string& kind, float spacing, int columns, float rotation) {
    FormationSpec spec;
    if (kind == "line")
        spec.kind = FormationKind::Line;
    else if (kind == "grid")
        spec.kind = FormationKind::Grid;
    else if (kind == "wedge")
        spec.kind = FormationKind::Wedge;
    else if (kind == "column")
        spec.kind = FormationKind::Column;
    else if (kind == "dispersed")
        spec.kind = FormationKind::Dispersed;
    else
        return Result<FormationSpec>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown formation layout", "formation.kind"));
    spec.spacing         = spacing;
    spec.columns         = columns;
    spec.rotationRadians = rotation;
    auto valid           = spec.validate();
    if (!valid) return Result<FormationSpec>::failure(valid.status());
    return Result<FormationSpec>::success(spec);
}
}  // namespace

void exposeFormationBindings(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("moveGroupUnits",
                [vm](RTS* self, ssq::Array texts, float x, float y, const std::string& kind, float spacing, int columns,
                     float rotation, float leadDistance) -> ssq::Table {
                    auto subjects = readSubjectArray(texts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    auto formation = parseFormation(kind, spacing, columns, rotation);
                    if (!formation) return script::projectStatusResult(vm, formation.status());
                    MovementGroupBatch batch;
                    batch.units        = std::move(subjects).takeValue();
                    batch.target       = {x, y};
                    batch.formation    = std::move(formation).takeValue();
                    batch.leadDistance = leadDistance;
                    return script::projectResult(vm, self->submitMovementGroup(batch), fanOutValue);
                });
    cls.addFunc("queueScriptGroupMove",
                [vm](RTS* self, std::int64_t tick, ssq::Array texts, float x, float y, const std::string& kind,
                     float spacing, int columns, float rotation, float leadDistance) -> ssq::Table {
                    if (tick < 0)
                        return script::projectStatusResult(
                            vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "RTS replay tick must be non-negative", "tick")));
                    auto subjects = readSubjectArray(texts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    auto formation = parseFormation(kind, spacing, columns, rotation);
                    if (!formation) return script::projectStatusResult(vm, formation.status());
                    RTSReplayCommand command;
                    command.operation         = RTSReplayOperation::MovementGroup;
                    command.tick              = SimulationTick{static_cast<std::uint64_t>(tick)};
                    command.units             = std::move(subjects).takeValue();
                    command.command.target    = {x, y};
                    command.formation         = std::move(formation).takeValue();
                    command.groupLeadDistance = leadDistance;
                    return script::projectResult(vm, self->queueScriptCommand(std::move(command)));
                });
    cls.addFunc("moveUnits",
                [vm](RTS* self, ssq::Array subjectTexts, float x, float y, bool append, float spacing) -> ssq::Table {
                    auto subjects = readSubjectArray(subjectTexts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    CommandSpec command;
                    command.kind   = OrderKind::Move;
                    command.target = {x, y};
                    command.append = append;
                    FormationSpec formation;
                    formation.kind    = FormationKind::Grid;
                    formation.spacing = spacing;
                    return script::projectResult(
                        vm, self->commandUnits(std::move(subjects).takeValue(), command, formation), fanOutValue);
                });
    cls.addFunc("attackMoveUnits",
                [vm](RTS* self, ssq::Array subjectTexts, float x, float y, bool append, float spacing) -> ssq::Table {
                    auto subjects = readSubjectArray(subjectTexts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    CommandSpec command;
                    command.kind   = OrderKind::AttackMove;
                    command.target = {x, y};
                    command.append = append;
                    FormationSpec formation;
                    formation.kind    = FormationKind::Grid;
                    formation.spacing = spacing;
                    return script::projectResult(
                        vm, self->commandUnits(std::move(subjects).takeValue(), command, formation), fanOutValue);
                });

    cls.addFunc("moveFormationUnits",
                [vm](RTS* self, ssq::Array texts, float x, float y, bool append, const std::string& kind, float spacing,
                     int columns, float rotation) -> ssq::Table {
                    auto subjects = readSubjectArray(texts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    auto formation = parseFormation(kind, spacing, columns, rotation);
                    if (!formation) return script::projectStatusResult(vm, formation.status());
                    CommandSpec command;
                    command.kind   = OrderKind::Move;
                    command.target = {x, y};
                    command.append = append;
                    return script::projectResult(
                        vm, self->commandUnits(std::move(subjects).takeValue(), command, formation.value()),
                        fanOutValue);
                });
    cls.addFunc("queueScriptFormationMove",
                [vm](RTS* self, std::int64_t tick, ssq::Array texts, float x, float y, bool append,
                     const std::string& kind, float spacing, int columns, float rotation) -> ssq::Table {
                    if (tick < 0)
                        return script::projectStatusResult(
                            vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "RTS replay tick must be non-negative", "tick")));
                    auto subjects = readSubjectArray(texts);
                    if (!subjects) return script::projectStatusResult(vm, subjects.status());
                    auto formation = parseFormation(kind, spacing, columns, rotation);
                    if (!formation) return script::projectStatusResult(vm, formation.status());
                    RTSReplayCommand command;
                    command.tick           = SimulationTick{static_cast<std::uint64_t>(tick)};
                    command.units          = std::move(subjects).takeValue();
                    command.command.kind   = OrderKind::Move;
                    command.command.target = {x, y};
                    command.command.append = append;
                    command.formation      = std::move(formation).takeValue();
                    return script::projectResult(vm, self->queueScriptCommand(std::move(command)));
                });
}
}  // namespace eve::rts::script_internal
