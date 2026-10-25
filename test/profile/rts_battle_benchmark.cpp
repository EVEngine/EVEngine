// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "performance",
  "module": "crowd"
}
*/
// clang-format on

#include "common/CrashHandler.h"
#include "common/ECS.h"
#include "common/Profile.h"
#include "crowd/CrowdTypes.h"
#include "crowd/CrowdWorkScheduler.h"
#if defined(EV_RTS_BENCH_THREAD)
#include "crowd/thread/CrowdThread.h"
#include "thread/JobSystem.h"
#endif
#include "rts/RTS.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <class T>
T require(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
void require(eve::Result<void> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
}
eve::SubjectRef identity(int value) {
    char text[37];
    std::snprintf(text, sizeof(text), "00000000-0000-7000-8000-%012x", static_cast<unsigned>(value));
    auto id = eve::PersistentId::parse(text);
    if (!id) throw std::runtime_error("invalid scenario identity");
    return eve::SubjectRef::fromPersistentId(*id);
}
bool positive(const char* text, int& value) {
    const std::string input(text);
    auto              parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size() && value > 0;
}
}  // namespace
int main(int argc, char** argv) {
#if defined(_WIN32)
    eve::installCrashHandler();
#endif
    int        count = 128, ticks = 300, workers = 1;
    const bool melee    = argc >= 4 && std::string(argv[3]) == "melee";
    const bool journey  = argc >= 4 && std::string(argv[3]) == "grouped-corridor";
    const bool corridor = journey || (argc >= 4 && std::string(argv[3]) == "corridor");
    const bool grouped  = journey || (argc >= 4 && std::string(argv[3]) == "grouped");
    const bool shared   = melee || grouped || corridor || (argc >= 4 && std::string(argv[3]) == "shared");
    if (argc > 5 || (argc >= 4 && !shared) || (argc > 1 && !positive(argv[1], count)) ||
        (argc > 2 && !positive(argv[2], ticks)) || (argc > 4 && !positive(argv[4], workers)) ||
        (workers != 1 && workers != 4) || count < 2 || count > 10000 || ticks > 36000) {
        std::cerr << "usage: eve_rts_battle_benchmark [agents:2..10000] [ticks:1..36000] "
                     "[shared|corridor|grouped|grouped-corridor|melee] [workers:1|4]\n";
        return 2;
    }
    try {
#if defined(EV_RTS_BENCH_THREAD)
        std::shared_ptr<eve::thread::JobSystem> jobs;
#endif
        ecs::Table       world;
        ecs::ScopedTable guard(world);
        eve::rts::RTS    sim;
        const int        rows = (count + 15) / 16;
        require(sim.configureScriptWorld(64, rows + 8, 1.f));
#if defined(EV_RTS_BENCH_THREAD)
        if (workers == 4) {
            jobs = std::shared_ptr<eve::thread::JobSystem>(eve::thread::createJobSystem(4));
            auto scheduler = require(eve::crowd_thread::makeJobSystemCrowdScheduler(jobs));
            require(sim.configureScriptCrowdExecution(eve::crowd::CrowdExecution::Parallel, std::move(scheduler)));
        }
#else
        if (workers != 1) return 2;
#endif
        if (corridor) {
            for (int y = 0; y < rows + 8; ++y) {
                if (y != rows / 2 + 4) require(sim.setScriptNavigationBlocked(27, y, true));
            }
        }
        if (shared) {
            eve::rts::CrowdSurfaceTracking settings;
            settings.surface          = eve::crowd::NavigationSurfaceId(1);
            settings.movement         = eve::crowd::MovementClassId(1);
            settings.movementRevision = settings.costModelRevision = 1;
            settings.clearanceRadius                               = 0.35f;
            require(sim.configureCrowdSurfaceTracking(settings));
            require(sim.configureCrowdFlowNavigation({32768, 4}));
        }
        std::string content = R"({
          "weapons":[{"id":"rifle","damageType":"normal","damage":10,"range":6,
            "cooldown":0.2,"projectileSpeed":0,"blockedByObstacles":true,"magazineSize":1000,"reloadTime":1}],
          "units":[{"id":"soldier","role":"soldier","weaponType":"rifle","health":40,
            "speed":3,"radius":0.35,"attackRange":6,"sightRange":10,"targetTags":["biological"]}]
        })";
        if (melee) {
            for (const std::string token : {std::string("\"range\":6"), std::string("\"attackRange\":6")}) {
                const auto position = content.find(token);
                if (position == std::string::npos) throw std::runtime_error("missing melee content field");
                content.replace(position, token.size(), token.substr(0, token.size() - 1) + "1.2");
            }
        }
        require(sim.loadScriptContent(content));
        auto*      blue       = require(sim.newFaction(identity(1)));
        auto*      red        = require(sim.newFaction(identity(2)));
        const auto definition = eve::LogicalId::parse("unit:soldier");
        if (!definition) throw std::runtime_error("invalid unit definition");
        std::array<std::vector<eve::SubjectRef>, 2> armies;
        for (int i = 0; i < count; ++i) {
            const bool right  = (i % 2) != 0;
            const int  rank   = i / 2;
            auto*      unit   = require(sim.newFactionUnit(right ? *red : *blue, identity(i + 3), *definition));
            unit->motion()->x = right ? 34.f + float(rank % 8) : 20.f - float(rank % 8);
            unit->motion()->y = 4.f + float(rank / 8);
            eve::rts::CommandSpec order;
            order.kind   = eve::rts::OrderKind::AttackMove;
            order.target = {right ? 18.f : 36.f, unit->motion()->y};
            if (grouped)
                armies[right ? 1 : 0].push_back(identity(i + 3));
            else
                require(unit->orders()->values.enqueue(order));
        }
        if (grouped) {
            for (std::size_t side = 0; side < armies.size(); ++side) {
                for (std::size_t offset = 0; offset < armies[side].size(); offset += 512) {
                    const auto                   end = std::min(offset + 512, armies[side].size());
                    eve::rts::MovementGroupBatch batch;
                    batch.units.assign(armies[side].begin() + offset, armies[side].begin() + end);
                    batch.kind   = eve::rts::OrderKind::AttackMove;
                    batch.target = {side == 1 ? 18.f : 36.f,
                                    4.f + float(offset / 8) + float((end - offset + 7) / 8 - 1) * 0.5f};
                    batch.formation = {eve::rts::FormationKind::Grid, 1.f, 8};
                    require(sim.submitMovementGroup(batch));
                }
            }
        }
        std::uint64_t fired = 0, damage = 0, deaths = 0;
        eve::prof::Profiler::setEnabled(true);
        std::cout << "# path=RTS::stepScript,scene=two-armies,agents=" << count
                  << ",dt=0.05,seed=none,render=none,grouped=" << grouped << ",corridor=" << corridor
                  << ",melee=" << melee << ",shared_flow=" << shared << "\n";
        std::cout << "tick,step_ms,crowd_working_ms,crowd_motion_ms,combat_fire_ms,remaining_step_ms,weapon_fired,"
                     "damage,death\n"
                  << std::flush;
        for (int tick = 1; tick <= ticks; ++tick) {
            const auto start = std::chrono::steady_clock::now();
            require(sim.stepScript(0.05));
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            eve::prof::Profiler::frameMark();
            if (tick == 2 || tick == 20 || tick == 60) std::cerr << eve::prof::Profiler::textReport() << '\n';
            double crowdMs = 0.0, motionMs = 0.0, combatMs = 0.0;
            for (const auto& sample : eve::prof::Profiler::lastFrame()) {
                if (sample.module == "crowd" && sample.name == "Crowd::advance") crowdMs += sample.totalMs;
                if (sample.module == "rts" && sample.name == "CrowdMotionSystem::step") motionMs += sample.totalMs;
                if (sample.module == "rts" && sample.name == "CombatFireSystem::step") combatMs += sample.totalMs;
            }
            const auto  projection = sim.inspectFrameEvents();
            const auto* events     = projection.getIf<eve::Value::Array>();
            if (!events) throw std::runtime_error("missing event projection");
            for (const auto& event : *events) {
                const auto* object = event.getIf<eve::Value::Object>();
                if (!object) continue;
                const auto found = object->find("type");
                if (found == object->end()) continue;
                const auto* type = found->second.getIf<std::string>();
                if (!type) continue;
                if (*type == "weapon_fired") ++fired;
                if (*type == "damage") {
                    ++damage;
                    const auto killed = object->find("killed");
                    if (killed != object->end()) {
                        const auto* value = killed->second.getIf<bool>();
                        if (value && *value) ++deaths;
                    }
                }
            }
            std::cout << tick << ',' << ms << ',' << crowdMs << ',' << motionMs << ',' << combatMs << ','
                      << (ms - motionMs - combatMs) << ',' << fired << ',' << damage << ',' << deaths << '\n'
                      << std::flush;
        }
        std::cout << "# completed_ticks=" << sim.scriptTick() << ",weapon_fired=" << fired << ",damage=" << damage
                  << ",death=" << deaths << ",remaining_units=" << sim.unitCount() << '\n';
        if (journey) {
            const auto snapshot = require(sim.snapshotState());
            std::cout << "# remaining_groups=" << snapshot.movementGroups.size() << '\n';
            for (const auto& saved : snapshot.units) {
                auto* unit = sim.findUnit(saved.subject);
                if (!unit) continue;
                auto current = unit->orders()->values.current();
                std::cout << "# unit=" << saved.subject.format() << ",x=" << unit->motion()->x
                          << ",y=" << unit->motion()->y << ",arrived=" << unit->motion()->arrived
                          << ",traffic=" << unit->navigation()->trafficWaiting
                          << ",unreachable=" << unit->navigation()->unreachable
                          << ",pacing=" << unit->navigation()->formationSpeedFactor;
                if (current.ok())
                    std::cout << ",goal_x=" << current.value().target.x << ",goal_y=" << current.value().target.y;
                else
                    current.ignore("Final observation of completed orders");
                std::cout << '\n';
            }

            if (!snapshot.movementGroups.empty())
                throw std::runtime_error("surviving groups have not reformed and arrived");
        }
        if (shared) {
            const auto flow = sim.getCrowdFlowNavigationStatus();
            std::cout << "# flow_prepared=" << flow.prepared << ",flow_cache_hits=" << flow.cacheHits
                      << ",flow_pending=" << flow.pendingGoals << '\n';
            if (((!grouped || corridor) && flow.prepared == 0) || flow.pendingGoals != 0)
                throw std::runtime_error("shared navigation did not complete its active requests");
        }
        if (fired == 0) {
            for (int sample=0;sample<std::min(count,16);++sample) {
                auto* unit=sim.findUnit(identity(sample+3));
                if(!unit) continue;
                auto current=unit->orders()->values.current();
                std::cout << "# stalled_sample=" << sample << ",x=" << unit->motion()->x << ",y=" << unit->motion()->y
                          << ",speed=" << unit->motion()->speed << ",pacing=" << unit->navigation()->formationSpeedFactor
                          << ",range=" << unit->combat()->engagementRange << ",waiting=" << unit->navigation()->trafficWaiting;
                if(current.ok()) std::cout << ",goal_x=" << current.value().target.x << ",goal_y=" << current.value().target.y;
                else current.ignore("Diagnostic observation of completed order");
                std::cout << '\n';
            }
        }
        if (fired == 0 || damage == 0 || deaths == 0 || sim.unitCount() >= static_cast<std::size_t>(count)) {
            std::cerr << "battle acceptance failed: no actual fire, damage or death\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "battle failed: " << error.what() << '\n';
        return 1;
    }
}
