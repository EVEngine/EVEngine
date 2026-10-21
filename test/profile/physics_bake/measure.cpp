#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "physics/bake/BakedParticlePlayback.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/RigidPlayback.h"

namespace {
using Clock = std::chrono::steady_clock;
using namespace eve::physics;
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
template <class Sample>
int measure(std::string_view kind, const char* path, int instances, double loadMs, double prepareMs, Sample sample) {
    constexpr int Queries = 2400;
    double        sum     = 0;
    std::size_t   values  = 0;
    // Warm the production sampling path before the measured interval.
    for (int i = 0; i < instances; ++i) sample(i, 0.5, sum);
    sum              = 0;
    const auto begin = Clock::now();
    for (int query = 0; query < Queries; ++query) {
        const double phase = double(query % 240) / 239;
        for (int i = 0; i < instances; ++i) values += sample(i, i % 2 ? 1 - phase : phase, sum);
    }
    const double elapsed = milliseconds(begin);
    if (!std::isfinite(sum)) throw std::runtime_error("non-finite sampled checksum");
    std::cout << "kind=" << kind << " instances=" << instances << " queries_per_instance=" << Queries
              << " file_bytes=" << std::filesystem::file_size(path) << " load_ms=" << loadMs
              << " prepare_ms=" << prepareMs << " sampling_ms=" << elapsed
              << " us_per_instance_query=" << elapsed * 1000 / (Queries * instances) << " sampled_values=" << values
              << " pose_sum=" << sum << '\n';
    return 0;
}
int run(std::string_view kind, const char* path, int count) {
    const auto loadStart = Clock::now();
    if (kind == "rigid") {
        auto                           track        = RigidPoseTrack::load(path).expect("load rigid");
        const double                   loadMs       = milliseconds(loadStart);
        const auto                     prepareStart = Clock::now();
        std::vector<eve::PersistentId> ids;
        for (std::size_t i = 0; i < track.trackCount(); ++i) ids.push_back(track.trackId(i).expect("identity"));
        std::vector<RigidPlayback> players;
        for (int i = 0; i < count; ++i) {
            players.emplace_back(track);
            players.back().setPlacement({float(i), 0, 0}).expect("placement");
        }
        const double prepareMs = milliseconds(prepareStart);
        return measure(kind, path, count, loadMs, prepareMs, [&](int i, double phase, double& sum) {
            auto& player = players[std::size_t(i)];
            player.seek(phase * track.duration()).expect("seek");
            for (auto id : ids) {
                const auto pose = player.sample(id).expect("sample");
                sum += double(pose.x) + pose.y + pose.z;
            }
            return ids.size();
        });
    }
    if (kind == "deform") {
        auto                        track        = DeformTrack::load(path).expect("load deform");
        const double                loadMs       = milliseconds(loadStart);
        const auto                  prepareStart = Clock::now();
        std::vector<DeformPlayback> players;
        std::vector<DeformPoint>    output(track.topology().vertexCount());
        for (int i = 0; i < count; ++i) {
            players.push_back(DeformPlayback::prepare(track).expect("prepare deform"));
            players.back().setPlacement({float(i), 0, 0}).expect("placement");
        }
        const double prepareMs = milliseconds(prepareStart);
        return measure(kind, path, count, loadMs, prepareMs, [&](int i, double phase, double& sum) {
            auto& player = players[std::size_t(i)];
            player.seek(phase * track.duration()).expect("seek");
            player.sample(output).expect("sample");
            for (const auto& point : output) sum += double(point.x) + point.y + point.z;
            return output.size();
        });
    }
    if (kind == "particles") {
        auto                               track        = ParticleTrack::load(path).expect("load particles");
        const double                       loadMs       = milliseconds(loadStart);
        const auto                         prepareStart = Clock::now();
        std::vector<BakedParticlePlayback> players;
        std::vector<BakedParticleSample>   output(track.capacity());
        for (int i = 0; i < count; ++i) {
            players.push_back(BakedParticlePlayback::prepare(track).expect("prepare particles"));
            players.back().setPlacement({float(i), 0, 0}).expect("placement");
        }
        const double prepareMs = milliseconds(prepareStart);
        return measure(kind, path, count, loadMs, prepareMs, [&](int i, double phase, double& sum) {
            auto& player = players[std::size_t(i)];
            player.seek(phase * track.duration()).expect("seek");
            const auto visible = player.sample(output).expect("sample");
            for (std::size_t j = 0; j < visible; ++j) sum += double(output[j].pose.x) + output[j].pose.y;
            return visible;
        });
    }
    throw std::runtime_error("kind must be rigid, deform or particles");
}
}  // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: physics_bake_measure rigid|deform|particles asset.bake instances(1..1024)\n";
        return 2;
    }
    int                    count = 0;
    const std::string_view input(argv[3]);
    const auto             parsed = std::from_chars(input.data(), input.data() + input.size(), count);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || count < 1 || count > 1024) return 2;
    try {
        return run(argv[1], argv[2], count);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
