#include "physics/destruction/fx/CollapseDust.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/FogVolume.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/FractureGeometry.h"
namespace eve::physics {
namespace {
Result<void> bad(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "collapseDust"));
}
constexpr std::uint32_t MaxFrames = 2048, MaxParticles = 1024, MaxSamples = 524288;
void                    put(std::ostream& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (i * 8)) & 255));
}
std::uint32_t get(std::istream& in) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        int c = in.get();
        if (c < 0) throw std::runtime_error("truncated dust asset");
        v |= static_cast<std::uint32_t>(c) << (i * 8);
    }
    return v;
}
void  real(std::ostream& out, float v) { put(out, std::bit_cast<std::uint32_t>(v)); }
float real(std::istream& in) { return std::bit_cast<float>(get(in)); }
bool  valid(const std::array<float, 7>& p) {
    for (float f : p)
        if (!std::isfinite(f)) return false;
    for (int i = 0; i < 3; ++i)
        if (std::abs(p[i]) > 10000.f) return false;
    for (int i = 3; i < 7; ++i)
        if (p[i] < 0.f || p[i] > 1.f) return false;
    return true;
}
}  // namespace
CollapseDust::CollapseDust() = default;
CollapseDust::~CollapseDust() {
    if (!future_.valid()) return;
    try {
        auto pending = future_.get();
        if (!pending) std::fprintf(stderr, "collapse dust: unpublished worker failed during teardown\n");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "collapse dust: worker teardown: %s\n", error.what());
    }
}
Result<void> CollapseDust::capture(const fluids::VolumeFluid& source, float seconds) {
    if (future_.valid()) return bad("collect fog worker before capture");
    if (!std::isfinite(seconds) || seconds < 0.f || frames_.size() >= MaxFrames ||
        (frames_.empty() ? seconds != 0.f : seconds <= frames_.back().seconds))
        return bad("invalid capture time/count");
    Frame frame{seconds, source.spacing(), {}};
    for (const auto& p : source.particleView())
        if (p.material.phase == fluids::VolumeFluidPhase::Gas) {
            const float          fade = p.life > 0.f ? std::min(1.f, p.life / 2.f) : 1.f;
            std::array<float, 7> sample{p.position.x, p.position.y, p.position.z,    p.color.r,
                                        p.color.g,    p.color.b,    p.color.a * fade};
            if (!valid(sample)) return bad("invalid gas sample");
            frame.particles.push_back(sample);
        }
    std::size_t total = frame.particles.size();
    for (const auto& f : frames_) total += f.particles.size();
    if (frame.particles.size() > MaxParticles || total > MaxSamples) return bad("gas cache budget exceeded");
    frames_.push_back(std::move(frame));
    requestedSeconds_ = -1.f;
    presented_        = false;
    return Result<void>::success();
}
Result<void> CollapseDust::present(fluids::VolumeFluid& target, float seconds) const {
    if (frames_.empty() || !std::isfinite(seconds)) return bad("empty dust clip or invalid time");
    auto it = std::upper_bound(frames_.begin(), frames_.end(), seconds,
                               [](float t, const Frame& f) { return t < f.seconds; });
    if (it != frames_.begin()) --it;
    fluids::VolumeFluidSnapshot state;
    state.settings         = target.snapshot().settings;
    state.settings.spacing = it->spacing;
    for (const auto& s : it->particles) {
        fluids::VolumeFluidParticle p;
        p.position       = {s[0], s[1], s[2]};
        p.color          = {s[3], s[4], s[5], s[6]};
        p.material.phase = fluids::VolumeFluidPhase::Gas;
        state.particles.push_back(p);
    }
    return target.restore(state);
}
Result<void> CollapseDust::save(std::string_view path) const {
    if (path.empty() || frames_.empty()) return bad("path and frames required");
    std::error_code             ec;
    const std::filesystem::path output{std::string(path)};
    if (std::filesystem::exists(output, ec) || ec) return bad("dust output already exists or cannot be inspected");
    std::ofstream out(output, std::ios::binary);
    if (!out) return bad("cannot create dust output");
    out.write("EVEDUST1", 8);
    put(out, 1);
    put(out, static_cast<std::uint32_t>(frames_.size()));
    for (const auto& f : frames_) {
        real(out, f.seconds);
        real(out, f.spacing);
        put(out, static_cast<std::uint32_t>(f.particles.size()));
        for (const auto& p : f.particles)
            for (float v : p) real(out, v);
    }
    out.close();
    if (out) return Result<void>::success();
    std::filesystem::remove(output, ec);
    return bad("dust write failed");
}
Result<void> CollapseDust::load(std::string_view path) {
    if (future_.valid()) return bad("collect fog worker before load");
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return bad("dust asset not found");
    std::vector<Frame> candidate;
    try {
        char magic[8];
        in.read(magic, 8);
        if (!in || std::string_view(magic, 8) != "EVEDUST1" || get(in) != 1)
            return bad("unsupported dust schema/version");
        const auto count = get(in);
        if (!count || count > MaxFrames) return bad("dust frame budget exceeded");
        std::size_t total = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            Frame f{real(in), real(in), {}};
            auto  n = get(in);
            total += n;
            if (n > MaxParticles || total > MaxSamples || !std::isfinite(f.seconds) || !std::isfinite(f.spacing) ||
                f.spacing < 0.001f || f.spacing > 10.f ||
                (i ? f.seconds <= candidate.back().seconds : f.seconds != 0.f))
                return bad("invalid dust frame");
            f.particles.resize(n);
            for (auto& p : f.particles) {
                for (float& v : p) v = real(in);
                if (!valid(p)) return bad("invalid dust sample");
            }
            candidate.push_back(std::move(f));
        }
        if (in.peek() != std::char_traits<char>::eof()) return bad("trailing dust fields");
    } catch (const std::exception&) {
        return bad("truncated dust asset");
    }
    frames_           = std::move(candidate);
    requestedSeconds_ = -1.f;
    presented_        = false;
    return Result<void>::success();
}
Result<void> CollapseDust::prepareDepth(int width, int height) {
    if (future_.valid()) return bad("collect outstanding fog job before depth preparation");
    if (width < 1 || height < 1 || width > 1024 || height > 1024) return bad("dust depth dimensions outside budget");
    std::vector<float> candidate(static_cast<std::size_t>(width) * height, std::numeric_limits<float>::max());
    depth_ = std::move(candidate);
    depthPixels_.resize(depth_.size() * 4);
    width_  = width;
    height_ = height;
    return Result<void>::success();
}
Result<void> CollapseDust::rasterizeDepth(const CollapseAnimation& clip, float seconds, graphics::Camera3D& camera) {
    const auto  c = camera.data();
    DustFogView view;
    view.view = glm::lookAtRH(glm::vec3(c->eyeX, c->eyeY, c->eyeZ), glm::vec3(c->targetX, c->targetY, c->targetZ),
                              glm::vec3(c->upX, c->upY, c->upZ));
    view.fov  = c->fovYDeg;
    view.nearDistance = c->nearZ;
    view.farDistance  = c->farZ;
    if (c->orthographic) return bad("dust needs a perspective camera");
    return rasterizeDepthView(clip, seconds, view);
}
Result<void> CollapseDust::rasterizeDepthView(const CollapseAnimation& clip, float seconds, const DustFogView& camera) {
    auto geometry = clip.fragmentGeometry();
    if (!geometry || depth_.empty() || !std::isfinite(seconds) || camera.fov <= 1.f || camera.fov >= 179.f)
        return bad("invalid depth camera/clip");
    const auto& view  = camera.view;
    const float scale = std::tan(glm::radians(camera.fov) * 0.5f), aspect = static_cast<float>(width_) / height_;
    std::fill(depth_.begin(), depth_.end(), std::numeric_limits<float>::max());
    for (std::size_t bone = 0; bone < geometry->fragments.size(); ++bone) {
        auto sample = clip.sample(seconds, static_cast<int>(bone));
        if (!sample) return Result<void>::failure(sample.status());
        const auto&     b = sample.value();
        const glm::quat q(b.rotW, b.rotX, b.rotY, b.rotZ);
        const glm::vec3 origin(b.worldX, b.worldY, b.worldZ);
        const auto&     mesh = geometry->fragments[bone];
        for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
            glm::vec3 v[3];
            bool      clipped = false;
            for (int k = 0; k < 3; ++k) {
                const auto  j = mesh.indices[t + k] * 3;
                const auto  p = view * glm::vec4(origin + q * glm::vec3(mesh.positions[j], mesh.positions[j + 1],
                                                                        mesh.positions[j + 2]),
                                                 1.f);
                const float z = -p.z;
                if (z <= camera.nearDistance) {
                    clipped = true;
                    break;
                }
                v[k] = {width_ * (0.5f + 0.5f * p.x / (z * scale * aspect)),
                        height_ * (0.5f - 0.5f * p.y / (z * scale)), 1.f / z};
            }
            if (clipped) continue;
            const auto edge = [](const glm::vec3& a, const glm::vec3& b, float x, float y) {
                return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
            };
            const float area = edge(v[0], v[1], v[2].x, v[2].y);
            if (std::abs(area) < 1e-5f) continue;
            int x0 = std::clamp(static_cast<int>(std::floor(std::min({v[0].x, v[1].x, v[2].x}))), 0, width_ - 1);
            int x1 = std::clamp(static_cast<int>(std::ceil(std::max({v[0].x, v[1].x, v[2].x}))), 0, width_ - 1);
            int y0 = std::clamp(static_cast<int>(std::floor(std::min({v[0].y, v[1].y, v[2].y}))), 0, height_ - 1);
            int y1 = std::clamp(static_cast<int>(std::ceil(std::max({v[0].y, v[1].y, v[2].y}))), 0, height_ - 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    float a    = edge(v[1], v[2], x + 0.5f, y + 0.5f) / area,
                          bary = edge(v[2], v[0], x + 0.5f, y + 0.5f) / area, d = 1.f - a - bary;
                    if (a >= 0.f && bary >= 0.f && d >= 0.f) {
                        float z    = 1.f / (a * v[0].z + bary * v[1].z + d * v[2].z);
                        auto& cell = depth_[static_cast<std::size_t>(y) * width_ + x];
                        cell       = std::min(cell, z);
                    }
                }
        }
    }
    return Result<void>::success();
}
Result<void> CollapseDust::updateDepthTexture(graphics::Graphics& graphics, graphics::Texture& texture,
                                              const CollapseAnimation& clip, float seconds,
                                              graphics::Camera3D& camera) {
    if (texture.getWidth() != width_ || texture.getHeight() != height_) return bad("depth texture dimensions mismatch");
    auto raster = rasterizeDepth(clip, seconds, camera);
    if (!raster) return raster;
    const float near = camera.getNearClip(), far = camera.getFarClip();
    for (std::size_t i = 0; i < depth_.size(); ++i) {
        const auto value =
            static_cast<unsigned char>(std::lround(std::clamp((depth_[i] - near) / (far - near), 0.f, 1.f) * 255.f));
        depthPixels_[i * 4]     = value;
        depthPixels_[i * 4 + 1] = value;
        depthPixels_[i * 4 + 2] = value;
        depthPixels_[i * 4 + 3] = 255;
    }
    if (!graphics.updateTexture(&texture, width_, height_, depthPixels_.data()))
        return bad("depth texture upload failed");
    return Result<void>::success();
}
Result<void> CollapseDust::injectFog(graphics::AtmosphereVolume& grid, const glm::mat4& inverse, float seconds,
                                     float density, float radius) const {
    if (frames_.empty() || !std::isfinite(seconds) || !std::isfinite(density) || density < 0.f || density > 10.f ||
        !std::isfinite(radius) || radius < 0.1f || radius > 5.f || grid.getFroxelCount() == 0 ||
        grid.getFroxelCount() > 4000000)
        return bad("invalid fog injection parameters or grid");
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(inverse[c][r])) return bad("nonfinite fog camera");
    if (std::abs(glm::determinant(inverse)) < 1e-12f) return bad("singular fog camera");
    const auto projection = glm::inverse(inverse);
    auto       it         = std::upper_bound(frames_.begin(), frames_.end(), seconds,
                                             [](float t, const Frame& f) { return t < f.seconds; });
    if (it != frames_.begin()) --it;
    auto unproject = [&](float x, float y, float z) {
        const auto p = inverse * glm::vec4(x, y, z, 1.f);
        return glm::vec3(p) / p.w;
    };
    const int          w = grid.getWidth(), h = grid.getHeight();
    const float        nearDistance = grid.getNearDistance(), farDistance = grid.getFarDistance();
    std::vector<float> fractions(static_cast<std::size_t>(grid.getDepth()));
    for (int z = 0; z < grid.getDepth(); ++z)
        fractions[z] = (grid.sliceDistance(z) - nearDistance) / (farDistance - nearDistance);
    std::vector<glm::vec3> nearRays(static_cast<std::size_t>(w) * h), farRays(nearRays.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float nx = (x + 0.5f) / w * 2.f - 1.f, ny = (y + 0.5f) / h * 2.f - 1.f;
            const auto  index = static_cast<std::size_t>(y) * w + x;
            nearRays[index]   = unproject(nx, ny, 0.f);
            farRays[index]    = unproject(nx, ny, 1.f);
        }
    for (const auto& p : it->particles) {
        if (p[6] <= 0.f) continue;
        graphics::FogVolume fog;
        fog.setShape("sphere");
        fog.setPosition(p[0], p[1], p[2]);
        fog.setSize(radius * 2.f, radius * 2.f, radius * 2.f);
        fog.setAlbedo(p[3], p[4], p[5]);
        fog.setExtinction(density * p[6]);
        fog.setEdgeFalloff(0.9f);
        fog.setNoise(0.72f, 1.4f, 42);
        float xmin = 1.f, xmax = -1.f, ymin = 1.f, ymax = -1.f, zmin = grid.getFarDistance(), zmax = 0.f;
        bool  nearCrossing = false;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 offset((corner & 1) ? radius : -radius, (corner & 2) ? radius : -radius,
                                   (corner & 4) ? radius : -radius);
            const auto projected = projection * glm::vec4(glm::vec3(p[0], p[1], p[2]) + offset, 1.f);
            if (projected.w <= grid.getNearDistance()) {
                nearCrossing = true;
                continue;
            }
            xmin = std::min(xmin, projected.x / projected.w);
            xmax = std::max(xmax, projected.x / projected.w);
            ymin = std::min(ymin, projected.y / projected.w);
            ymax = std::max(ymax, projected.y / projected.w);
            zmin = std::min(zmin, projected.w);
            zmax = std::max(zmax, projected.w);
        }
        if (nearCrossing) {
            xmin = ymin = -1.f;
            xmax = ymax = 1.f;
            zmin        = grid.getNearDistance();
        }
        if (xmax < -1.f || xmin > 1.f || ymax < -1.f || ymin > 1.f || zmax < grid.getNearDistance() ||
            zmin > grid.getFarDistance())
            continue;
        const int x0 = std::clamp(static_cast<int>((xmin * 0.5f + 0.5f) * w) - 1, 0, w - 1),
                  x1 = std::clamp(static_cast<int>((xmax * 0.5f + 0.5f) * w) + 1, 0, w - 1);
        const int y0 = std::clamp(static_cast<int>((ymin * 0.5f + 0.5f) * h) - 1, 0, h - 1),
                  y1 = std::clamp(static_cast<int>((ymax * 0.5f + 0.5f) * h) + 1, 0, h - 1);
        const int z0 = std::max(0, grid.sliceForDistance(zmin) - 1),
                  z1 = std::min(grid.getDepth() - 1, grid.sliceForDistance(zmax) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const auto  index = static_cast<std::size_t>(y) * w + x;
                const auto& a     = nearRays[index];
                const auto& b     = farRays[index];
                for (int z = z0; z <= z1; ++z) {
                    const float fraction   = fractions[z];
                    const float extinction = fog.sampleExtinction(glm::mix(a, b, fraction));
                    if (extinction <= 0.f) continue;
                    auto& cell = grid.at(x, y, z);
                    cell.extinction += extinction;
                    cell.scattering += fog.getAlbedo() * extinction;
                    cell.anisotropy = 0.1f;
                }
            }
    }
    return Result<void>::success();
}

namespace {
bool sameView(const DustFogView& a, const DustFogView& b) {
    return a.view == b.view && a.inverseViewProjection == b.inverseViewProjection && a.fov == b.fov &&
           a.nearDistance == b.nearDistance && a.farDistance == b.farDistance;
}
}  // namespace
Result<void> CollapseDust::prepareFog(const CollapseAnimation& clip, int dw, int dh, int gw, int gh, int gd) {
    if (future_.valid()) return bad("collect outstanding fog job before preparation");
    if (!clip.fragmentGeometry() || gw < 1 || gh < 1 || gd < 1 || gw > 256 || gh > 256 || gd > 256 ||
        static_cast<std::size_t>(gw) * gh * gd > 4000000)
        return bad("invalid fog preparation dimensions/clip");
    auto candidate = std::make_shared<CollapseAnimation>(clip);
    auto prepared  = prepareDepth(dw, dh);
    if (!prepared) return prepared;
    fogClip_          = std::move(candidate);
    gridWidth_        = gw;
    gridHeight_       = gh;
    gridDepth_        = gd;
    requestedSeconds_ = -1.f;
    presented_        = false;
    return Result<void>::success();
}
Result<DustFogRequest> CollapseDust::requestFog(float seconds, const DustFogView& view, float density, float radius) {
    if (!fogClip_ || frames_.empty() || !std::isfinite(seconds) || !std::isfinite(density) || density < 0.f ||
        density > 10.f || !std::isfinite(radius) || radius < 0.1f || radius > 5.f)
        return Result<DustFogRequest>::failure(bad("invalid fog job parameters").status());
    if (!std::isfinite(view.fov) || view.fov <= 1.f || view.fov >= 179.f || !std::isfinite(view.nearDistance) ||
        !std::isfinite(view.farDistance) || view.nearDistance <= 0.f || view.farDistance <= view.nearDistance)
        return Result<DustFogRequest>::failure(bad("invalid fog view range").status());
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(view.view[c][r]) || !std::isfinite(view.inverseViewProjection[c][r]))
                return Result<DustFogRequest>::failure(bad("nonfinite fog view").status());
    if (std::abs(glm::determinant(view.inverseViewProjection)) < 1e-12f)
        return Result<DustFogRequest>::failure(bad("singular fog view").status());
    if (future_.valid()) return Result<DustFogRequest>::success(DustFogRequest::Busy);
    if (seconds == requestedSeconds_ && density == requestedDensity_ && radius == requestedRadius_ &&
        sameView(view, requestedView_))
        return Result<DustFogRequest>::success(DustFogRequest::Unchanged);
    auto it = std::upper_bound(frames_.begin(), frames_.end(), seconds,
                               [](float t, const Frame& f) { return t < f.seconds; });
    if (it != frames_.begin()) --it;
    auto frame      = *it;
    frame.seconds   = 0.f;
    const auto clip = fogClip_;
    const int  dw = width_, dh = height_, gw = gridWidth_, gh = gridHeight_, gd = gridDepth_;
    try {
        future_ = std::async(
            std::launch::async,
            [frame = std::move(frame), clip, view, seconds, density, radius, dw, dh, gw, gh,
             gd]() mutable -> Result<DustFogFrame> {
                CollapseDust scratch;
                scratch.frames_.push_back(std::move(frame));
                auto prepared = scratch.prepareDepth(dw, dh);
                if (!prepared) return Result<DustFogFrame>::failure(prepared.status());
                graphics::AtmosphereVolume grid;
                grid.resize(gw, gh, gd);
                grid.setDepthRange(view.nearDistance, view.farDistance);
                auto injected = scratch.injectFog(grid, view.inverseViewProjection, 0.f, density, radius);
                if (!injected) return Result<DustFogFrame>::failure(injected.status());
                grid.integrate(glm::vec3(1.10f, 1.02f, 0.86f), 1.f);
                DustFogFrame output;
                output.view        = view;
                output.seconds     = seconds;
                output.depthWidth  = dw;
                output.depthHeight = dh;
                const int cols     = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(gd))))),
                          rows     = (gd + cols - 1) / cols;
                output.atlasWidth  = gw * cols;
                output.atlasHeight = gh * rows;
                output.atlas.resize(static_cast<std::size_t>(output.atlasWidth) * output.atlasHeight * 4);
                for (int z = 0; z < gd; ++z)
                    for (int y = 0; y < gh; ++y)
                        for (int x = 0; x < gw; ++x) {
                            const auto& value  = grid.integratedAt(x, y, z);
                            const auto  mapped = glm::vec3(value) / (glm::vec3(1.f) + glm::vec3(value));
                            const auto  at     = (static_cast<std::size_t>((z / cols) * gh + y) * output.atlasWidth +
                                             (z % cols) * gw + x) *
                                            4;
                            for (int channel = 0; channel < 3; ++channel)
                                output.atlas[at + channel] = static_cast<unsigned char>(
                                    std::lround(std::clamp(mapped[channel], 0.f, 1.f) * 255.f));
                            output.atlas[at + 3] =
                                static_cast<unsigned char>(std::lround(std::clamp(value.a, 0.f, 1.f) * 255.f));
                        }
                // Reconstruct sparse media footprints before the existing fog
                // compositor samples them. Clamp within each Z tile: neighbouring
                // atlas tiles represent different depths and must never bleed.
                std::vector<unsigned char> filtered(output.atlas.size());
                for (int pass = 0; pass < 2; ++pass) {
                    for (int z = 0; z < gd; ++z)
                        for (int y = 0; y < gh; ++y)
                            for (int x = 0; x < gw; ++x) {
                                const auto at = (static_cast<std::size_t>((z / cols) * gh + y) * output.atlasWidth +
                                                 (z % cols) * gw + x) *
                                                4;
                                for (int channel = 0; channel < 4; ++channel) {
                                    unsigned           total      = 0;
                                    constexpr unsigned weights[5] = {1, 4, 6, 4, 1};
                                    for (int tap = -2; tap <= 2; ++tap) {
                                        const int  sx = std::clamp(x + (pass == 0 ? tap : 0), 0, gw - 1);
                                        const int  sy = std::clamp(y + (pass == 1 ? tap : 0), 0, gh - 1);
                                        const auto from =
                                            (static_cast<std::size_t>((z / cols) * gh + sy) * output.atlasWidth +
                                             (z % cols) * gw + sx) *
                                                4 +
                                            channel;
                                        total += weights[tap + 2] * output.atlas[from];
                                    }
                                    filtered[at + channel] = static_cast<unsigned char>((total + 8) / 16);
                                }
                            }
                    output.atlas.swap(filtered);
                }
                auto depth = scratch.rasterizeDepthView(*clip, seconds, view);
                if (!depth) return Result<DustFogFrame>::failure(depth.status());
                output.depth.resize(scratch.depth_.size() * 4);
                for (std::size_t i = 0; i < scratch.depth_.size(); ++i) {
                    const auto d            = static_cast<unsigned char>(std::lround(
                        std::clamp((scratch.depth_[i] - view.nearDistance) / (view.farDistance - view.nearDistance),
                                              0.f, 1.f) *
                        255.f));
                    output.depth[i * 4]     = d;
                    output.depth[i * 4 + 1] = d;
                    output.depth[i * 4 + 2] = d;
                    output.depth[i * 4 + 3] = 255;
                }
                return Result<DustFogFrame>::success(std::move(output));
            });
    } catch (const std::exception&) {
        return Result<DustFogRequest>::failure(bad("fog worker admission failed").status());
    }
    requestedSeconds_ = seconds;
    requestedDensity_ = density;
    requestedRadius_  = radius;
    requestedView_    = view;
    return Result<DustFogRequest>::success(DustFogRequest::Started);
}
Result<std::optional<DustFogFrame>> CollapseDust::takeFogFrame() {
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return Result<std::optional<DustFogFrame>>::success(std::nullopt);
    try {
        auto output = future_.get();
        if (!output) {
            requestedSeconds_ = -1.f;
            return Result<std::optional<DustFogFrame>>::failure(output.status());
        }
        return Result<std::optional<DustFogFrame>>::success(std::move(output).takeValue());
    } catch (const std::exception&) {
        requestedSeconds_ = -1.f;
        return Result<std::optional<DustFogFrame>>::failure(bad("fog worker failed").status());
    }
}
Result<DustFogDisplay> CollapseDust::updateFog(graphics::Volumetric& fog, graphics::Graphics& graphics, float seconds,
                                               graphics::Camera3D& camera, float density, float radius) {
    DustFogView view;
    const auto  c = camera.data();
    if (c->orthographic) return Result<DustFogDisplay>::failure(bad("fog needs a perspective camera").status());
    view.fov          = c->fovYDeg;
    view.nearDistance = c->nearZ;
    view.farDistance  = c->farZ;
    view.view = glm::lookAtRH(glm::vec3(c->eyeX, c->eyeY, c->eyeZ), glm::vec3(c->targetX, c->targetY, c->targetZ),
                              glm::vec3(c->upX, c->upY, c->upZ));
    view.inverseViewProjection = fog.getInvViewProj();
    auto completed             = takeFogFrame();
    if (!completed) return Result<DustFogDisplay>::failure(completed.status());
    if (completed.value() && sameView(completed.value()->view, view)) {
        const auto& frame = *completed.value();
        auto*       atlas = fog.getFroxelAtlas();
        if (!atlas || atlas->getWidth() != frame.atlasWidth || atlas->getHeight() != frame.atlasHeight)
            return Result<DustFogDisplay>::failure(bad("prepare matching fog atlas before update").status());
        if (!presented_) graphics.setTextureSampler(atlas, "linear", "none", 1.f, 0.f);
        if (!graphics.updateTexture(atlas, frame.atlasWidth, frame.atlasHeight, frame.atlas.data()))
            return Result<DustFogDisplay>::failure(bad("fog texture upload failed").status());
        presented_     = true;
        presentedView_ = view;
    }
    auto request = requestFog(seconds, view, density, radius);
    if (!request) return Result<DustFogDisplay>::failure(request.status());
    return Result<DustFogDisplay>::success(presented_ && sameView(presentedView_, view) ? DustFogDisplay::Ready
                                                                                        : DustFogDisplay::Pending);
}
}  // namespace eve::physics
