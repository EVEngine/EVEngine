#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include "physics/bake/collapse/CollapseAnimation.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/collapse/CollapsePlayback.h"

// Separate process admission of a real producer asset, reused by standalone
// and manifest profiles. Their build targets define the verified link closure.
int main(int argc, char** argv) {
    using namespace eve::physics;
    if (argc != 2) {
        std::cerr << "usage: physics_bake_inspect asset.evecollapse\n";
        return 2;
    }
    auto loaded = CollapseAnimation::load(argv[1]);
    if (!loaded) {
        std::cerr << loaded.status().describe() << '\n';
        return 1;
    }
    auto prepared = CollapsePlayback::prepare(std::move(loaded).takeValue());
    if (!prepared) {
        std::cerr << prepared.status().describe() << '\n';
        return 1;
    }
    auto player = std::move(prepared).takeValue();
    auto other  = player;
    other.setPlacement({10, 0, 0}).expect("second instance placement");
    const auto    clip    = player.animation();
    double        sum     = 0;
    const auto    begin   = std::chrono::steady_clock::now();
    constexpr int Queries = 120;
    for (int frame = 0; frame < Queries; ++frame) {
        player.seek(double(frame) * clip->duration() / (Queries - 1)).expect("forward seek");
        other.seek(double(Queries - 1 - frame) * clip->duration() / (Queries - 1)).expect("reverse seek");
        for (int bone = 0; bone < clip->boneCount(); ++bone) {
            auto first  = player.sample(bone);
            auto second = other.sample(bone);
            if (!first || !second) {
                if (!first) std::cerr << first.status().describe() << '\n';
                if (!second) std::cerr << second.status().describe() << '\n';
                return 1;
            }
            sum += double(first.value().worldX) + first.value().worldY + first.value().worldZ;
            sum += double(second.value().worldX) + second.value().worldY + second.value().worldZ;
        }
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    if (!std::isfinite(sum)) return 1;
    std::error_code error;
    const auto      size = std::filesystem::file_size(argv[1], error);
    if (error) {
        std::cerr << error.message() << '\n';
        return 1;
    }
    std::cout << "bones=" << clip->boneCount() << " frames=" << clip->frameCount() << " duration_s=" << clip->duration()
              << " file_bytes=" << size << " sampled_poses=" << Queries * 2LL * clip->boneCount()
              << " sampling_ms=" << elapsed << " pose_sum=" << sum << '\n';
    if (auto recipe = clip->recipe()) {
        auto key = recipe->key();
        if (!key) {
            std::cerr << key.status().describe() << '\n';
            return 1;
        }
        std::cout << "recipe=" << key.value() << '\n';
    } else {
        std::cout << "recipe=unidentified-legacy\n";
    }
    return 0;
}
