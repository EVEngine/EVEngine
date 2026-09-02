#include "pixelworld/PixelWorldControl.h"

#include "pixelworld/PixelMaterialCatalogCodec.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <unordered_map>

namespace eve::pixelworld {
namespace {

template <class T>
eve::Result<T> missingWorld() {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, "PixelWorld is not live", "world", {},
        "pixelworld.control"));
}

PixelWorldSummary summarize(const PixelWorld& world) {
    return {world.worldLink(), world.seed(), world.revision(), world.tickValue(),
            world.lastEditSequence(), std::uint32_t(world.chunkCount()),
            std::uint32_t(world.activeChunkCount()), world.isPaused()};
}

}  // namespace

struct PixelWorldControlService::Impl {
    std::unordered_map<std::uint64_t, PixelWorld*> worlds;
    std::unordered_map<std::uint64_t, std::deque<PixelWorldPerformanceSample>> samples;
};

PixelWorldControlService::Impl& PixelWorldControlService::impl() const {
    static auto* state = new Impl;
    return *state;
}

PixelWorldControlService& pixelWorldControlService() {
    static auto* service = new PixelWorldControlService;
    return *service;
}

void PixelWorldControlService::registerWorld(PixelWorld& world) {
    impl().worlds.insert_or_assign(world.worldLink().world, &world);
}

void PixelWorldControlService::unregisterWorld(const PixelWorld& world) noexcept {
    const auto found = impl().worlds.find(world.worldLink().world);
    if (found != impl().worlds.end() && found->second == &world) {
        impl().worlds.erase(found);
        impl().samples.erase(world.worldLink().world);
    }
}

void PixelWorldControlService::rebindWorld(const PixelWorld* previous,
                                           PixelWorld& replacement) noexcept {
    auto& worlds = impl().worlds;
    const auto found = worlds.find(replacement.worldLink().world);
    if (found != worlds.end() && found->second == previous) found->second = &replacement;
}

PixelWorld* PixelWorldControlService::find(std::uint64_t worldId) const noexcept {
    const auto found = impl().worlds.find(worldId);
    return found == impl().worlds.end() ? nullptr : found->second;
}

void PixelWorldControlService::recordStep(std::uint64_t worldId, const StepStats& stats,
                                          std::uint64_t elapsedMicroseconds) {
    auto& samples = impl().samples[worldId];
    samples.push_back({stats.tick.value(), elapsedMicroseconds, stats});
    if (samples.size() > 256) samples.pop_front();
}

std::vector<PixelWorldSummary> PixelWorldControlService::worlds() const {
    std::vector<PixelWorldSummary> result;
    result.reserve(impl().worlds.size());
    for (const auto& [id, world] : impl().worlds) result.push_back(summarize(*world));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.link.world < b.link.world;
    });
    return result;
}

eve::Result<PixelWorldSummary> PixelWorldControlService::world(std::uint64_t worldId) const {
    const PixelWorld* value = find(worldId);
    return value ? eve::Result<PixelWorldSummary>::success(summarize(*value))
                 : missingWorld<PixelWorldSummary>();
}

eve::Result<void> PixelWorldControlService::setPaused(std::uint64_t worldId, bool paused) {
    PixelWorld* value = find(worldId);
    if (!value) return missingWorld<void>();
    value->setPaused(paused);
    return eve::Result<void>::success();
}

eve::Result<PixelWorldStepReceipt> PixelWorldControlService::step(std::uint64_t worldId,
                                                                 std::uint32_t count) {
    PixelWorld* value = find(worldId);
    if (!value) return missingWorld<PixelWorldStepReceipt>();
    if (count == 0 || count > 1024)
        return eve::Result<PixelWorldStepReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "step count must be in [1, 1024]", "count", {},
            "pixelworld.control"));
    PixelWorldStepReceipt receipt;
    receipt.world = worldId;
    receipt.steps = count;
    receipt.firstTick = value->tickValue() + 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < count; ++index) {
        auto advanced = value->advanceImpl(eve::SimulationTick(value->tickValue() + 1), nullptr);
        if (!advanced.ok()) return eve::Result<PixelWorldStepReceipt>::failure(advanced.status());
        receipt.finalStep = advanced.value();
    }
    receipt.lastTick = value->tickValue();
    receipt.elapsedMicroseconds = std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::steady_clock::now() - start)
                                                    .count());
    return eve::Result<PixelWorldStepReceipt>::success(receipt);
}

eve::Result<PixelEditReceipt> PixelWorldControlService::applyEdit(
    std::uint64_t worldId, const PixelEditCommand& command) {
    PixelWorld* value = find(worldId);
    return value ? value->applyEdit(command) : missingWorld<PixelEditReceipt>();
}

eve::Result<PixelCatalogReloadReceipt> PixelWorldControlService::reloadMaterialCatalog(
    std::uint64_t worldId, std::string_view catalogJson,
    std::uint64_t expectedFingerprint) {
    PixelWorld* value = find(worldId);
    if (!value) return missingWorld<PixelCatalogReloadReceipt>();
    auto catalog = decodeMaterialCatalogJson(catalogJson);
    if (!catalog.ok())
        return eve::Result<PixelCatalogReloadReceipt>::failure(catalog.status());
    return value->reloadMaterialCatalog(std::move(catalog).value(), expectedFingerprint);
}

eve::Result<std::vector<PixelChunkDiagnostic>> PixelWorldControlService::chunkDiagnostics(
    std::uint64_t worldId, PixelChunkRegion region) const {
    const PixelWorld* value = find(worldId);
    return value ? value->chunkDiagnostics(region)
                 : missingWorld<std::vector<PixelChunkDiagnostic>>();
}

eve::Result<std::vector<PixelWorldPerformanceSample>>
PixelWorldControlService::performanceSamples(std::uint64_t worldId, std::uint32_t limit) const {
    if (!find(worldId)) return missingWorld<std::vector<PixelWorldPerformanceSample>>();
    if (limit == 0 || limit > 256)
        return eve::Result<std::vector<PixelWorldPerformanceSample>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "sample limit must be in [1, 256]", "limit", {},
            "pixelworld.control"));
    const auto found = impl().samples.find(worldId);
    if (found == impl().samples.end())
        return eve::Result<std::vector<PixelWorldPerformanceSample>>::success({});
    const auto& samples = found->second;
    const std::size_t begin = samples.size() > limit ? samples.size() - limit : 0;
    std::vector<PixelWorldPerformanceSample> result(samples.begin() + std::ptrdiff_t(begin),
                                                     samples.end());
    return eve::Result<std::vector<PixelWorldPerformanceSample>>::success(
        std::move(result));
}

eve::Result<std::vector<std::byte>> PixelWorldControlService::captureSnapshot(
    std::uint64_t worldId) const {
    const PixelWorld* value = find(worldId);
    return value ? value->saveSnapshot() : missingWorld<std::vector<std::byte>>();
}

eve::Result<void> PixelWorldControlService::restoreSnapshot(
    std::uint64_t worldId, std::span<const std::byte> bytes) {
    PixelWorld* value = find(worldId);
    return value ? value->restoreSnapshot(bytes) : missingWorld<void>();
}

}  // namespace eve::pixelworld
