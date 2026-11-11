#include "GameplayBenchmark.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "common/GpuTimer.h"
#include "common/Profile.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace eve::animation;
using namespace eve::graphics;
using namespace eve::scene;
using Clock           = std::chrono::steady_clock;
constexpr int   bones = 64, keys = 61, warmup = 60;
constexpr float pi = 3.14159265358979323846f;

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int setting(const char* name, int fallback, int low, int high) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    int                    result = 0;
    const std::string_view text(value);
    const auto             parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < low || result > high)
        throw std::runtime_error(std::string("Invalid benchmark setting: ") + name);
    return result;
}
struct Metric {
    std::vector<double> values;
    void                print(const char* name) const {
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto p = [&](int percent) { return sorted[(sorted.size() * percent + 99) / 100 - 1]; };
        std::cout << std::quoted(name)
                  << ":{\"meanMs\":" << std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size()
                  << ",\"p50Ms\":" << p(50) << ",\"p95Ms\":" << p(95) << ",\"p99Ms\":" << p(99) << '}';
    }
};

// A deterministic deformable fixture, not a claim about humanoid art complexity.
// Each actor owns a mesh because the production palette is mesh-owned.
struct Rig {
    AnimSkeleton               skeleton;
    AnimClip                   walk{"walk"}, turn{"turn"};
    std::unique_ptr<AnimSkin>  skin;
    std::vector<float>         positions, normals, uv;
    std::vector<std::uint32_t> indices;
    explicit Rig(int influences) {
        for (int b = 0; b < bones; ++b) {
            skeleton.addBone("bone" + std::to_string(b), b ? (b - 1) / 2 : -1);
            skeleton.setBindPosition(b, 0, b ? .025f : 0.f, 0);
        }
        for (auto* clip : {&walk, &turn}) {
            clip->setDuration(2.f);
            clip->setLoop(true);
            for (int b = 0; b < bones; ++b) {
                // Imported rigs commonly retain dense rotations but sparse translation/scale tracks.
                clip->addPositionKey(b, 0, 0, b ? .025f : 0.f, 0);
                clip->addScaleKey(b, 0, 1, 1, 1);
                if (b && b % 8 == 0) {
                    clip->addPositionKey(b, 1, .01f, .025f, 0);
                    clip->addPositionKey(b, 2, 0, .025f, 0);
                }
                for (int k = 0; k < (b ? keys : 1); ++k) {
                    const float t     = float(k) / 30.f;
                    const float angle = b ? .12f * std::sin(pi * t + float(b % 7)) : 0.f;
                    clip->addRotationKey(b, t, clip == &walk ? std::sin(angle / 2) : 0.f,
                                         clip == &turn ? std::sin(angle / 2) : 0.f, 0, std::cos(angle / 2));
                }
            }
        }
        AnimPose bind;
        skeleton.applyBindPose(&bind);
        bind.computeWorld(&skeleton);
        AnimSkinStreamData streams;
        for (int b = 0; b < bones; ++b) {
            streams.skeletonBones.push_back(b);
            streams.boneNames.push_back("bone" + std::to_string(b));
            std::array<float, 16> inverse{};
            inverse[0] = inverse[5] = inverse[10] = inverse[15] = 1.f;
            inverse[13]                                         = -bind.getWorldPositionY(b);
            streams.inverseBindMatrices.push_back(inverse);
        }
        constexpr int sides = 32;
        for (int ring = 0; ring < bones; ++ring) {
            for (int side = 0; side < sides; ++side) {
                const float angle = 2 * pi * float(side) / sides;
                positions.insert(positions.end(),
                                 {.35f * std::cos(angle), 2.f * ring / (bones - 1), .35f * std::sin(angle)});
                normals.insert(normals.end(), {std::cos(angle), 0.f, std::sin(angle)});
                uv.insert(uv.end(), {float(side) / sides, float(ring) / (bones - 1)});
                const std::array<float, 4> weights = influences == 1   ? std::array{1.f, 0.f, 0.f, 0.f}
                                                     : influences == 2 ? std::array{.75f, .25f, 0.f, 0.f}
                                                     : influences == 3 ? std::array{.5f, .3f, .2f, 0.f}
                                                                       : std::array{.4f, .3f, .2f, .1f};
                for (int influence = 0; influence < 4; ++influence) {
                    streams.vertexSkinJoints.push_back(std::max(0, ring - influence));
                    streams.vertexWeights.push_back(weights[influence]);
                }
                if (ring + 1 < bones) {
                    const auto a = std::uint32_t(ring * sides + side);
                    const auto b = std::uint32_t(ring * sides + (side + 1) % sides);
                    indices.insert(indices.end(), {a, a + sides, b, b, a + sides, b + sides});
                }
            }
        }
        streams.vertexCount   = int(positions.size() / 3);
        streams.bindPositions = positions;
        streams.bindNormals   = normals;
        skin                  = AnimSkin::fromStreams(std::move(streams)).expect("benchmark skin streams");
        require(skin->updateMatrixPalette(&bind), "bind palette failed");
        for (int b = 0; b < bones; ++b)
            for (int e = 0; e < 16; ++e)
                require(std::abs(skin->getMatrixPaletteElement(b, e) - (e % 5 == 0 ? 1.f : 0.f)) < 1e-5f,
                        "bind palette is not identity");
    }
};

struct Actor {
    std::unique_ptr<AnimPlayer> player;
    Mesh*                       mesh = nullptr;  // Graphics-owned through benchmark shutdown.
};
struct SceneFixture {
    SceneHost*                 host   = nullptr;  // ECS-owned, released before the table/Graphics.
    Camera3D*                  camera = nullptr;
    Light3D*                   light  = nullptr;
    std::vector<Renderable3D*> renderables;
    std::vector<int>           linkedNodes, movingGroups;
    std::unique_ptr<Rig>       rig;
    std::vector<Actor>         actors;
    float                      span = 0;

    SceneFixture(Graphics& gfx, std::size_t count, int characters, bool moving, bool shadows, int influences) {
        const int side = int(std::ceil(std::sqrt(double(count))));
        span           = float(side) * 3.f;
        host           = SceneHost::createHost("gameplay-benchmark").expect("benchmark host");
        NodeDesc root;
        root.id = "root";
        for (std::size_t first = 0; first < count; first += 16) {
            NodeDesc group;
            group.id = "group" + std::to_string(first / 16);
            for (std::size_t i = first; i < std::min(count, first + 16); ++i) {
                NodeDesc prop;
                prop.id = "prop" + std::to_string(i);
                prop.x  = float(i % side) * 3.f - span / 2;
                prop.z  = float(i / side) * 3.f - span / 2;
                group.children.push_back(std::move(prop));
            }
            root.children.push_back(std::move(group));
        }
        for (int i = 0; i < characters; ++i) {
            NodeDesc actor;
            actor.id = "actor" + std::to_string(i);
            actor.x  = float(i % 8) * 2.f - 7.f;
            actor.z  = float(i / 8) * 2.f;
            root.children.push_back(std::move(actor));
        }
        host->setTree(std::move(root));
        auto* cube   = gfx.newMeshCube();
        auto* sphere = gfx.newMeshSphere(16, 8);
        require(cube && sphere, "prop mesh creation failed");
        std::array<Texture*, 8> textures{};
        for (int i = 0; i < 8; ++i) {
            const std::uint8_t color[] = {std::uint8_t(60 + i * 20), std::uint8_t(180 - i * 12), 160, 255};
            textures[i]                = gfx.newTexture(1, 1, color);
            require(textures[i] != nullptr, "material texture creation failed");
        }
        const auto link = [&](const std::string& id, Mesh* mesh, int material) {
            auto* entity = Renderable3D::create();
            renderables.push_back(entity);
            entity->setMesh(mesh);
            entity->setTexture(textures[material % textures.size()]);
            entity->setRoughness(.3f + .05f * (material % 8));
            require(host->linkRenderable3D(id, entity), "scene/graphics link missing");
            const int node = host->findIndexById(id);
            require(node >= 0, "scene fixture node missing");
            linkedNodes.push_back(node);
        };
        for (std::size_t i = 0; i < count; ++i) link("prop" + std::to_string(i), i % 4 ? cube : sphere, int(i % 8));
        if (moving)
            for (std::size_t i = 0; i < (count + 15) / 16; i += 10)
                movingGroups.push_back(host->findIndexById("group" + std::to_string(i)));
        if (characters) rig = std::make_unique<Rig>(influences);
        for (int i = 0; i < characters; ++i) {
            Actor actor;
            actor.mesh =
                gfx.newMeshFromArrays(rig->positions.data(), rig->normals.data(), rig->uv.data(),
                                      int(rig->positions.size() / 3), rig->indices.data(), int(rig->indices.size()));
            require(actor.mesh && rig->skin->bindGpuMesh(&gfx, actor.mesh), "GPU skin binding failed");
            actor.player = std::make_unique<AnimPlayer>(&rig->skeleton);
            actor.player->play(&rig->walk);
            actor.player->setTime(float(i % 30) / 30.f);
            actor.player->setSpeed(1.f + .05f * float(i % 5));
            actor.player->getPose()->computeWorld(&rig->skeleton);
            require(rig->skin->updateGpuMesh(actor.mesh, actor.player->getPose()), "initial palette failed");
            link("actor" + std::to_string(i), actor.mesh, i);
            actors.push_back(std::move(actor));
        }
        camera                  = Camera3D::createCamera();
        camera->data()->nearZ   = .1f;
        camera->data()->farZ    = span * 3 + 100;
        camera->data()->fovYDeg = 60.f;
        light                   = Light3D::createLight("dir");
        light->setDirection(.35f, 1.f, .25f);
        light->setColor(1.f, .97f, .92f, 2.f);
        light->setCastShadow(shadows);
        TransformSystem::updateHost(host);
        validate();
    }
    ~SceneFixture() {
        if (host) ecs::DestroyEntity(host);
        for (auto* entity : renderables) ecs::DestroyEntity(entity);
        if (camera) ecs::DestroyEntity(camera);
        if (light) ecs::DestroyEntity(light);
    }
    void edit(int frame) {
        const float phase = float(frame) / 60.f;
        for (int index : movingGroups) {
            require(index >= 0, "moving group missing");
            auto& node = host->tree()->nodes[std::size_t(index)];
            node.y     = .5f * std::sin(phase + float(index));
            node.yaw   = .03f * std::sin(phase);
            host->markSubtreeDirty(index);
        }
        camera->setEye(std::sin(phase * .2f) * span * .25f, span * .3f + 5.f, span * .65f);
        camera->setTarget(0.f, 0.f, 0.f);
    }
    std::array<double, 3> animate(int frame) {
        EV_PROFILE_MODULE("animation", "Benchmark::sampleAndPublish");
        const eve::SimulationStep step{eve::SimulationTick{std::uint64_t(frame + 1)},
                                       eve::Duration::fromNanoseconds(16666667)};
        auto                      start = Clock::now();
        for (auto& actor : actors) {
            // Recurring transitions keep blending active during warmup and measured frames.
            if (frame && frame % 120 == 0) actor.player->crossFade(frame % 240 ? &rig->turn : &rig->walk, .3f);
            actor.player->advance(step).expect("benchmark animation advance");
        }
        const double sampling = elapsed(start);
        start                 = Clock::now();
        for (auto& actor : actors) actor.player->getPose()->computeWorld(&rig->skeleton);
        const double worlds = elapsed(start);
        start               = Clock::now();
        for (auto& actor : actors) {
            require(rig->skin->updateGpuMesh(actor.mesh, actor.player->getPose()), "palette publication failed");
        }
        return {sampling, worlds, elapsed(start)};
    }
    std::uint64_t validate() const {
        require(host->getNodeCount() >= int(renderables.size()), "scene lost nodes");
        std::uint64_t hash    = 14695981039346656037ull;
        const auto    consume = [&](float value) {
            require(std::isfinite(value), "nonfinite scene or animation output");
            hash = (hash ^ std::bit_cast<std::uint32_t>(value)) * 1099511628211ull;
        };
        for (std::size_t i = 0; i < renderables.size(); ++i) {
            const auto& node      = host->tree()->nodes[std::size_t(linkedNodes[i])];
            const auto  transform = renderables[i]->transform();
            require(transform->hasLinkedWorld, "scene did not publish world matrix");
            // Fixture parents have only Y translation and yaw; verify against an independent
            // closed-form result, not merely two consumers of the same cached matrix.
            const auto& parent = host->tree()->nodes[std::size_t(node.parent)];
            const float c = std::cos(parent.yaw), s = std::sin(parent.yaw);
            require(std::abs(node.world[3][0] - (c * node.x + s * node.z)) < 1e-3f &&
                        std::abs(node.world[3][1] - (node.y + parent.y)) < 1e-3f &&
                        std::abs(node.world[3][2] - (-s * node.x + c * node.z)) < 1e-3f,
                    "scene hierarchy produced an incorrect world position");
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) {
                    require(transform->linkedWorld[c][r] == node.world[c][r], "stale linked transform");
                    consume(node.world[c][r]);
                }
        }
        for (const auto& actor : actors) {
            require(actor.player->isPlaying(), "animation became idle during the benchmark");
            require(actor.mesh->hasGpuSkinning() && actor.mesh->getSkinPaletteCount() == bones,
                    "character lost its published skin palette");
            for (int b = 0; b < bones; ++b)
                for (int e = 0; e < 16; ++e) consume(actor.player->getPose()->getWorldMatrixElement(b, e));
            for (float value : actor.mesh->skinPalette()) consume(value);
        }
        return hash;
    }
};
}  // namespace

int runGameplayBenchmark(eve::graphics::Graphics& gfx, std::string_view workload, int frames, std::size_t props) {
    const bool moving   = workload == "3d-gameplay-dynamic" || workload == "3d-gameplay-mixed";
    const bool animated = workload == "3d-gameplay-animated" || workload == "3d-gameplay-mixed";
    require(moving || animated || workload == "3d-gameplay-static", "unknown gameplay workload");
    require(props >= 1 && props <= 100000 && frames >= 9 && frames <= 36000, "gameplay size out of range");
    const int  characters = animated ? setting("EVENGINE_BENCH_CHARACTERS", 32, 1, 256) : 0;
    const int  influences = setting("EVENGINE_BENCH_INFLUENCES", 2, 1, 4);
    const bool render     = setting("EVENGINE_BENCH_RENDER", 1, 0, 1) != 0;
    const bool shadows    = setting("EVENGINE_BENCH_SHADOWS", 0, 0, 1) != 0;
    const bool profile    = std::getenv("EVENGINE_BENCH_PROFILE") != nullptr;
    auto*      control    = gfx.getRenderControl();
    if (const char* lighting = std::getenv("EVENGINE_BENCH_LIGHTING")) {
        require(std::strcmp(lighting, "forwardPlus") == 0 || std::strcmp(lighting, "hybrid") == 0,
                "lighting must be forwardPlus or hybrid");
        require(control->setLightingMode(lighting).ok(), "lighting selection failed");
        control->compile();
        require(!control->hasHybridLightingFallback(), "requested lighting mode unavailable");
    }
    if (const char* msaa = std::getenv("EVENGINE_BENCH_MSAA"); msaa && std::strcmp(msaa, "0") == 0)
        control->disable("msaa");
    control->ensureCompiled();
    std::map<std::string, eve::prof::ZoneSample> zones;
    const auto                                   start = Clock::now();
    auto         scene = std::make_unique<SceneFixture>(gfx, props, characters, moving, shadows, influences);
    const double setup = elapsed(start);
    Metric       total, edits, animation, sampling, worlds, palettes, transforms, render3d, present, gpu;
    for (auto* metric :
         {&total, &edits, &animation, &sampling, &worlds, &palettes, &transforms, &render3d, &present, &gpu})
        metric->values.reserve(frames);
    double aggregation = 0;
    eve::prof::Profiler::setEnabled(profile);
    for (int frame = 0; frame < warmup + frames; ++frame) {
        const auto begin = Clock::now();
        scene->edit(frame);
        const double editMs         = elapsed(begin);
        auto         stage          = Clock::now();
        const auto   animationParts = scene->animate(frame);
        const double animationMs    = elapsed(stage);
        stage                       = Clock::now();
        TransformSystem::updateHost(scene->host);
        const double transformMs = elapsed(stage);
        stage                    = Clock::now();
        if (render) RenderSystem3D::render(gfx);
        const double renderMs = elapsed(stage);
        stage                 = Clock::now();
        if (render) RenderSystem::render(gfx);
        const double presentMs = elapsed(stage);
        SDL_Event    event;
        while (SDL_PollEvent(&event)) {
        }
        const double totalMs = elapsed(begin);
        if (frame >= warmup) {
            total.values.push_back(totalMs);
            edits.values.push_back(editMs);
            animation.values.push_back(animationMs);
            sampling.values.push_back(animationParts[0]);
            worlds.values.push_back(animationParts[1]);
            palettes.values.push_back(animationParts[2]);
            transforms.values.push_back(transformMs);
            render3d.values.push_back(renderMs);
            present.values.push_back(presentMs);
            if (render)
                if (auto* timer = eve::cap::query<eve::service::IGpuTimer>(); timer && timer->gpuTimingAvailable())
                    gpu.values.push_back(timer->gpuFrameMs());
        }
        if (profile) {
            const double cost = eve::prof::Profiler::frameMark();
            if (frame >= warmup) {
                aggregation += cost;
                for (const auto& zone : eve::prof::Profiler::lastFrame()) {
                    auto& sum  = zones[zone.module + "/" + zone.name + "/" + zone.thread];
                    sum.module = zone.module;
                    sum.name   = zone.name;
                    sum.thread = zone.thread;
                    sum.selfMs += zone.selfMs;
                    sum.totalMs += zone.totalMs;
                    sum.count += zone.count;
                }
            }
        }
    }
    eve::prof::Profiler::setEnabled(false);
    const auto checksum = scene->validate();
    const int  nodes    = scene->host->getNodeCount();
    if (const char* path = std::getenv("EVENGINE_BENCH_CAPTURE")) {
        require(render, "frame capture requires rendering");
        gfx.setScreenReadbackEnabled(true);
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(gfx);
            RenderSystem::render(gfx);
        }
        std::unique_ptr<eve::image::ImageData> pixels(gfx.newImageData());
        require(pixels && pixels->getSize() == std::size_t(gfx.getWidth()) * gfx.getHeight() * 4,
                "presented-frame readback failed");
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << gfx.getWidth() << ' ' << gfx.getHeight() << "\n255\n";
        const auto* bytes = static_cast<const char*>(pixels->getData());
        for (std::size_t p = 0; p < pixels->getSize(); p += 4) file.write(bytes + p, 3);
        require(bool(file), "capture write failed");
    }
    const auto teardownStart = Clock::now();
    scene.reset();
    const double teardown = elapsed(teardownStart);
    const auto*  device   = eve::cap::query<eve::caps::IGpuInfo>();
    std::cout << "GAMEPLAY_BENCHMARK_JSON={\"schema\":\"evengine.gameplay-benchmark\",\"version\":1"
              << ",\"workload\":" << std::quoted(workload) << ",\"props\":" << props << ",\"nodes\":" << nodes
              << ",\"characters\":" << characters << ",\"skinInfluences\":" << influences
              << ",\"bonesPerCharacter\":" << bones
              << ",\"verticesPerCharacter\":2048,\"rotationKeysPerChannel\":" << keys
              << ",\"translationKeysPerChannel\":\"1-or-3\",\"scaleKeysPerChannel\":1,\"frames\":" << frames
              << ",\"warmup\":" << warmup << ",\"renderEnabled\":" << (render ? "true" : "false")
              << ",\"shadows\":" << (shadows ? "true" : "false") << ",\"vsync\":" << (gfx.isVSync() ? "true" : "false")
              << ",\"width\":" << gfx.getWidth() << ",\"height\":" << gfx.getHeight()
              << ",\"backend\":" << std::quoted(gfx.getBackendName())
              << ",\"activeGpu\":" << std::quoted(device && device->gpuReady() ? device->gpuName() : "unknown")
              << ",\"setupMs\":" << setup << ",\"ecsTeardownMs\":" << teardown
              << ",\"stateChecksum\":" << std::quoted(std::to_string(checksum)) << ",\"lightingMode\":"
              << std::quoted(control->getEffectiveLightingMode() == LightingMode::Hybrid ? "hybrid" : "forwardPlus")
              << ",\"msaaEnabled\":" << (control->isEnabled("msaa") ? "true" : "false")
              << ",\"validation\":\"passed\",\"phases\":{";
    total.print("frame");
    std::cout << ',';
    edits.print("edits");
    std::cout << ',';
    animation.print("animationAndPalette");
    std::cout << ',';
    sampling.print("clipSamplingAndBlend");
    std::cout << ',';
    worlds.print("boneWorldTransforms");
    std::cout << ',';
    palettes.print("skinPalettePublication");
    std::cout << ',';
    transforms.print("sceneTransforms");
    std::cout << ',';
    render3d.print("render3dIncludingWaits");
    std::cout << ',';
    present.print("compositeAndPresentIncludingWaits");
    if (!gpu.values.empty()) {
        std::cout << ',';
        gpu.print("delayedGpuQueueSpan");
    }
    std::cout << "},\"profilerAggregationMeanMs\":" << aggregation / frames << ",\"cpuZones\":[";
    bool first = true;
    for (const auto& [key, zone] : zones) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"name\":" << std::quoted(zone.name) << ",\"module\":" << std::quoted(zone.module)
                  << ",\"thread\":" << std::quoted(zone.thread) << ",\"selfMeanMs\":" << zone.selfMs / frames
                  << ",\"totalMeanMs\":" << zone.totalMs / frames << ",\"calls\":" << zone.count << '}';
    }
    std::cout << "]}\n";
    return 0;
}
