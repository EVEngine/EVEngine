// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "probe",
  "default": "module-composition",
  "module": "physics_backend",
  "task": "physics-bake"
}
*/
// clang-format on

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include "physics/backend/bake/collapse/CollapseAnimation.h"
#include "physics/backend/bake/collapse/CollapseCaptureSource.h"

namespace {
using namespace eve::physics;
class SampleSource final : public CollapseCaptureSource {
public:
    explicit SampleSource(const CollapseAnimation& clip) : clip_(clip) {}
    float                                   seconds = 0;
    int                                     boneCount() const noexcept override { return clip_.boneCount(); }
    std::shared_ptr<const FractureGeometry> fragmentGeometry() const noexcept override { return {}; }
    eve::Result<BonePresentation> bonePresentation(int bone) const override { return clip_.sample(seconds, bone); }

private:
    const CollapseAnimation& clip_;
};
struct Error {
    double      maximum = 0, squares = 0;
    std::size_t count = 0;
    void        add(double value) {
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite error");
        maximum = std::max(maximum, value);
        squares += value * value;
        ++count;
    }
    double rms() const { return std::sqrt(squares / double(count)); }
};
}  // namespace
// Explicit source cadence: this diagnostic never infers stored timestamps from frame count.
// Compares against the loaded asset sampler, not an independently running solver.
int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: physics_bake_collapse_error asset.evecollapse reference_hz\n";
            return 2;
        }
        std::size_t  end = 0;
        const double hz  = std::stod(argv[2], &end);
        if (end != std::string(argv[2]).size() || !std::isfinite(hz) || hz <= 0 || hz > 10000)
            throw std::runtime_error("reference_hz must be finite and in (0,10000]");
        auto loaded = CollapseAnimation::load(argv[1]);
        if (!loaded) {
            std::cerr << loaded.status().describe() << '\n';
            return 1;
        }
        const auto   reference = std::move(loaded).takeValue();
        const double intervals = double(reference.duration()) * hz;
        if (intervals < 1 || intervals > 10000000 || std::abs(intervals - std::round(intervals)) > .001)
            throw std::runtime_error("duration must contain an integral, bounded reference grid");
        const int steps = static_cast<int>(std::round(intervals));
        for (const int stride : {1, 2, 4, 8}) {
            CollapseAnimation sparse;
            SampleSource      source(reference);
            for (int i = 0; i <= steps; i += stride) {
                source.seconds = float(double(i) / hz);
                auto captured  = sparse.capture(source, source.seconds);
                if (!captured) throw std::runtime_error(captured.status().describe());
            }
            if (steps % stride != 0) {
                source.seconds = reference.duration();
                auto captured  = sparse.capture(source, source.seconds);
                if (!captured) throw std::runtime_error(captured.status().describe());
            }
            Error       position, angle;
            std::size_t stateMismatches = 0;
            int         peakFrame = 0, peakBone = 0;
            for (int i = 0; i <= steps; ++i) {
                const float seconds = float(double(i) / hz);
                for (int bone = 0; bone < reference.boneCount(); ++bone) {
                    auto aResult = sparse.sample(seconds, bone);
                    auto bResult = reference.sample(seconds, bone);
                    if (!aResult || !bResult) throw std::runtime_error("sample failed");
                    const auto   a = aResult.value(), b = bResult.value();
                    const double delta = std::hypot(double(a.worldX) - b.worldX, double(a.worldY) - b.worldY,
                                                    double(a.worldZ) - b.worldZ);
                    if (delta > position.maximum) {
                        peakFrame = i;
                        peakBone  = bone;
                    }
                    position.add(delta);
                    const double dot = double(a.rotX) * b.rotX + double(a.rotY) * b.rotY + double(a.rotZ) * b.rotZ +
                                       double(a.rotW) * b.rotW;
                    const double an = std::sqrt(double(a.rotX) * a.rotX + double(a.rotY) * a.rotY +
                                                double(a.rotZ) * a.rotZ + double(a.rotW) * a.rotW);
                    const double bn = std::sqrt(double(b.rotX) * b.rotX + double(b.rotY) * b.rotY +
                                                double(b.rotZ) * b.rotZ + double(b.rotW) * b.rotW);
                    if (an <= 0 || bn <= 0) throw std::runtime_error("zero quaternion");
                    angle.add(2 * std::acos(std::clamp(std::abs(dot) / (an * bn), 0., 1.)));
                    if (a.state != b.state) ++stateMismatches;
                }
            }
            std::cout << "reference=asset_sampler reference_hz=" << hz << " stride=" << stride
                      << " frames=" << sparse.frameCount() << " poses=" << position.count
                      << " position_max_m=" << position.maximum << " position_rms_m=" << position.rms()
                      << " angle_max_rad=" << angle.maximum << " angle_rms_rad=" << angle.rms()
                      << " position_peak_seconds=" << double(peakFrame) / hz << " position_peak_bone=" << peakBone
                      << " state_mismatches=" << stateMismatches << '\n';
            if (stride > 1) {
                for (int i = std::max(0, peakFrame - stride); i <= std::min(steps, peakFrame + stride); ++i) {
                    const float seconds    = float(double(i) / hz);
                    auto        poseResult = reference.sample(seconds, peakBone);
                    if (!poseResult) throw std::runtime_error(poseResult.status().describe());
                    const auto pose = poseResult.value();
                    std::cout << "peak_neighbor stride=" << stride << " frame=" << i << " seconds=" << seconds
                              << " bone=" << peakBone << " state=" << static_cast<int>(pose.state)
                              << " x=" << pose.worldX << " y=" << pose.worldY << " z=" << pose.worldZ << '\n';
                }
            }
            if (stride == 1 && (position.maximum > 1e-5 || angle.maximum > 1e-6 || stateMismatches != 0))
                throw std::runtime_error("reference-grid round trip failed");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
