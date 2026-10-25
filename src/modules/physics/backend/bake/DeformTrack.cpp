// eve-hot-path
#include <algorithm>
#include <cmath>
#include <utility>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformTrackInternal.h"
namespace eve::physics {

namespace {
Diagnostic invalidDeform(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "deformTrack");
}
bool sameTopology(const DeformTopology& a, const DeformTopology& b) {
    // Exact comparison avoids accepting a noncryptographic hash collision as topology equality.
    return a.primitive() == b.primitive() && a.vertexCount() == b.vertexCount() &&
           std::equal(a.indices().begin(), a.indices().end(), b.indices().begin(), b.indices().end()) &&
           std::equal(a.uv().begin(), a.uv().end(), b.uv().begin(), b.uv().end(),
                      [](DeformUV x, DeformUV y) { return x.u == y.u && x.v == y.v; });
}
}  // namespace
Result<void> bake_detail::validateDeformFrame(const DeformFrame& frame, const DeformTopology& topology,
                                              const BakeRecipe& recipe, std::uint32_t index, float previous) {
    auto valid = recipe.validateSampleTime(index, frame.seconds);
    if (!valid) return valid;
    if (frame.seconds <= previous || frame.positions.size() != topology.vertexCount() ||
        !sameTopology(topology, frame.topology))
        return Result<void>::failure(invalidDeform("changed topology, count or nonincreasing time"));
    for (const auto point : frame.positions)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            (recipe.coordinates() == BakeCoordinates::PlanarPixels && point.z != 0))
            return Result<void>::failure(invalidDeform("nonfinite or out-of-plane deformation point"));
    return Result<void>::success();
}
DeformTrack::DeformTrack(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
Result<DeformTrack> DeformTrack::prepare(std::vector<DeformFrame> frames, const BakeRecipe& recipe) {
    auto encoded = recipe.encode();
    if (!encoded) return Result<DeformTrack>::failure(encoded.status());
    if (frames.empty() || frames.size() > 2000000)
        return Result<DeformTrack>::failure(invalidDeform("empty or oversized deformation recording"));
    const auto topology = frames.front().topology;
    if (frames.size() > 2000000 / topology.vertexCount())
        return Result<DeformTrack>::failure(invalidDeform("deformation sample budget exceeded"));
    float previous = -1;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto& frame = frames[i];
        auto valid = bake_detail::validateDeformFrame(frame, topology, recipe, static_cast<std::uint32_t>(i), previous);
        if (!valid) return Result<DeformTrack>::failure(valid.status());
        previous       = frame.seconds;
        frame.topology = topology;
    }
    return Result<DeformTrack>::success(
        DeformTrack(std::make_shared<const Data>(Data{std::move(frames), std::make_shared<const BakeRecipe>(recipe)})));
}
Result<void> DeformTrack::sample(float seconds, std::span<DeformPoint> output) const {
    const auto& frames = data_->frames;
    if (!std::isfinite(seconds) || output.size() != frames.front().positions.size())
        return Result<void>::failure(invalidDeform("invalid deformation sample time or output extent"));
    auto         upper    = std::upper_bound(frames.begin(), frames.end(), seconds,
                                             [](float time, const DeformFrame& frame) { return time < frame.seconds; });
    const auto&  a        = upper == frames.begin() ? frames.front() : *(upper - 1);
    const auto&  b        = upper == frames.end() ? frames.back() : *upper;
    const double fraction = a.seconds == b.seconds
                                ? 0
                                : std::clamp((double(seconds) - a.seconds) / (double(b.seconds) - a.seconds), 0., 1.);
    auto         blend    = [fraction](float x, float y) {
        return static_cast<float>((1 - fraction) * double(x) + fraction * double(y));
    };
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto x = a.positions[i], y = b.positions[i];
        output[i] = {blend(x.x, y.x), blend(x.y, y.y), blend(x.z, y.z)};
    }
    return Result<void>::success();
}
DeformTopology                    DeformTrack::topology() const { return data_->frames.front().topology; }
std::shared_ptr<const BakeRecipe> DeformTrack::recipe() const noexcept { return data_->recipe; }
float                             DeformTrack::duration() const noexcept { return data_->frames.back().seconds; }
std::size_t                       DeformTrack::frameCount() const noexcept { return data_->frames.size(); }
}  // namespace eve::physics
