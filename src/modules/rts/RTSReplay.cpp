#include "rts/RTSReplay.h"

#include "rts/RTS.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace eve::rts {
namespace {

Result<void> replayFailure(std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message),
                                                    std::move(path)));
}

SubjectRef parseSubject(const std::string& text) {
    auto parsed = PersistentId::parse(text);
    return parsed ? SubjectRef::fromPersistentId(*parsed) : SubjectRef{};
}

ecs::Entity* resolveTarget(RTS& world, SubjectRef subject) {
    if (!subject.isValid()) return nullptr;
    if (auto* unit = world.findUnit(subject)) return unit;
    if (auto* building = world.findBuilding(subject)) return building;
    return world.findResourceNode(subject);
}

bool normalizedLess(SubjectRef left, SubjectRef right) { return left.format() < right.format(); }

}  // namespace

Result<void> RTSCommandLog::queue(RTSReplayCommand command, SimulationTick currentTick) {
    if (command.tick.value() < currentTick.value())
        return replayFailure("RTS replay command cannot target a past tick", "command.tick");
    if (static_cast<unsigned>(command.operation) > static_cast<unsigned>(RTSReplayOperation::Escort))
        return replayFailure("RTS replay operation is invalid", "command.operation");
    if (!std::isfinite(command.point.x) || !std::isfinite(command.point.y))
        return replayFailure("RTS replay point must be finite", "command.point");
    if (command.operation == RTSReplayOperation::UnitCommand) {
        auto validCommand = command.command.validate();
        if (!validCommand) return validCommand;
        auto validFormation = command.formation.validate();
        if (!validFormation) return validFormation;
        if (command.units.empty()) return replayFailure("RTS replay command requires units", "command.units");
        if (ecs::try_get(command.command.targetEntity) != nullptr && !command.targetEntity.isValid())
            return replayFailure("RTS replay entity targets require a stable SubjectRef", "command.targetEntity");
        command.command.targetEntity = {};
        std::sort(command.units.begin(), command.units.end(), normalizedLess);
        command.units.erase(std::unique(command.units.begin(), command.units.end()), command.units.end());
    } else if (command.operation == RTSReplayOperation::Construction) {
        if (command.units.size() != 1 || !command.faction.isValid() || !command.resultSubject.isValid() ||
            !command.definition.isValid())
            return replayFailure("RTS construction replay requires builder, faction, result, and definition",
                                 "command.construction");
    } else if (command.operation == RTSReplayOperation::Production ||
               command.operation == RTSReplayOperation::ReinforcementProduction) {
        if (!command.producer.isValid() || !command.resultSubject.isValid() || !command.definition.isValid())
            return replayFailure("RTS production replay requires producer, result, and definition",
                                 "command.production");
    } else if (command.operation == RTSReplayOperation::Research) {
        if (!command.producer.isValid() || command.value.empty())
            return replayFailure("RTS research replay requires producer and upgrade", "command.research");
    } else if (command.operation == RTSReplayOperation::Ability &&
               (command.units.size() != 1 || command.value.empty())) {
        return replayFailure("RTS ability replay requires caster and ability", "command.ability");
    } else if (command.operation == RTSReplayOperation::CancelProduction &&
               (!command.producer.isValid() || command.priority < -1)) {
        return replayFailure("RTS production cancellation requires producer and queue index",
                             "command.cancelProduction");
    } else if (command.operation == RTSReplayOperation::CancelAbility && command.units.empty()) {
        return replayFailure("RTS ability cancellation requires units", "command.cancelAbility");
    } else if ((command.operation == RTSReplayOperation::CancelConstruction ||
                command.operation == RTSReplayOperation::SellBuilding) && !command.producer.isValid()) {
        return replayFailure("RTS building lifecycle command requires a building", "command.producer");
    } else if (command.operation == RTSReplayOperation::RequestFireSupport &&
               (!command.producer.isValid() || !std::isfinite(command.command.radius) ||
                command.command.radius <= 0.0f || command.priority <= 0)) {
        return replayFailure("RTS fire-support request requires requester, radius, and shot budget",
                             "command.fireSupport");
    } else if (command.operation == RTSReplayOperation::CancelFireSupport && !command.producer.isValid()) {
        return replayFailure("RTS fire-support cancellation requires a requester", "command.fireSupport");
    } else if (command.operation == RTSReplayOperation::SuppressArea &&
               (command.units.empty() || command.priority < 0 ||
                !std::isfinite(command.command.target.x) || !std::isfinite(command.command.target.y) ||
                !std::isfinite(command.command.secondaryTarget.x) ||
                !std::isfinite(command.command.secondaryTarget.y) ||
                !std::isfinite(command.command.radius) || command.command.radius <= 0.0f ||
                std::hypot(command.command.secondaryTarget.x - command.command.target.x,
                           command.command.secondaryTarget.y - command.command.target.y) <= 1e-3f)) {
        return replayFailure("RTS suppression requires units, corridor, width, and non-negative shots",
                             "command.suppression");
    } else if (command.operation == RTSReplayOperation::Escort &&
               (command.units.empty() || !command.targetEntity.isValid() ||
                !std::isfinite(command.command.radius) || command.command.radius <= 0.0f ||
                !std::isfinite(command.formation.spacing) || command.formation.spacing <= 0.0f)) {
        return replayFailure("RTS escort requires units, target, guard radius, and spacing", "command.escort");
    }
    if (command.operation == RTSReplayOperation::CancelAbility ||
        command.operation == RTSReplayOperation::SuppressArea ||
        command.operation == RTSReplayOperation::Escort) {
        std::sort(command.units.begin(), command.units.end(), normalizedLess);
        command.units.erase(std::unique(command.units.begin(), command.units.end()), command.units.end());
    }
    if (std::any_of(command.units.begin(), command.units.end(), [](SubjectRef value) { return !value.isValid(); }))
        return replayFailure("RTS replay unit subjects must be valid", "command.units");
    queued_[command.tick.value()].push_back(command);
    history_.push_back(std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> RTSCommandLog::apply(SimulationTick tick, RTS& world) {
    auto found = queued_.find(tick.value());
    if (found == queued_.end()) return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    std::size_t applied = 0;
    for (const auto& replay : found->second) {
        if (replay.operation == RTSReplayOperation::Construction) {
            auto* faction = world.findFaction(replay.faction);
            auto* builder = world.findUnit(replay.units.front());
            if (faction == nullptr || builder == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay construction actor cannot be resolved", "command.construction"));
            auto result = world.startScriptConstruction(*faction, replay.resultSubject, replay.definition,
                                                        replay.point, *builder);
            if (!result) return Result<std::size_t>::failure(result.status());
            ++applied;
            continue;
        }
        if (replay.operation == RTSReplayOperation::Production ||
            replay.operation == RTSReplayOperation::ReinforcementProduction ||
            replay.operation == RTSReplayOperation::Research) {
            auto* producer = world.findBuilding(replay.producer);
            if (producer == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay producer cannot be resolved", "command.producer"));
            if (replay.operation == RTSReplayOperation::Research) {
                auto result = world.queueScriptResearch(*producer, replay.value, replay.priority);
                if (!result) return Result<std::size_t>::failure(result.status());
            } else if (replay.operation == RTSReplayOperation::ReinforcementProduction) {
                auto result = world.queueScriptReinforcement(
                    *producer, replay.resultSubject, replay.definition, replay.priority);
                if (!result) return Result<std::size_t>::failure(result.status());
            } else {
                auto result = world.queueScriptUnit(
                    *producer, replay.resultSubject, replay.definition, replay.priority);
                if (!result) return Result<std::size_t>::failure(result.status());
            }
            ++applied;
            continue;
        }
        if (replay.operation == RTSReplayOperation::Ability) {
            auto* caster = world.findUnit(replay.units.front());
            if (caster == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay ability caster cannot be resolved", "command.units"));
            auto result = world.castScriptAbility(*caster, replay.value, replay.targetEntity, replay.point);
            if (!result) return Result<std::size_t>::failure(result.status());
            ++applied;
            continue;
        }
        if (replay.operation == RTSReplayOperation::CancelProduction) {
            auto* producer = world.findBuilding(replay.producer);
            if (producer == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay producer cannot be resolved", "command.producer"));
            auto result = world.cancelScriptProduction(*producer, replay.priority);
            if (!result) return Result<std::size_t>::failure(result.status());
            ++applied;
            continue;
        }
        if (replay.operation == RTSReplayOperation::CancelAbility) {
            for (SubjectRef subject : replay.units) {
                auto* caster = world.findUnit(subject);
                if (caster == nullptr)
                    return Result<std::size_t>::failure(Diagnostic::error(
                        DiagnosticCode::NotFound, "RTS replay ability caster cannot be resolved", "command.units"));
                auto result = world.cancelScriptAbility(*caster);
                if (!result) return Result<std::size_t>::failure(result.status());
                ++applied;
            }
            continue;
        }
        if (replay.operation == RTSReplayOperation::CancelConstruction ||
            replay.operation == RTSReplayOperation::SellBuilding) {
            auto* building = world.findBuilding(replay.producer);
            if (building == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay building cannot be resolved", "command.producer"));
            auto result = replay.operation == RTSReplayOperation::CancelConstruction
                ? world.cancelScriptConstruction(*building)
                : world.sellScriptBuilding(*building);
            if (!result) return Result<std::size_t>::failure(result.status());
            ++applied;
            continue;
        }
        if (replay.operation == RTSReplayOperation::RequestFireSupport ||
            replay.operation == RTSReplayOperation::CancelFireSupport) {
            auto* requester = world.findUnit(replay.producer);
            if (requester == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS fire-support requester cannot be resolved",
                    "command.producer"));
            auto result = replay.operation == RTSReplayOperation::RequestFireSupport
                ? world.requestFireSupport(*requester, replay.point, replay.command.radius,
                                           replay.priority, replay.limit)
                : world.cancelFireSupport(*requester);
            if (!result) return Result<std::size_t>::failure(result.status());
            applied += result.value();
            continue;
        }
        if (replay.operation == RTSReplayOperation::SuppressArea) {
            auto result = world.suppressArea(replay.units, replay.command.target,
                                             replay.command.secondaryTarget,
                                             replay.command.radius, replay.priority);
            if (!result) return Result<std::size_t>::failure(result.status());
            applied += result.value().accepted;
            continue;
        }
        if (replay.operation == RTSReplayOperation::Escort) {
            auto result = world.escortUnits(replay.units, replay.targetEntity,
                                            replay.command.radius, replay.formation.spacing);
            if (!result) return Result<std::size_t>::failure(result.status());
            applied += result.value().accepted;
            continue;
        }
        std::vector<ecs::EntityHandle> handles;
        handles.reserve(replay.units.size());
        for (SubjectRef subject : replay.units) {
            auto* unit = world.findUnit(subject);
            if (unit == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay unit cannot be resolved", "command.units"));
            handles.push_back(ecs::handle_of(unit));
        }
        auto command = replay.command;
        if (replay.targetEntity.isValid()) {
            auto* target = resolveTarget(world, replay.targetEntity);
            if (target == nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS replay target cannot be resolved", "command.targetEntity"));
            command.targetEntity = ecs::handle_of(target);
        }
        auto receipt = CommandFanOutSystem::fanOut(handles, command, replay.formation);
        if (!receipt) return Result<std::size_t>::failure(receipt.status());
        applied += receipt.value().accepted;
    }
    queued_.erase(found);
    return Result<std::size_t>::success(applied, Status::success(StatusCode::Applied));
}

std::string RTSCommandLog::exportText() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool extended = std::any_of(history_.begin(), history_.end(), [](const auto& value) {
        return value.operation != RTSReplayOperation::UnitCommand;
    });
    const bool fireSupport = std::any_of(history_.begin(), history_.end(), [](const auto& value) {
        return value.operation == RTSReplayOperation::RequestFireSupport ||
               value.operation == RTSReplayOperation::CancelFireSupport;
    });
    const unsigned version = fireSupport ? 3u : (extended ? 2u : 1u);
    out << "EVERTS_COMMANDS " << version << '\n'
        << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& value : history_) {
        if (extended) {
            out << value.tick.value() << ' ' << static_cast<unsigned>(value.operation) << ' '
                << std::quoted(value.producer.isValid() ? value.producer.format() : std::string{}) << ' '
                << std::quoted(value.faction.isValid() ? value.faction.format() : std::string{}) << ' '
                << std::quoted(value.resultSubject.isValid() ? value.resultSubject.format() : std::string{}) << ' '
                << std::quoted(value.definition.isValid() ? value.definition.format() : std::string{}) << ' '
                << std::quoted(value.value) << ' ' << value.priority << ' ';
            if (version >= 3) out << value.limit << ' ';
            out << value.point.x << ' ' << value.point.y << ' ';
        }
        out << value.tick.value() << ' ' << static_cast<unsigned>(value.command.kind) << ' '
            << std::quoted(value.command.definitionId) << ' ' << value.command.priority << ' '
            << value.command.timeoutSeconds << ' ' << value.command.target.x << ' ' << value.command.target.y << ' '
            << value.command.secondaryTarget.x << ' ' << value.command.secondaryTarget.y << ' '
            << value.command.radius << ' ' << value.command.append << ' '
            << static_cast<unsigned>(value.formation.kind) << ' ' << value.formation.spacing << ' '
            << value.formation.columns << ' '
            << std::quoted(value.targetEntity.isValid() ? value.targetEntity.format() : std::string{}) << ' '
            << value.units.size();
        for (SubjectRef subject : value.units) out << ' ' << std::quoted(subject.format());
        out << '\n';
    }
    return out.str();
}

Result<void> RTSCommandLog::importText(std::string_view text, SimulationTick currentTick, bool clearExisting) {
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string magic;
    unsigned version = 0;
    if (!(in >> magic >> version) || magic != "EVERTS_COMMANDS" ||
        (version != 1 && version != 2 && version != 3))
        return replayFailure("invalid RTS command log header", "commandLog.header");
    std::vector<RTSReplayCommand> parsed;
    while (in >> std::ws && !in.eof()) {
        RTSReplayCommand value;
        std::uint64_t tick = 0;
        if (version >= 2) {
            unsigned operation = 0;
            std::string producer, faction, result, definition;
            if (!(in >> tick >> operation >> std::quoted(producer) >> std::quoted(faction) >> std::quoted(result) >>
                  std::quoted(definition) >> std::quoted(value.value) >> value.priority) ||
                operation > static_cast<unsigned>(RTSReplayOperation::Escort))
                return replayFailure("malformed extended RTS command log entry", "commandLog.entry");
            if (version >= 3 && !(in >> value.limit))
                return replayFailure("malformed RTS command limit", "commandLog.entry");
            if (!(in >> value.point.x >> value.point.y))
                return replayFailure("malformed RTS command point", "commandLog.entry");
            value.operation = static_cast<RTSReplayOperation>(operation);
            const auto decode = [](const std::string& text) { return text.empty() ? SubjectRef{} : parseSubject(text); };
            value.producer = decode(producer);
            value.faction = decode(faction);
            value.resultSubject = decode(result);
            if ((!producer.empty() && !value.producer.isValid()) || (!faction.empty() && !value.faction.isValid()) ||
                (!result.empty() && !value.resultSubject.isValid()))
                return replayFailure("invalid extended RTS command subject", "commandLog.entry");
            if (!definition.empty()) {
                auto parsedDefinition = LogicalId::parse(definition);
                if (!parsedDefinition) return replayFailure("invalid RTS command definition", "commandLog.definition");
                value.definition = *parsedDefinition;
            }
        }
        unsigned kind = 0, formation = 0;
        std::string target;
        std::size_t unitCount = 0;
        std::uint64_t commandTick = 0;
        if (!(in >> commandTick >> kind >> std::quoted(value.command.definitionId) >> value.command.priority >>
              value.command.timeoutSeconds >> value.command.target.x >> value.command.target.y >>
              value.command.secondaryTarget.x >> value.command.secondaryTarget.y >> value.command.radius >>
              value.command.append >> formation >> value.formation.spacing >> value.formation.columns >>
              std::quoted(target) >> unitCount) || kind > static_cast<unsigned>(OrderKind::SupplyRelay) ||
            formation > static_cast<unsigned>(FormationKind::Wedge) || unitCount > 100000)
            return replayFailure("malformed RTS command log entry", "commandLog.entry");
        if (version >= 2 && commandTick != tick)
            return replayFailure("extended RTS command tick mismatch", "commandLog.tick");
        tick = commandTick;
        value.tick = SimulationTick{tick};
        value.command.kind = static_cast<OrderKind>(kind);
        value.formation.kind = static_cast<FormationKind>(formation);
        if (!target.empty()) {
            value.targetEntity = parseSubject(target);
            if (!value.targetEntity.isValid()) return replayFailure("invalid target subject", "commandLog.target");
        }
        value.units.reserve(unitCount);
        for (std::size_t index = 0; index < unitCount; ++index) {
            std::string unit;
            if (!(in >> std::quoted(unit))) return replayFailure("missing unit subject", "commandLog.units");
            auto subject = parseSubject(unit);
            if (!subject.isValid()) return replayFailure("invalid unit subject", "commandLog.units");
            value.units.push_back(subject);
        }
        RTSCommandLog validator;
        auto valid = validator.queue(value, currentTick);
        if (!valid) return valid;
        parsed.push_back(std::move(value));
    }
    if (clearExisting) clear();
    for (auto& value : parsed) {
        auto accepted = queue(std::move(value), currentTick);
        if (!accepted) return accepted;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void RTSCommandLog::clear() noexcept {
    queued_.clear();
    history_.clear();
}

RTSLockstep::RTSLockstep() {
    auto duration = Duration::fromSeconds(1.0 / 60.0);
    if (duration) fixedStep_ = duration.value();
}

Result<void> RTSLockstep::setFixedStep(Duration value) {
    if (value.nanoseconds() <= 0)
        return replayFailure("RTS lockstep interval must be strictly positive", "fixedStep");
    fixedStep_ = value;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> RTSLockstep::step(RTS& world, IRTSActionExecutor& executor, std::uint64_t count) {
    if (count == 0) return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    if (fixedStep_.nanoseconds() <= 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "RTS lockstep has no valid fixed interval", "fixedStep"));
    std::size_t processed = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
        const SimulationTick next{tick_.value() + 1};
        auto applied = commands_.apply(next, world);
        if (!applied) return Result<std::size_t>::failure(applied.status());
        auto stepped = world.step({next, fixedStep_}, executor);
        if (!stepped) return Result<std::size_t>::failure(stepped.status());
        processed += applied.value() + stepped.value();
        tick_ = next;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

void RTSLockstep::reset(SimulationTick tick, bool clearCommands) noexcept {
    tick_ = tick;
    if (clearCommands) commands_.clear();
}

}  // namespace eve::rts
