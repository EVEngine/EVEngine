#include "towerdefense/WaveDirector.h"

#include "common/Diagnostic.h"
#include "common/Json.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <utility>

namespace eve::towerdefense {
namespace {

std::string phaseLabel(WavePhase phase) {
    switch (phase) {
        case WavePhase::Idle: return "idle";
        case WavePhase::Preparing: return "preparing";
        case WavePhase::Spawning: return "spawning";
        case WavePhase::BetweenWaves: return "betweenWaves";
        case WavePhase::Completed: return "completed";
        case WavePhase::Failed: return "failed";
    }
    return "idle";
}

eve::Value spawnEventValue(const WaveSpawnEvent& event) {
    return eve::Value(eve::Value::Object{
        {"sequence", eve::Value(static_cast<std::int64_t>(event.sequence))},
        {"waveId", eve::Value(event.waveId)},
        {"waveIndex", eve::Value(static_cast<std::int64_t>(event.waveIndex))},
        {"archetypeId", eve::Value(event.archetypeId)},
        {"entryIndex", eve::Value(static_cast<std::int64_t>(event.entryIndex))},
        {"spawnIndex", eve::Value(static_cast<std::int64_t>(event.spawnIndex))},
    });
}

eve::Value lifecycleEventValue(const WaveLifecycleEvent& event) {
    return eve::Value(eve::Value::Object{
        {"sequence", eve::Value(static_cast<std::int64_t>(event.sequence))},
        {"kind", eve::Value(event.kind)},
        {"waveId", eve::Value(event.waveId)},
        {"waveIndex", eve::Value(static_cast<std::int64_t>(event.waveIndex))},
        {"rewardGold", eve::Value(static_cast<std::int64_t>(event.rewardGold))},
        {"reason", eve::Value(event.reason)},
    });
}

eve::Value advanceResultValue(const WaveAdvanceResult& result) {
    eve::Value::Array spawns;
    spawns.reserve(result.spawns.size());
    for (const auto& spawn : result.spawns) spawns.push_back(spawnEventValue(spawn));
    eve::Value::Array events;
    events.reserve(result.events.size());
    for (const auto& event : result.events) events.push_back(lifecycleEventValue(event));
    return eve::Value(eve::Value::Object{
        {"phase", eve::Value(phaseLabel(result.phase))},
        {"spawnCount", eve::Value(static_cast<std::int64_t>(result.spawns.size()))},
        {"eventCount", eve::Value(static_cast<std::int64_t>(result.events.size()))},
        {"spawns", eve::Value(std::move(spawns))},
        {"events", eve::Value(std::move(events))},
    });
}

Result<WaveEntry> decodeEntry(const eve::json::Value& object) {
    WaveEntry entry;
    entry.archetypeId     = object.getString("archetypeId");
    entry.count           = object.getInt("count", 1);
    entry.intervalSeconds = object.getDouble("intervalSeconds", 0.5);
    entry.delaySeconds    = object.getDouble("delaySeconds", 0.0);
    if (entry.archetypeId.empty())
        return Result<WaveEntry>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "wave entry requires archetypeId", "entry.archetypeId", {},
                                                            "towerdefense"));
    if (entry.count <= 0)
        return Result<WaveEntry>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "wave entry count must be positive", "entry.count", {}, "towerdefense"));
    if (!std::isfinite(entry.intervalSeconds) || entry.intervalSeconds < 0.0)
        return Result<WaveEntry>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "intervalSeconds must be finite and >= 0",
                                                            "entry.intervalSeconds", {}, "towerdefense"));
    if (!std::isfinite(entry.delaySeconds) || entry.delaySeconds < 0.0)
        return Result<WaveEntry>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "delaySeconds must be finite and >= 0",
                                                            "entry.delaySeconds", {}, "towerdefense"));
    return Result<WaveEntry>::success(std::move(entry));
}

Result<WaveDefinition> decodeWave(const eve::json::Value& object) {
    WaveDefinition wave;
    wave.id          = object.getString("id");
    wave.prepSeconds = object.getDouble("prepSeconds", 3.0);
    wave.rewardGold  = object.getInt("rewardGold", 0);
    if (wave.id.empty())
        return Result<WaveDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "wave requires id", "wave.id", {}, "towerdefense"));
    if (!std::isfinite(wave.prepSeconds) || wave.prepSeconds < 0.0)
        return Result<WaveDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "prepSeconds must be finite and >= 0",
                                                                 "wave.prepSeconds", {}, "towerdefense"));
    if (wave.rewardGold < 0)
        return Result<WaveDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "rewardGold must be >= 0", "wave.rewardGold", {}, "towerdefense"));
    const eve::json::Value entries = object.get("entries");
    if (!entries.isArray() || entries.size() == 0)
        return Result<WaveDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "wave requires a non-empty entries array",
                                                                 "wave.entries", {}, "towerdefense"));
    wave.entries.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const eve::json::Value entry = entries.at(i);
        if (!entry.isObject())
            return Result<WaveDefinition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "wave entry must be an object", "wave.entries", {}, "towerdefense"));
        auto decoded = decodeEntry(entry);
        if (!decoded) return Result<WaveDefinition>::failure(decoded.status());
        wave.entries.push_back(std::move(decoded).takeValue());
    }
    return Result<WaveDefinition>::success(std::move(wave));
}

}  // namespace

void WaveDirector::clear() {
    waves_.clear();
    phase_          = WavePhase::Idle;
    waveIndex_      = 0;
    entryIndex_     = 0;
    spawnedInEntry_ = 0;
    remainingPrep_  = 0.0;
    spawnCooldown_  = 0.0;
    lastTick_       = 0;
    hasTick_        = false;
    eventSequence_  = 0;
}

Result<void> WaveDirector::registerWave(WaveDefinition definition) {
    if (definition.id.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "wave requires id", "wave.id", {}, "towerdefense"));
    if (definition.entries.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "wave requires a non-empty entries array", "wave.entries", {},
                                                       "towerdefense"));
    if (!std::isfinite(definition.prepSeconds) || definition.prepSeconds < 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "prepSeconds must be finite and >= 0", "wave.prepSeconds", {},
                                                       "towerdefense"));
    for (const auto& entry : definition.entries) {
        if (entry.archetypeId.empty() || entry.count <= 0 || !std::isfinite(entry.intervalSeconds) ||
            entry.intervalSeconds < 0.0 || !std::isfinite(entry.delaySeconds) || entry.delaySeconds < 0.0)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "wave entry fields are invalid", "wave.entries", {}, "towerdefense"));
    }
    for (auto& existing : waves_) {
        if (existing.id == definition.id) {
            existing = std::move(definition);
            return Result<void>::success(Status::success(StatusCode::Applied));
        }
    }
    waves_.push_back(std::move(definition));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<int> WaveDirector::loadScheduleJson(std::string_view json) {
    std::string               error;
    const eve::json::Document doc = eve::json::Document::parse(std::string(json), &error);
    if (!doc.valid())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                      error.empty() ? "invalid wave schedule json" : error, "schedule",
                                                      {}, "towerdefense"));
    const eve::json::Value root = doc.root();
    if (!root.isArray())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "wave schedule must be a JSON array", "schedule", {}, "towerdefense"));
    clear();
    int loaded = 0;
    for (std::size_t i = 0; i < root.size(); ++i) {
        const eve::json::Value entry = root.at(i);
        if (!entry.isObject())
            return Result<int>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "schedule entries must be objects", "schedule", {}, "towerdefense"));
        auto decoded = decodeWave(entry);
        if (!decoded) return Result<int>::failure(decoded.status());
        auto registered = registerWave(std::move(decoded).takeValue());
        if (!registered) return Result<int>::failure(registered.status());
        ++loaded;
    }
    return Result<int>::success(loaded);
}

std::string WaveDirector::phaseName() const { return phaseLabel(phase_); }

Result<void> WaveDirector::start() {
    if (waves_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "cannot start an empty wave schedule", "schedule", {},
                                                       "towerdefense"));
    waveIndex_      = 0;
    entryIndex_     = 0;
    spawnedInEntry_ = 0;
    spawnCooldown_  = 0.0;
    hasTick_        = false;
    lastTick_       = 0;
    eventSequence_  = 0;
    remainingPrep_  = waves_.front().prepSeconds;
    phase_          = WavePhase::Preparing;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> WaveDirector::reset() {
    phase_          = WavePhase::Idle;
    waveIndex_      = 0;
    entryIndex_     = 0;
    spawnedInEntry_ = 0;
    remainingPrep_  = 0.0;
    spawnCooldown_  = 0.0;
    hasTick_        = false;
    lastTick_       = 0;
    eventSequence_  = 0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void WaveDirector::emitLifecycle(WaveAdvanceResult& out, std::string kind, std::string waveId, int waveIndex,
                                 int rewardGold, std::string reason) {
    WaveLifecycleEvent event;
    event.sequence   = ++eventSequence_;
    event.kind       = std::move(kind);
    event.waveId     = std::move(waveId);
    event.waveIndex  = waveIndex;
    event.rewardGold = rewardGold;
    event.reason     = std::move(reason);
    out.events.push_back(std::move(event));
}

Result<void> WaveDirector::beginWave(int index, WaveAdvanceResult& out) {
    if (index < 0 || index >= static_cast<int>(waves_.size()))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "wave index is out of range", "waveIndex", {}, "towerdefense"));
    waveIndex_       = index;
    entryIndex_      = 0;
    spawnedInEntry_  = 0;
    const auto& wave = waves_[static_cast<std::size_t>(index)];
    spawnCooldown_   = wave.entries.empty() ? 0.0 : wave.entries.front().delaySeconds;
    phase_           = WavePhase::Spawning;
    emitLifecycle(out, "waveStarted", wave.id, index, 0, {});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<WaveAdvanceResult> WaveDirector::advance(std::uint64_t tick, double dt) {
    WaveAdvanceResult out;
    out.phase = phase_;
    if (!std::isfinite(dt) || dt < 0.0)
        return Result<WaveAdvanceResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dt must be finite and >= 0", "advance.dt", {}, "towerdefense"));
    if (hasTick_ && tick <= lastTick_)
        return Result<WaveAdvanceResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "tick must strictly increase", "advance.tick", {}, "towerdefense"));
    hasTick_  = true;
    lastTick_ = tick;

    if (phase_ == WavePhase::Idle || phase_ == WavePhase::Completed || phase_ == WavePhase::Failed) {
        out.phase = phase_;
        return Result<WaveAdvanceResult>::success(std::move(out));
    }

    double        budget               = dt;
    int           spawnsThisAdvance    = 0;
    constexpr int kMaxSpawnsPerAdvance = 256;
    while (true) {
        if (phase_ == WavePhase::Preparing || phase_ == WavePhase::BetweenWaves) {
            if (remainingPrep_ > budget) {
                remainingPrep_ -= budget;
                break;
            }
            budget -= remainingPrep_;
            remainingPrep_ = 0.0;
            auto began     = beginWave(waveIndex_, out);
            if (!began) {
                phase_ = WavePhase::Failed;
                emitLifecycle(out, "failed", {}, waveIndex_, 0, began.status().describe());
                out.phase = phase_;
                return Result<WaveAdvanceResult>::success(std::move(out));
            }
            continue;
        }

        if (phase_ != WavePhase::Spawning) break;
        if (waveIndex_ < 0 || waveIndex_ >= static_cast<int>(waves_.size())) {
            phase_ = WavePhase::Failed;
            emitLifecycle(out, "failed", {}, waveIndex_, 0, "active wave missing");
            break;
        }
        auto& wave = waves_[static_cast<std::size_t>(waveIndex_)];
        if (entryIndex_ >= static_cast<int>(wave.entries.size())) {
            emitLifecycle(out, "waveCompleted", wave.id, waveIndex_, wave.rewardGold, {});
            const int next = waveIndex_ + 1;
            if (next >= static_cast<int>(waves_.size())) {
                phase_ = WavePhase::Completed;
                emitLifecycle(out, "scheduleCompleted", {}, next, 0, {});
                break;
            }
            waveIndex_     = next;
            remainingPrep_ = waves_[static_cast<std::size_t>(next)].prepSeconds;
            phase_         = WavePhase::BetweenWaves;
            continue;
        }

        auto& entry = wave.entries[static_cast<std::size_t>(entryIndex_)];
        if (spawnCooldown_ > budget) {
            spawnCooldown_ -= budget;
            break;
        }
        budget -= spawnCooldown_;
        spawnCooldown_ = 0.0;
        if (spawnsThisAdvance >= kMaxSpawnsPerAdvance) break;

        WaveSpawnEvent spawn;
        spawn.sequence    = ++eventSequence_;
        spawn.waveId      = wave.id;
        spawn.waveIndex   = waveIndex_;
        spawn.archetypeId = entry.archetypeId;
        spawn.entryIndex  = entryIndex_;
        spawn.spawnIndex  = spawnedInEntry_;
        out.spawns.push_back(std::move(spawn));
        ++spawnsThisAdvance;
        ++spawnedInEntry_;
        if (spawnedInEntry_ >= entry.count) {
            ++entryIndex_;
            spawnedInEntry_ = 0;
            if (entryIndex_ < static_cast<int>(wave.entries.size()))
                spawnCooldown_ = wave.entries[static_cast<std::size_t>(entryIndex_)].delaySeconds;
        } else {
            spawnCooldown_ = entry.intervalSeconds;
        }
        // Zero-interval batches may consume no dt; stop once the time budget is exhausted.
        if (budget <= 0.0 && spawnCooldown_ <= 0.0) break;
    }

    out.phase = phase_;
    return Result<WaveAdvanceResult>::success(std::move(out));
}

DefenseBase::DefenseBase(int lives) : lives_(lives < 0 ? 0 : lives) {}

Result<int> DefenseBase::leak(int count) {
    if (count <= 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "leak count must be positive",
                                                      "leak.count", {}, "towerdefense"));
    leaks_ += count;
    if (lives_ > count)
        lives_ -= count;
    else
        lives_ = 0;
    return Result<int>::success(lives_);
}

Result<void> DefenseBase::reset(int lives) {
    if (lives < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lives must be >= 0", "base.lives", {}, "towerdefense"));
    lives_ = lives;
    leaks_ = 0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Module_IMPL(TowerDefense, new TowerDefense());

void TowerDefense::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();

    auto director = table.addClass<WaveDirector>(
        "WaveDirector", std::function<WaveDirector*()>([]() -> WaveDirector* { return new WaveDirector(); }), true);
    director.addFunc("clear", [](WaveDirector* value) {
        if (value) value->clear();
    });
    director.addFunc("waveCount", [](WaveDirector* value) { return value ? value->waveCount() : 0; });
    director.addFunc("phaseName", [](WaveDirector* value) { return value ? value->phaseName() : std::string{}; });
    director.addFunc("waveIndex", [](WaveDirector* value) { return value ? value->waveIndex() : 0; });
    director.addFunc("remainingPrepSeconds", [](WaveDirector* value) {
        return value ? static_cast<float>(value->remainingPrepSeconds()) : 0.f;
    });
    director.addFunc("loadScheduleJson", [vm](WaveDirector* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm,
                Result<int>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "wave director must not be null", "director", {}, "towerdefense")),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return eve::script::projectResult(vm, value->loadScheduleJson(json),
                                          [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
    });
    director.addFunc("start", [vm](WaveDirector* value) {
        if (!value)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "wave director must not be null",
                                                      "director", {}, "towerdefense")));
        return eve::script::projectResult(vm, value->start());
    });
    director.addFunc("reset", [vm](WaveDirector* value) {
        if (!value)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "wave director must not be null",
                                                      "director", {}, "towerdefense")));
        return eve::script::projectResult(vm, value->reset());
    });
    director.addFunc("advance", [vm](WaveDirector* value, std::int64_t tick, float dt) {
        if (!value)
            return eve::script::projectResult(
                vm,
                Result<WaveAdvanceResult>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "wave director must not be null", "director", {}, "towerdefense")),
                [](const WaveAdvanceResult& result) { return advanceResultValue(result); });
        return eve::script::projectResult(vm, value->advance(static_cast<std::uint64_t>(tick), static_cast<double>(dt)),
                                          [](const WaveAdvanceResult& result) { return advanceResultValue(result); });
    });

    auto base = table.addClass<DefenseBase>(
        "DefenseBase",
        std::function<DefenseBase*(int)>([](int lives) -> DefenseBase* { return new DefenseBase(lives); }), true);
    base.addFunc("lives", [](DefenseBase* value) { return value ? value->lives() : 0; });
    base.addFunc("leaks", [](DefenseBase* value) { return value ? value->leaks() : 0; });
    base.addFunc("isDefeated", [](DefenseBase* value) { return value && value->isDefeated(); });
    base.addFunc("leak", [vm](DefenseBase* value, int count) {
        if (!value)
            return eve::script::projectResult(
                vm,
                Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "defense base must not be null",
                                                       "base", {}, "towerdefense")),
                [](int lives) { return eve::Value(static_cast<std::int64_t>(lives)); });
        return eve::script::projectResult(vm, value->leak(count),
                                          [](int lives) { return eve::Value(static_cast<std::int64_t>(lives)); });
    });
    base.addFunc("reset", [vm](DefenseBase* value, int lives) {
        if (!value)
            return eve::script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "defense base must not be null", "base", {}, "towerdefense")));
        return eve::script::projectResult(vm, value->reset(lives));
    });

    auto cls = table.addClass(name, TowerDefense::create, false);
    expose(cls);
}

void TowerDefense::expose(ssq::Class& cls) {
    cls.addFunc("newDirector", [](TowerDefense*) -> WaveDirector* { return new WaveDirector(); });
    cls.addFunc("newBase", [](TowerDefense*, int lives) -> DefenseBase* { return new DefenseBase(lives); });
}

}  // namespace eve::towerdefense
