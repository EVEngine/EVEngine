#include <ECS.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#ifndef ECS_BENCH_ENGINE_REV
#define ECS_BENCH_ENGINE_REV "unknown"
#define ECS_BENCH_CORE_REV   "unknown"
#endif
#ifndef ECS_BENCH_HEADER_SHA256
#define ECS_BENCH_HEADER_SHA256 "unknown"
#endif

namespace {
using Clock         = std::chrono::steady_clock;
constexpr double dt = 1.0 / 64.0;

struct Position {
    double x = 0, y = 0;
};
struct Velocity {
    double x = 0, y = 0;
};
struct Health {
    double value = 1000;
};
struct Energy {
    double value = 0.125;
};
struct Cold {
    std::array<std::uint64_t, 32> words{};
};

class Unit : public ecs::Entity {
public:
    ENTITY(Unit, ecs::Entity)
    COMPONENT(Position, position)
    COMPONENT(Velocity, velocity)
    COMPONENT(Health, health)
    COMPONENT(Energy, energy)
    COMPONENT(Cold, cold)
    std::uint64_t seed = 0;
    void          release() override { ecs::DestroyEntity(this); }
};
class Soldier : public Unit {
public:
    ENTITY(Soldier, Unit)
};
class Vehicle : public Unit {
public:
    ENTITY(Vehicle, Unit)
};

struct Options {
    std::size_t entities   = 1000000;
    std::size_t samples    = 21;
    std::size_t warmup     = 5;
    std::size_t iterations = 5;
    std::string scenario   = "dense";
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t number(std::string_view value) {
    std::size_t result      = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    require(error == std::errc{} && end == value.data() + value.size(), "invalid numeric option");
    return result;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--help") {
            std::cout << "ecs_benchmark [--entities N] [--samples N] [--warmup N] [--iterations N]\n"
                         "  --scenario dense|dense4|cold|holes50|holes90|refill|inheritance|deferred|vector|prepare\n"
                         "One scenario per process. CSV goes to stdout; metadata to stderr.\n";
            std::exit(0);
        }
        require(i + 1 < argc, "missing option value");
        const std::string_view value(argv[++i]);
        if (key == "--entities")
            options.entities = number(value);
        else if (key == "--samples")
            options.samples = number(value);
        else if (key == "--warmup")
            options.warmup = number(value);
        else if (key == "--iterations")
            options.iterations = number(value);
        else if (key == "--scenario")
            options.scenario = value;
        else
            throw std::runtime_error("unknown option: " + std::string(key));
    }
    const std::array<std::string_view, 10> scenarios{"dense",  "dense4",      "cold",     "holes50", "holes90",
                                                     "refill", "inheritance", "deferred", "vector",  "prepare"};
    require(std::find(scenarios.begin(), scenarios.end(), options.scenario) != scenarios.end(), "unknown scenario");
    require(options.entities > 0 && options.entities <= 10000000, "entities must be in [1,10000000]");
    require(options.samples > 0 && options.samples <= 10000, "samples must be in [1,10000]");
    require(options.warmup <= 10000 && options.iterations > 0 && options.iterations <= 10000,
            "warmup/iterations out of range");
    return options;
}

double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::int64_t rssBytes() {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::int64_t  total = 0, resident = 0;
    const auto    page = sysconf(_SC_PAGESIZE);
    if (page > 0 && (statm >> total >> resident)) return resident * page;
#endif
    return -1;
}

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1];
}

void emit(const Options& o, std::string_view metric, std::size_t live, std::size_t operations,
          const std::vector<double>& samples) {
    const double median = percentile(samples, 0.5);
    const double rate   = median > 0 ? static_cast<double>(operations) / (median * 1000.0) : 0;
    std::cout << o.scenario << ',' << o.entities << ',' << live << ',' << metric << ',' << samples.size() << ','
              << std::fixed << std::setprecision(6) << median << ',' << percentile(samples, 0.95) << ','
              << percentile(samples, 0.99) << ',' << operations << ',' << rate << ',' << rssBytes() << '\n';
}

Position initialPosition(std::uint64_t seed) {
    return {static_cast<double>(seed % 1024), static_cast<double>(seed % 257)};
}
Velocity initialVelocity(std::uint64_t seed) { return {1.0 + static_cast<double>(seed % 7) * 0.25, -0.5}; }

void initialize(Unit* unit, std::uint64_t seed, bool wide, bool cold) {
    unit->seed        = seed;
    *unit->position() = initialPosition(seed);
    *unit->velocity() = initialVelocity(seed);
    if (wide) {
        *unit->health() = Health{};
        *unit->energy() = Energy{};
    }
    if (cold) unit->cold()->words.fill(seed);
}

// Contract: Unit and descendants; read Velocity (+ Energy), write Position (+ Health).
// No structure changes/events/services/callbacks. Single owner thread, explicit fixed dt.
// A fresh View scope per tick includes setup and automatic defer exit/commit in timing.
void update(ecs::Table& world, bool wide) {
    if (wide) {
        for (auto [p, v, h, e] : ecs::View<Unit, Position, Velocity, Health, Energy>(world)) {
            p->x += v->x * dt;
            p->y += v->y * dt;
            h->value -= e->value * dt;
        }
    } else {
        for (auto [p, v] : ecs::View<Unit, Position, Velocity>(world)) {
            p->x += v->x * dt;
            p->y += v->y * dt;
        }
    }
}

void checkPosition(const Position& p, std::uint64_t seed, std::size_t ticks) {
    const auto   start    = initialPosition(seed);
    const auto   velocity = initialVelocity(seed);
    const double time     = static_cast<double>(ticks) * dt;
    require(
        std::abs(p.x - (start.x + velocity.x * time)) < 1e-8 && std::abs(p.y - (start.y + velocity.y * time)) < 1e-8,
        "position checksum mismatch");
}

template <class Update>
void measure(const Options& o, std::size_t live, Update&& tick, std::string_view metric = "update_tick") {
    for (std::size_t i = 0; i < o.warmup; ++i) tick();
    std::vector<double> samples;
    samples.reserve(o.samples);
    for (std::size_t s = 0; s < o.samples; ++s) {
        const auto start = Clock::now();
        for (std::size_t i = 0; i < o.iterations; ++i) tick();
        samples.push_back(milliseconds(start) / static_cast<double>(o.iterations));
    }
    emit(o, metric, live, metric == "view_prepare" ? 0 : live, samples);
}

void runVector(const Options& o) {
    const auto            start = Clock::now();
    std::vector<Position> positions(o.entities);
    std::vector<Velocity> velocities(o.entities);
    for (std::size_t i = 0; i < o.entities; ++i) {
        positions[i]  = initialPosition(i);
        velocities[i] = initialVelocity(i);
    }
    emit(o, "create_once", o.entities, o.entities, {milliseconds(start)});
    measure(o, o.entities, [&] {
        for (std::size_t i = 0; i < o.entities; ++i) {
            positions[i].x += velocities[i].x * dt;
            positions[i].y += velocities[i].y * dt;
        }
    });
    const auto ticks = o.warmup + o.samples * o.iterations;
    for (std::size_t i = 0; i < o.entities; ++i) checkPosition(positions[i], i, ticks);
    std::cerr << "validation=passed checked=" << o.entities << " ticks=" << ticks << '\n';
}

void runEcs(const Options& o) {
    const bool wide = o.scenario == "dense4";
    const bool cold = o.scenario == "cold";
    ecs::Table world;
    // Handles belong to this live Table and are never used after its destruction.
    std::vector<ecs::EntityHandle> handles;
    handles.reserve(o.entities);
    auto start = Clock::now();
    for (std::size_t i = 0; i < o.entities; ++i) {
        Unit* unit = nullptr;
        if (o.scenario == "inheritance" && i % 3 == 1)
            unit = Soldier::create(world);
        else if (o.scenario == "inheritance" && i % 3 == 2)
            unit = Vehicle::create(world);
        else
            unit = Unit::create(world);
        initialize(unit, i, wide, cold);
        handles.push_back(ecs::handle_of(unit));
    }
    emit(o, "create_once", o.entities, o.entities, {milliseconds(start)});

    std::size_t live = o.entities;
    if (o.scenario == "holes50" || o.scenario == "holes90" || o.scenario == "refill") {
        const std::size_t period = o.scenario == "holes90" ? 10 : 2;
        start                    = Clock::now();
        for (std::size_t i = 0; i < handles.size(); ++i) {
            if (i % period != 0) {
                ecs::DestroyEntity(ecs::try_get(handles[i]));
                --live;
            }
        }
        emit(o, "remove_once", live, o.entities - live, {milliseconds(start)});
        for (std::size_t i = 0; i < handles.size(); ++i)
            require((ecs::try_get(handles[i]) != nullptr) == (i % period == 0), "stale handle after removal");
        if (o.scenario == "refill") {
            start = Clock::now();
            for (std::size_t i = 1; i < o.entities; i += 2) {
                auto* replacement = Unit::create(world);
                initialize(replacement, i, wide, cold);
                ++live;
            }
            emit(o, "refill_once", live, o.entities / 2, {milliseconds(start)});
            for (std::size_t i = 1; i < handles.size(); i += 2)
                require(ecs::try_get(handles[i]) == nullptr, "old handle revived after slot reuse");
        }
    }

    if (o.scenario == "deferred") {
        const std::size_t              batch = std::max<std::size_t>(1, o.entities / 100);
        std::vector<ecs::EntityHandle> transient(batch);
        std::vector<double>            queueTimes, commitTimes;
        for (std::size_t s = 0; s < o.warmup + o.samples; ++s) {
            double queueMs = 0;
            start          = Clock::now();
            {
                ecs::ScopedDefer defer(world);
                for (std::size_t i = 0; i < batch; ++i) {
                    auto* staged = Unit::create(world);
                    initialize(staged, o.entities + i, false, false);
                    transient[i] = ecs::handle_of(staged);
                }
                for (std::size_t i = 0; i < batch; ++i) {
                    auto* entity = ecs::try_get(handles[i]);
                    require(entity != nullptr, "missing entity in deferred batch");
                    ecs::DestroyEntity(entity);
                }
                queueMs = milliseconds(start);
                start   = Clock::now();
            }
            const double commitMs = milliseconds(start);
            for (std::size_t i = 0; i < batch; ++i) {
                require(ecs::try_get(handles[i]) == nullptr, "deferred old handle revived");
                auto* published = ecs::try_get(transient[i]);
                require(published != nullptr && ecs::is_entity_visible(published), "staged handle failed to publish");
                handles[i] = transient[i];
            }
            if (s >= o.warmup) {
                queueTimes.push_back(queueMs);
                commitTimes.push_back(commitMs);
            }
        }
        emit(o, "deferred_queue_batch", live, batch * 2, queueTimes);
        emit(o, "deferred_commit_batch", live, batch * 2, commitTimes);
    }

    if (o.scenario == "prepare") {
        measure(o, live, [&] { auto view = ecs::View<Unit, Position, Velocity>(world); }, "view_prepare");
    } else {
        measure(o, live, [&] { update(world, wide); });
    }
    const auto  ticks    = o.scenario == "prepare" ? 0 : o.warmup + o.samples * o.iterations;
    std::size_t checked  = 0;
    double      checksum = 0;
    {
        auto view = ecs::View<Unit, Position, Velocity>(world);
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [entity, p, v] = it.row();
            checkPosition(*p, entity->seed, ticks);
            const auto expectedVelocity = initialVelocity(entity->seed);
            require(v->x == expectedVelocity.x && v->y == expectedVelocity.y, "read-only velocity changed");
            if (wide) {
                require(entity->health()->value == 1000.0 - 0.125 * dt * static_cast<double>(ticks),
                        "health checksum mismatch");
                require(entity->energy()->value == 0.125, "read-only energy changed");
            }
            if (cold)
                for (auto word : entity->cold()->words) require(word == entity->seed, "cold data changed");
            checksum += p->x + p->y;
            ++checked;
        }
    }
    require(checked == live, "view skipped or duplicated entities");
    start = Clock::now();
    {
        auto view = ecs::View<Unit, Position>(world);
        for (auto it = view.begin(); it != view.end(); ++it) ecs::DestroyEntity(it.entity());
    }
    emit(o, "destroy_deferred_once", 0, live, {milliseconds(start)});
    {
        auto empty = ecs::View<Unit, Position>(world);
        require(empty.begin() == empty.end(), "world not empty after destruction");
    }
    std::cerr << "validation=passed checked=" << checked << " ticks=" << ticks << " checksum=" << checksum << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        std::cerr << "schema=ecs-benchmark-v1 config=" << ECS_BENCH_CONFIG << " compiler=" <<
#if defined(__clang__)
            "clang " << __clang_version__
#elif defined(__GNUC__)
            "gcc " << __VERSION__
#elif defined(_MSC_VER)
            "msvc " << _MSC_VER
#else
            "unknown"
#endif
                  << " pointer_bytes=" << sizeof(void*) << " entity_bytes=" << sizeof(Unit)
                  << " handle_bytes=" << sizeof(ecs::EntityHandle) << " hot_payload_bytes="
                  << sizeof(Position) + sizeof(Velocity) +
                         (options.scenario == "dense4" ? sizeof(Health) + sizeof(Energy) : 0)
                  << " cold_payload_bytes=" << (options.scenario == "cold" ? sizeof(Cold) : 0)
                  << " warmup=" << options.warmup << " samples=" << options.samples
                  << " iterations=" << options.iterations << " dt=" << dt << '\n'
                  << "engine_revision=" << ECS_BENCH_ENGINE_REV << " ecs_revision=" << ECS_BENCH_CORE_REV
                  << " ecs_header_sha256=" << ECS_BENCH_HEADER_SHA256 << " rss_before_bytes=" << rssBytes() << '\n';
        std::cout << "scenario,requested,live,metric,samples,p50_ms,p95_ms,p99_ms,operations,million_ops_s,rss_bytes\n";
        if (options.scenario == "vector")
            runVector(options);
        else
            runEcs(options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "validation=failed error=" << error.what() << '\n';
        return 1;
    }
}
