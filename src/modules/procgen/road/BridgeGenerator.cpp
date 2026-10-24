#include "procgen/road/BridgeGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::procgen::road {
namespace {
using V = BridgePoint;
V     operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V     operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V     operator*(V a, float b) { return {a.x * b, a.y * b, a.z * b}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V     cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V a) { return std::sqrt(dot(a, a)); }
V     unit(V a) { return a * (1.f / length(a)); }
bool  finite(V a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
           std::max({std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}) <= 100000.f;
}
template <class T>
Result<T> invalid(const std::string& message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "bridge"));
}
Result<void> validate(const BridgeParameters& p) {
    if (p.kind < BridgeKind::Beam || p.kind > BridgeKind::Trestle || !finite(p.start) || !finite(p.end))
        return invalid<void>("unknown bridge kind or non-finite endpoint");
    const float dimensions[] = {
        p.width,         p.deckThickness,  p.memberWidth,  p.pierWidth,        p.pierDepth,       p.archRise,
        p.towerHeight,   p.trussHeight,    p.cableRadius,  p.railingHeight,    p.railingWidth,    p.memberDepth,
        p.girderHeight,  p.archRibWidth,   p.archRibDepth, p.foundationHeight, p.foundationScale, p.pierCapHeight,
        p.abutmentDepth, p.wingWallLength, p.uvMeters,     p.towerLegWidth};
    for (float value : dimensions)
        if (!std::isfinite(value) || value < .01f || value > 100.f)
            return invalid<void>("bridge dimensions must be finite and in [0.01,100] metres");
    if (!std::isfinite(p.sidewalkWidth) || p.sidewalkWidth < 0.f || p.sidewalkWidth > 20.f ||
        !std::isfinite(p.groundY) || !std::isfinite(p.cableSag) || p.cableSag < .05f || p.cableSag > .95f ||
        !std::isfinite(p.towerInset) || p.towerInset < .15f || p.towerInset > .4f ||
        !std::isfinite(p.towerTopWidthRatio) || p.towerTopWidthRatio < .05f || p.towerTopWidthRatio > 1.f ||
        p.railing < BridgeRailing::Metal || p.railing > BridgeRailing::Timber || p.railingBars < 1 ||
        p.railingBars > 8 || p.plankCount < 8 || p.plankCount > 2048 || !std::isfinite(p.portalInset) ||
        p.portalInset < 0.f || p.portalInset > .2f || !std::isfinite(p.cantileverCenterRatio) ||
        p.cantileverCenterRatio < .1f || p.cantileverCenterRatio > .7f || p.spans < 1 || p.spans > 64 || p.panels < 2 ||
        p.panels > 64 || p.curveSegments < 4 || p.curveSegments > 128 || p.cableSides < 3 || p.cableSides > 16)
        return invalid<void>("bridge offsets, ratios or subdivision counts outside supported bounds");
    const double span = std::hypot(double(p.end.x) - p.start.x, double(p.end.z) - p.start.z);
    const float  below =
        p.kind == BridgeKind::Arch ? p.archRise : (p.kind == BridgeKind::DeckTruss ? p.trussHeight : 0.f);
    if (span < 12. || span > 10000. || std::max(std::fabs(p.start.y), std::fabs(p.end.y)) > 100000.f ||
        std::min(p.start.y, p.end.y) - p.deckThickness - below - p.foundationHeight - p.pierCapHeight <= p.groundY)
        return invalid<void>("horizontal span must be [12,10000]; foundations must lie below the complete structure");
    return Result<void>::success();
}
}  // namespace

std::string_view bridgeKindName(BridgeKind kind) noexcept {
    constexpr std::string_view names[] = {"beam",      "slab",       "arch",        "tiedArch",   "throughTruss",
                                          "deckTruss", "suspension", "cableStayed", "cantilever", "trestle"};
    const auto                 index   = static_cast<unsigned>(kind);
    return index < std::size(names) ? names[index] : std::string_view{};
}
std::string_view bridgePartName(BridgePart part) noexcept {
    constexpr std::string_view names[] = {
        "deck",   "asphalt.bridge", "pier",    "abutment", "girder", "arch",     "truss",      "tower", "cable",
        "hanger", "railing",        "marking", "footing",  "cap",    "sidewalk", "connection", "anchor"};
    const auto index = static_cast<unsigned>(part);
    return index < std::size(names) ? names[index] : std::string_view{};
}
std::string_view bridgeMaterialName(BridgeMaterial material) noexcept {
    constexpr std::string_view names[] = {"concrete", "limestone", "paintedSteel", "galvanizedSteel",
                                          "timber",   "asphalt",   "whitePaint",   "yellowPaint"};
    const auto                 index   = static_cast<unsigned>(material);
    return index < std::size(names) ? names[index] : std::string_view{};
}
Result<BridgeParameters> bridgePreset(BridgeKind kind) {
    if (bridgeKindName(kind).empty()) return invalid<BridgeParameters>("unknown bridge preset");
    BridgeParameters p;
    p.kind              = kind;
    p.curveSegments     = 96;
    p.connectionDetails = true;
    float span = 72.f, height = 9.f;
    p.pierWidth       = 2.4f;
    p.pierDepth       = 2.5f;
    p.foundationScale = 1.8f;
    p.pierCapHeight   = 1.f;
    p.memberWidth     = .65f;
    p.memberDepth     = .75f;
    p.girderHeight    = 1.2f;
    p.panels          = 18;
    switch (kind) {
        case BridgeKind::Beam: break;
        case BridgeKind::Slab:
            span               = 28.f;
            height             = 5.f;
            p.spans            = 1;
            p.deckThickness    = .65f;
            p.foundationHeight = .65f;
            p.abutmentDepth    = 2.4f;
            p.wingWallLength   = 5.f;
            p.railing          = BridgeRailing::Concrete;
            p.railingHeight    = .95f;
            p.sidewalkWidth    = .4f;
            break;
        case BridgeKind::Arch:
            span            = 52.f;
            height          = 12.f;
            p.width         = 6.f;
            p.spans         = 1;
            p.panels        = 14;
            p.archRise      = 7.f;
            p.archRibWidth  = .95f;
            p.archRibDepth  = 1.1f;
            p.memberWidth   = .5f;
            p.memberDepth   = .55f;
            p.railing       = BridgeRailing::Masonry;
            p.railingHeight = .9f;
            break;
        case BridgeKind::TiedArch:
            span           = 64.f;
            height         = 7.f;
            p.spans        = 1;
            p.archRise     = 12.f;
            p.archRibWidth = .65f;
            p.archRibDepth = .85f;
            p.panels       = 28;
            p.memberWidth  = .35f;
            p.memberDepth  = .5f;
            p.cableRadius  = .055f;
            break;
        case BridgeKind::ThroughTruss:
            span          = 60.f;
            height        = 8.f;
            p.spans       = 1;
            p.trussHeight = 8.f;
            p.panels      = 12;
            p.portalInset = .07f;
            p.memberWidth = .42f;
            p.memberDepth = .5f;
            break;
        case BridgeKind::DeckTruss:
            span          = 84.f;
            height        = 13.f;
            p.trussHeight = 5.f;
            p.panels      = 24;
            p.pierWidth   = 2.f;
            p.pierDepth   = 2.f;
            p.memberWidth = .4f;
            p.memberDepth = .5f;
            break;
        case BridgeKind::Suspension:
            span            = 156.f;
            height          = 10.f;
            p.width         = 9.f;
            p.towerHeight   = 32.f;
            p.towerInset    = .23f;
            p.towerLegWidth = 2.1f;
            p.panels        = 60;
            p.cableRadius   = .14f;
            p.girderHeight  = 1.5f;
            p.memberWidth   = .35f;
            p.memberDepth   = .4f;
            break;
        case BridgeKind::CableStayed:
            span                 = 112.f;
            height               = 10.f;
            p.width              = 9.f;
            p.towerHeight        = 28.f;
            p.towerLegWidth      = 1.9f;
            p.panels             = 48;
            p.towerTopWidthRatio = .2f;
            p.cableRadius        = .065f;
            break;
        case BridgeKind::Cantilever:
            span                    = 140.f;
            height                  = 16.f;
            p.width                 = 8.f;
            p.trussHeight           = 20.f;
            p.panels                = 32;
            p.memberWidth           = .65f;
            p.memberDepth           = .85f;
            p.cantileverCenterRatio = .30f;
            p.portalInset           = 0.f;
            p.pierWidth             = 3.f;
            p.pierDepth             = 3.f;
            p.foundationHeight      = 1.2f;
            p.towerInset            = .28f;
            break;
        case BridgeKind::Trestle:
            span               = 56.f;
            height             = 7.f;
            p.width            = 4.f;
            p.sidewalkWidth    = 0.f;
            p.plankCount       = 160;
            p.deckThickness    = .22f;
            p.memberWidth      = .28f;
            p.memberDepth      = .32f;
            p.girderHeight     = .5f;
            p.spans            = 14;
            p.foundationHeight = .4f;
            p.railing          = BridgeRailing::Timber;
            p.railingWidth     = .1f;
            p.railingBars      = 3;
            p.markings         = false;
            break;
    }
    p.start    = {-span * .5f, height, 0.f};
    p.end      = {span * .5f, height, 0.f};
    auto valid = validate(p);
    if (!valid.ok()) return Result<BridgeParameters>::failure(valid.status());
    return Result<BridgeParameters>::success(p);
}

namespace {
class BridgePlanner {
public:
    explicit BridgePlanner(const BridgeParameters& parameters) : p(parameters) {
        side             = unit(cross({0.f, 1.f, 0.f}, p.end - p.start));
        horizontalLength = std::hypot(p.end.x - p.start.x, p.end.z - p.start.z);
        half             = p.width * .5f + p.sidewalkWidth;
        plan.cableSides  = p.cableSides;
    }
    BridgePlan build() {
        deck();
        abutments();
        railing();
        if (p.kind == BridgeKind::Beam) {
            for (float lateral : {-half * .72f, 0.f, half * .72f})
                add(point(0, -p.deckThickness - p.girderHeight * .5f, lateral),
                    point(1, -p.deckThickness - p.girderHeight * .5f, lateral), p.memberWidth * 1.6f, p.girderHeight,
                    BridgePart::Girder, BridgeSection::IBeam);
        }
        const bool towers = p.kind == BridgeKind::Suspension || p.kind == BridgeKind::CableStayed;
        if (towers || p.kind == BridgeKind::Cantilever) {
            support(p.towerInset);
            support(1.f - p.towerInset);
        } else if (p.kind != BridgeKind::Slab && p.kind != BridgeKind::TiedArch && p.kind != BridgeKind::ThroughTruss) {
            for (int i = 1; i < p.spans; ++i) support(static_cast<float>(i) / static_cast<float>(p.spans));
        }
        if (p.kind == BridgeKind::Arch || p.kind == BridgeKind::TiedArch) arches();
        if (p.kind == BridgeKind::ThroughTruss || p.kind == BridgeKind::DeckTruss || p.kind == BridgeKind::Cantilever)
            trusses();
        if (towers) cables();
        return std::move(plan);
    }

private:
    const BridgeParameters& p;
    BridgePlan              plan;
    V                       side;
    float                   half = 0.f, horizontalLength = 0.f;
    V                       point(float t, float y = 0.f, float lateral = 0.f) const {
        return p.start + (p.end - p.start) * t + V{0.f, y, 0.f} + side * lateral;
    }
    BridgeMaterial finish(BridgePart part) const {
        if (part == BridgePart::Surface) return BridgeMaterial::Asphalt;
        if (part == BridgePart::Marking) return BridgeMaterial::WhitePaint;
        if (part == BridgePart::Cable || part == BridgePart::Hanger) return BridgeMaterial::GalvanizedSteel;
        if (part == BridgePart::Connection) return BridgeMaterial::PaintedSteel;
        if (p.kind == BridgeKind::Trestle) {
            if (part == BridgePart::Footing || part == BridgePart::Abutment) return BridgeMaterial::Limestone;
            return BridgeMaterial::Timber;
        }
        if (p.kind == BridgeKind::Arch || (p.kind == BridgeKind::Suspension && part == BridgePart::Tower))
            return BridgeMaterial::Limestone;
        if (part == BridgePart::Railing) return BridgeMaterial::GalvanizedSteel;
        if (part == BridgePart::Truss || part == BridgePart::Arch ||
            (part == BridgePart::Girder && p.kind != BridgeKind::Beam))
            return BridgeMaterial::PaintedSteel;
        return BridgeMaterial::Concrete;
    }
    void add(V a, V b, float width, float height, BridgePart part, BridgeSection section = BridgeSection::Box) {
        if (length(b - a) <= 1e-4f) return;
        BridgeMember member{a, b, width, height, section, part, side};
        member.material = finish(part);
        member.uvMeters = p.uvMeters;
        plan.members.push_back(member);
    }
    void block(float t, float y, float lateral, float width, float depth, float height, BridgePart part) {
        add(point(t, y - height * .5f, lateral), point(t, y + height * .5f, lateral), width, depth, part);
    }
    void footing(float t, float lateral, float width, float depth) {
        float y = point(t).y;
        block(t, p.groundY - y + p.foundationHeight * .5f, lateral, width, depth, p.foundationHeight,
              BridgePart::Footing);
    }
    void deck() {
        if (p.kind == BridgeKind::Trestle) {
            for (int i = 0; i < p.plankCount; ++i) {
                float a = (static_cast<float>(i) + .025f) / static_cast<float>(p.plankCount);
                float b = (static_cast<float>(i) + .975f) / static_cast<float>(p.plankCount);
                add(point(a, -p.deckThickness * .5f), point(b, -p.deckThickness * .5f), half * 2.f, p.deckThickness,
                    BridgePart::Deck);
            }
            for (float lateral : {-half * .72f, 0.f, half * .72f})
                add(point(0, -p.deckThickness - p.girderHeight * .5f, lateral),
                    point(1, -p.deckThickness - p.girderHeight * .5f, lateral), p.memberWidth, p.girderHeight,
                    BridgePart::Girder);
        } else {
            add(point(0, -p.deckThickness * .5f), point(1, -p.deckThickness * .5f), half * 2.f, p.deckThickness,
                BridgePart::Deck);
            add(point(0, .015f), point(1, .015f), p.width, .03f, BridgePart::Surface);
            if (p.sidewalkWidth > .01f)
                for (float sign : {-1.f, 1.f}) {
                    float lateral = sign * (p.width * .5f + p.sidewalkWidth * .5f);
                    add(point(0, .09f, lateral), point(1, .09f, lateral), p.sidewalkWidth, .18f, BridgePart::Sidewalk);
                }
        }
        if (p.markings) {
            for (float sign : {-1.f, 1.f}) {
                add(point(0, .036f, sign * (p.width * .5f - .18f)), point(1, .036f, sign * (p.width * .5f - .18f)),
                    .09f, .006f, BridgePart::Marking);
                add(point(0, .036f, sign * .095f), point(1, .036f, sign * .095f), .075f, .006f, BridgePart::Marking);
                plan.members.back().material = BridgeMaterial::YellowPaint;
            }
        }
    }
    void abutments() {
        for (float t : {0.f, 1.f}) {
            float top    = -p.deckThickness;
            float bottom = p.groundY - point(t).y + p.foundationHeight;
            footing(t, 0.f, half * 2.f + 1.2f, p.abutmentDepth + 1.4f);
            block(t, (top + bottom) * .5f, 0.f, half * 2.f, p.abutmentDepth, top - bottom, BridgePart::Abutment);
            block(t, top - p.pierCapHeight * .5f, 0.f, half * 2.f + .45f, p.abutmentDepth + .3f, p.pierCapHeight,
                  BridgePart::Cap);
            // Stepped retaining wings are the same repeated block rule as pier capitals.
            for (float sign : {-1.f, 1.f})
                for (int step = 0; step < 4; ++step) {
                    float travel = (static_cast<float>(step) + .5f) * p.wingWallLength / 4.f;
                    float along  = t + (t == 0.f ? -1.f : 1.f) * travel / horizontalLength;
                    float height = std::max(.3f, (top - bottom) * (1.f - .18f * static_cast<float>(step)));
                    block(along, bottom + height * .5f, sign * (half + travel * .12f), .5f,
                          p.wingWallLength / 4.f + .02f, height, BridgePart::Abutment);
                }
        }
    }
    void railing() {
        if (!p.railings) return;
        if (p.railing == BridgeRailing::Concrete || p.railing == BridgeRailing::Masonry) {
            for (float lateral : {-half + .18f, half - .18f}) {
                add(point(0, p.railingHeight * .5f + .18f, lateral), point(1, p.railingHeight * .5f + .18f, lateral),
                    .38f, p.railingHeight, BridgePart::Railing);
                plan.members.back().material =
                    p.railing == BridgeRailing::Masonry ? BridgeMaterial::Limestone : BridgeMaterial::Concrete;
                add(point(0, p.railingHeight + .22f, lateral), point(1, p.railingHeight + .22f, lateral), .50f, .12f,
                    BridgePart::Cap);
                if (p.railing == BridgeRailing::Masonry)
                    for (int i = 0; i <= p.panels / 2; ++i) {
                        float t = static_cast<float>(i) / static_cast<float>(std::max(1, p.panels / 2));
                        block(t, .18f + (p.railingHeight + .3f) * .5f, lateral, .65f, .65f, p.railingHeight + .3f,
                              BridgePart::Railing);
                        block(t, .18f + p.railingHeight + .38f, lateral, .85f, .85f, .16f, BridgePart::Cap);
                    }
            }
            return;
        }
        const bool  wood = p.railing == BridgeRailing::Timber;
        const float base = p.kind == BridgeKind::Trestle ? 0.f : .18f;
        for (float lateral : {-half + .12f, half - .12f}) {
            for (int bar = 1; bar <= p.railingBars; ++bar) {
                float y = base + p.railingHeight * static_cast<float>(bar) / static_cast<float>(p.railingBars);
                add(point(0, y, lateral), point(1, y, lateral), p.railingWidth, p.railingWidth, BridgePart::Railing,
                    wood ? BridgeSection::Box : BridgeSection::Round);
                if (wood) plan.members.back().material = BridgeMaterial::Timber;
            }
            int posts = std::min(256, std::max(2, static_cast<int>(std::ceil(horizontalLength / 2.4f))));
            for (int i = 0; i <= posts; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(posts);
                add(point(t, base, lateral), point(t, base + p.railingHeight + .08f, lateral), p.railingWidth * 1.25f,
                    p.railingWidth * 1.25f, BridgePart::Railing, wood ? BridgeSection::Box : BridgeSection::Round);
                if (wood) plan.members.back().material = BridgeMaterial::Timber;
                block(t, base + .035f, lateral, p.railingWidth * 2.8f, p.railingWidth * 2.8f, .07f,
                      BridgePart::Connection);
            }
        }
    }
    void support(float t) {
        if (!p.piers) return;
        float deckBottom = -p.deckThickness;
        if (p.kind == BridgeKind::DeckTruss) deckBottom -= p.trussHeight;
        if (p.kind == BridgeKind::Trestle) {
            for (float lateral : {-half * .78f, half * .78f}) {
                footing(t, lateral, p.memberWidth * 2.7f, p.memberWidth * 2.7f);
                V a = point(t, 0.f, lateral * 1.2f);
                a.y = p.groundY + p.foundationHeight;
                V b = point(t, deckBottom, lateral);
                add(a, b, p.memberWidth, p.memberWidth, BridgePart::Pier);
            }
            add(point(t, deckBottom - p.memberWidth * .5f, -half), point(t, deckBottom - p.memberWidth * .5f, half),
                p.memberWidth, p.memberWidth, BridgePart::Cap);
            if (p.crossBracing)
                for (float sign : {-1.f, 1.f}) {
                    V foot = point(t, 0.f, sign * half * .9f);
                    foot.y = p.groundY + p.foundationHeight + .25f;
                    add(foot, point(t, deckBottom - p.memberWidth, -sign * half * .75f), p.memberWidth * .55f,
                        p.memberWidth * .55f, BridgePart::Truss);
                }
            return;
        }
        bool  twin = p.kind == BridgeKind::Suspension || p.kind == BridgeKind::CableStayed;
        float base = p.groundY - point(t).y + p.foundationHeight;
        for (int leg = 0; leg < (twin ? 2 : 1); ++leg) {
            float lateral = twin ? (leg == 0 ? -half * .90f : half * .90f) : 0.f;
            float width   = twin ? p.pierWidth : p.pierWidth * 1.7f;
            footing(t, lateral, width * p.foundationScale, p.pierDepth * p.foundationScale);
            add(point(t, base, lateral), point(t, deckBottom - p.pierCapHeight, lateral), width, p.pierDepth,
                BridgePart::Pier, BridgeSection::TaperedBox);
            plan.members.back().endWidth  = width * .8f;
            plan.members.back().endHeight = p.pierDepth * .85f;
        }
        block(t, deckBottom - p.pierCapHeight * .5f, 0.f, half * 2.f + .4f, p.pierDepth * 1.5f, p.pierCapHeight,
              BridgePart::Cap);
    }
    void joint(float t, float y, float lateral) {
        if (!p.connectionDetails) return;
        for (float sign : {-1.f, 1.f}) {
            float offset = lateral + sign * p.memberWidth * .55f;
            block(t, y, offset, .055f, p.memberDepth * 1.8f, p.memberDepth * 1.8f, BridgePart::Connection);
            for (float dx : {-.22f, .22f})
                for (float dy : {-.22f, .22f}) {
                    V a =
                        point(t + dx * p.memberDepth / horizontalLength, y + dy * p.memberDepth, offset - sign * .04f);
                    add(a, a + side * (sign * .10f), .07f, .07f, BridgePart::Connection, BridgeSection::Round);
                    plan.members.back().material = BridgeMaterial::GalvanizedSteel;
                }
        }
    }
    void arches() {
        bool above = p.kind == BridgeKind::TiedArch;
        int  spans = above ? 1 : p.spans;
        for (int span = 0; span < spans; ++span) {
            auto position = [&](float u, float lateral) {
                float t = (static_cast<float>(span) + u) / static_cast<float>(spans);
                float y =
                    above ? 4.f * p.archRise * u * (1.f - u)
                          : -p.deckThickness - p.archRibDepth * .5f - p.archRise * (2.f * u - 1.f) * (2.f * u - 1.f);
                return point(t, y, lateral);
            };
            for (float lateral : {-half * .85f, half * .85f}) {
                for (int i = 0; i < p.curveSegments; ++i) {
                    float a = static_cast<float>(i) / static_cast<float>(p.curveSegments),
                          b = static_cast<float>(i + 1) / static_cast<float>(p.curveSegments);
                    add(position(a, lateral), position(b, lateral), p.archRibWidth, p.archRibDepth, BridgePart::Arch,
                        above ? BridgeSection::IBeam : BridgeSection::Box);
                }
                for (int i = 1; i < p.panels; ++i) {
                    float u = static_cast<float>(i) / static_cast<float>(p.panels),
                          t = (static_cast<float>(span) + u) / static_cast<float>(spans);
                    add(position(u, lateral), point(t, above ? -.1f : -p.deckThickness, lateral),
                        above ? p.cableRadius * 2.f : p.memberWidth, above ? p.cableRadius * 2.f : p.memberWidth,
                        above ? BridgePart::Hanger : BridgePart::Pier,
                        above ? BridgeSection::Round : BridgeSection::Box);
                }
                if (above)
                    add(point(0, -p.deckThickness, lateral), point(1, -p.deckThickness, lateral), p.archRibWidth,
                        p.archRibDepth, BridgePart::Girder, BridgeSection::IBeam);
            }
            if (above && p.crossBracing)
                for (int i = 1; i < 6; ++i) {
                    float u = static_cast<float>(i) / 6.f;
                    add(position(u, -half * .85f), position(u, half * .85f), p.memberWidth, p.memberDepth,
                        BridgePart::Arch, BridgeSection::IBeam);
                }
        }
    }
    void trusses() {
        bool below  = p.kind == BridgeKind::DeckTruss;
        auto height = [&](float t) {
            if (p.kind == BridgeKind::Cantilever) {
                float distance = std::min(std::fabs(t - p.towerInset), std::fabs(t - (1.f - p.towerInset)));
                return p.trussHeight * std::max(p.cantileverCenterRatio, 1.f - distance / p.towerInset);
            }
            if (!below && p.portalInset > 0.f)
                return p.trussHeight * std::min({1.f, t / p.portalInset, (1.f - t) / p.portalInset});
            return p.trussHeight;
        };
        const float base = below ? -p.deckThickness : 0.f;
        for (float lateral : {-half * .93f, half * .93f}) {
            for (int i = 0; i < p.panels; ++i) {
                float a  = static_cast<float>(i) / static_cast<float>(p.panels),
                      b  = static_cast<float>(i + 1) / static_cast<float>(p.panels);
                float ya = base + (below ? -height(a) : height(a)), yb = base + (below ? -height(b) : height(b));
                add(point(a, base, lateral), point(b, base, lateral), p.memberWidth, p.memberDepth, BridgePart::Truss,
                    BridgeSection::IBeam);
                add(point(a, ya, lateral), point(b, yb, lateral), p.memberWidth, p.memberDepth, BridgePart::Truss,
                    BridgeSection::IBeam);
                add(point(a, base, lateral), point(a, ya, lateral), p.memberWidth * .8f, p.memberDepth * .8f,
                    BridgePart::Truss, BridgeSection::IBeam);
                add(point(a, i % 2 ? ya : base, lateral), point(b, i % 2 ? base : yb, lateral), p.memberWidth * .8f,
                    p.memberDepth * .8f, BridgePart::Truss, BridgeSection::IBeam);
                joint(a, base, lateral);
                if (std::fabs(ya - base) > .01f) joint(a, ya, lateral);
            }
            add(point(1, base, lateral), point(1, base + (below ? -height(1) : height(1)), lateral), p.memberWidth,
                p.memberDepth, BridgePart::Truss, BridgeSection::IBeam);
        }
        if (p.crossBracing)
            for (int i = 1; i < p.panels; ++i) {
                float t    = static_cast<float>(i) / static_cast<float>(p.panels),
                      next = static_cast<float>(i + 1) / static_cast<float>(p.panels);
                float y = base + (below ? -height(t) : height(t)), yn = base + (below ? -height(next) : height(next));
                if (!below && std::min(y, yn) < p.railingHeight * 2.f) continue;
                add(point(t, y, -half * .93f), point(t, y, half * .93f), p.memberWidth * .7f, p.memberDepth * .7f,
                    BridgePart::Truss, BridgeSection::IBeam);
                add(point(t, y, -half * .93f), point(next, yn, half * .93f), p.memberWidth * .45f, p.memberDepth * .45f,
                    BridgePart::Truss, BridgeSection::IBeam);
                add(point(t, y, half * .93f), point(next, yn, -half * .93f), p.memberWidth * .45f, p.memberDepth * .45f,
                    BridgePart::Truss, BridgeSection::IBeam);
            }
    }
    void cables() {
        const bool  suspension = p.kind == BridgeKind::Suspension;
        const float a = p.towerInset, b = 1.f - a, topRatio = suspension ? 1.f : p.towerTopWidthRatio;
        for (float t : {a, b}) {
            for (float sign : {-1.f, 1.f}) {
                float lateral = sign * half * .90f;
                add(point(t, -p.deckThickness, lateral), point(t, p.towerHeight, lateral * topRatio), p.towerLegWidth,
                    p.towerLegWidth * 1.15f, BridgePart::Tower, BridgeSection::TaperedBox);
                plan.members.back().endWidth  = p.towerLegWidth * .78f;
                plan.members.back().endHeight = p.towerLegWidth * .90f;
                if (suspension) {
                    block(t, p.towerHeight + .18f, lateral, p.towerLegWidth * 1.5f, p.towerLegWidth * 1.6f, .36f,
                          BridgePart::Cap);
                    plan.members.back().material = BridgeMaterial::Limestone;
                }
            }
            if (suspension) {
                for (float level : {.30f, .67f}) {
                    float y = p.towerHeight * level;
                    // A repeated parabolic lintel makes two open masonry portals in each tower.
                    for (int i = 0; i < 20; ++i) {
                        float u = static_cast<float>(i) / 20.f, v = static_cast<float>(i + 1) / 20.f;
                        float ya = y + 2.0f * (1.f - (2.f * u - 1.f) * (2.f * u - 1.f));
                        float yb = y + 2.0f * (1.f - (2.f * v - 1.f) * (2.f * v - 1.f));
                        add(point(t, ya, (2.f * u - 1.f) * half * .9f), point(t, yb, (2.f * v - 1.f) * half * .9f),
                            p.towerLegWidth * .7f, p.towerLegWidth * .7f, BridgePart::Tower);
                    }
                    block(t, y + 2.3f, 0.f, half * 1.8f, p.towerLegWidth, .5f, BridgePart::Tower);
                }
            } else {
                float y = p.towerHeight * .2f, ratio = 1.f - (1.f - topRatio) * .2f;
                add(point(t, y, -half * .9f * ratio), point(t, y, half * .9f * ratio), p.towerLegWidth * .8f,
                    p.towerLegWidth * .6f, BridgePart::Tower);
            }
        }
        for (float lateral : {-half * .90f, half * .90f}) {
            auto cable = [&](V from, V to, BridgePart part, float radius) {
                add(from, to, radius * 2.f, radius * 2.f, part, BridgeSection::Round);
            };
            if (suspension) {
                auto cableY = [&](float t) {
                    if (t < a) return p.towerHeight * t / a;
                    if (t > b) return p.towerHeight * (1.f - t) / a;
                    float u = (t - a) / (b - a);
                    return p.towerHeight * (1.f - p.cableSag * 4.f * u * (1.f - u));
                };
                cable(point(0, .2f, lateral), point(a, p.towerHeight, lateral), BridgePart::Cable,
                      p.cableRadius * 1.6f);
                cable(point(b, p.towerHeight, lateral), point(1, .2f, lateral), BridgePart::Cable,
                      p.cableRadius * 1.6f);
                for (int i = 0; i < p.curveSegments; ++i) {
                    float t0 = a + (b - a) * static_cast<float>(i) / static_cast<float>(p.curveSegments),
                          t1 = a + (b - a) * static_cast<float>(i + 1) / static_cast<float>(p.curveSegments);
                    cable(point(t0, cableY(t0), lateral), point(t1, cableY(t1), lateral), BridgePart::Cable,
                          p.cableRadius * 1.6f);
                }
                for (int i = 1; i < p.panels; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(p.panels);
                    cable(point(t, .05f, lateral), point(t, cableY(t), lateral), BridgePart::Hanger,
                          p.cableRadius * .42f);
                }
                for (float t : {0.f, 1.f})
                    block(t, .4f, lateral, p.towerLegWidth, p.abutmentDepth * .7f, .9f, BridgePart::Anchor);
                for (float t : {a, b})
                    block(t, p.towerHeight, lateral, p.towerLegWidth * .8f, p.towerLegWidth * 1.3f, .5f,
                          BridgePart::Anchor);
            } else {
                for (int i = 0; i <= p.panels; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(p.panels), tower = t <= .5f ? a : b;
                    if (std::fabs(t - tower) < .035f) continue;
                    float fan = .7f + .3f * std::min(1.f, std::fabs(t - tower) / a);
                    cable(point(t, .10f, lateral),
                          point(tower, p.towerHeight * fan, lateral * (1.f - (1.f - topRatio) * fan)),
                          BridgePart::Cable, p.cableRadius);
                }
            }
        }
        if (suspension) {
            for (float lateral : {-half * .75f, half * .75f})
                add(point(0, -p.deckThickness - p.girderHeight * .5f, lateral),
                    point(1, -p.deckThickness - p.girderHeight * .5f, lateral), p.memberWidth, p.girderHeight,
                    BridgePart::Girder, BridgeSection::IBeam);
            if (p.crossBracing)
                for (int i = 0; i < p.panels; ++i) {
                    float a0 = static_cast<float>(i) / static_cast<float>(p.panels),
                          b0 = static_cast<float>(i + 1) / static_cast<float>(p.panels);
                    for (float lateral : {-half * .75f, half * .75f})
                        add(point(a0, -p.deckThickness, lateral), point(b0, -p.deckThickness - p.girderHeight, lateral),
                            p.memberWidth * .5f, p.memberWidth * .5f, BridgePart::Truss, BridgeSection::IBeam);
                }
        }
    }
};
}  // namespace

Result<BridgePlan> planBridge(const BridgeParameters& p) {
    auto valid = validate(p);
    if (!valid.ok()) return Result<BridgePlan>::failure(valid.status());
    auto plan = BridgePlanner(p).build();
    if (plan.members.size() > 16384u) return invalid<BridgePlan>("bridge plan exceeds member budget");
    return Result<BridgePlan>::success(std::move(plan));
}

Result<MeshBuild> bakeBridge(const BridgePlan& plan) {
    if (plan.members.empty() || plan.members.size() > 16384u || plan.cableSides < 3 || plan.cableSides > 16)
        return invalid<MeshBuild>("invalid bridge member count or cable tessellation");
    std::size_t elements = 0;
    for (const auto& m : plan.members) {
        if (!finite(m.from) || !finite(m.to) || !finite(m.widthAxis) || !std::isfinite(length(m.to - m.from)) ||
            length(m.to - m.from) < 1e-5f || !std::isfinite(m.width) || !std::isfinite(m.height) || m.width < .001f ||
            m.height < .001f || m.width > 1000.f || m.height > 1000.f || m.section < BridgeSection::Box ||
            m.section > BridgeSection::TaperedBox || bridgePartName(m.part).empty() ||
            bridgeMaterialName(m.material).empty() || !std::isfinite(m.uvMeters) || m.uvMeters < .01f ||
            m.uvMeters > 1000.f)
            return invalid<MeshBuild>("invalid or degenerate bridge member");
        if (m.section == BridgeSection::TaperedBox &&
            (!std::isfinite(m.endWidth) || !std::isfinite(m.endHeight) || m.endWidth < .001f || m.endHeight < .001f ||
             m.endWidth > 1000.f || m.endHeight > 1000.f))
            return invalid<MeshBuild>("tapered section requires bounded positive end dimensions");
        elements += m.section == BridgeSection::Round ? static_cast<std::size_t>(plan.cableSides) * 22u
                                                      : (m.section == BridgeSection::IBeam ? 180u : 60u);
    }
    if (elements > 4000000u) return invalid<MeshBuild>("bridge mesh exceeds element budget");
    MeshBuild mesh;
    for (const auto& m : plan.members) {
        mesh.setActiveGroup(std::string(bridgePartName(m.part)) + "@" + std::string(bridgeMaterialName(m.material)));
        V       forward   = unit(m.to - m.from);
        V       right     = unit(cross(std::fabs(forward.y) > .99f ? V{0, 0, 1} : V{0, 1, 0}, forward));
        const V projected = m.widthAxis - forward * dot(m.widthAxis, forward);
        if (length(projected) > 1e-5f) right = unit(projected);
        V     up     = unit(cross(forward, right));
        float travel = length(m.to - m.from);
        auto  vertex = [&](V pos, V normal, float u, float v) {
            if (m.material == BridgeMaterial::Timber) std::swap(u, v);
            mesh.addVertex(pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, u / m.uvMeters, v / m.uvMeters);
        };
        auto quad = [&](V a, V b, V c, V d, V expected, float u, float v) {
            auto base      = static_cast<std::uint32_t>(mesh.getVertexCount());
            V    geometric = cross(b - a, c - a);
            bool positive  = dot(geometric, expected) > 0.f;
            V    normal    = unit(geometric) * (positive ? 1.f : -1.f);
            vertex(a, normal, 0, 0);
            vertex(b, normal, u, 0);
            vertex(c, normal, u, v);
            vertex(d, normal, 0, v);
            if (positive) {
                mesh.addTriangle(base, base + 1, base + 2);
                mesh.addTriangle(base, base + 2, base + 3);
            } else {
                mesh.addTriangle(base, base + 2, base + 1);
                mesh.addTriangle(base, base + 3, base + 2);
            }
        };
        auto box = [&](V from, V to, float width, float height, float endWidth, float endHeight) {
            V r = right * (width * .5f), u = up * (height * .5f), r1 = right * (endWidth * .5f),
              u1                    = up * (endHeight * .5f);
            std::array<V, 4> start  = {from - r - u, from + r - u, from + r + u, from - r + u};
            std::array<V, 4> end    = {to - r1 - u1, to + r1 - u1, to + r1 + u1, to - r1 + u1};
            std::array<V, 4> normal = {up * -1.f, right, up, right * -1.f};
            for (int i = 0; i < 4; ++i)
                quad(start[i], end[i], end[(i + 1) % 4], start[(i + 1) % 4], normal[i], travel, i % 2 ? height : width);
            quad(start[0], start[1], start[2], start[3], forward * -1.f, width, height);
            quad(end[0], end[1], end[2], end[3], forward, endWidth, endHeight);
        };
        if (m.section == BridgeSection::Box || m.section == BridgeSection::TaperedBox) {
            box(m.from, m.to, m.width, m.height, m.section == BridgeSection::Box ? m.width : m.endWidth,
                m.section == BridgeSection::Box ? m.height : m.endHeight);
        } else if (m.section == BridgeSection::IBeam) {
            float flange = m.height * .16f, offset = (m.height - flange) * .5f;
            box(m.from + up * offset, m.to + up * offset, m.width, flange, m.width, flange);
            box(m.from - up * offset, m.to - up * offset, m.width, flange, m.width, flange);
            box(m.from, m.to, m.width * .18f, m.height - 2.f * flange, m.width * .18f, m.height - 2.f * flange);
        } else {
            for (int i = 0; i < plan.cableSides; ++i) {
                float a  = 6.28318530718f * static_cast<float>(i) / static_cast<float>(plan.cableSides);
                float b  = 6.28318530718f * static_cast<float>(i + 1) / static_cast<float>(plan.cableSides);
                V     na = right * std::cos(a) + up * std::sin(a), nb = right * std::cos(b) + up * std::sin(b);
                V     ra = na * (m.width * .5f), rb = nb * (m.width * .5f);
                auto  base = static_cast<std::uint32_t>(mesh.getVertexCount());
                vertex(m.from + ra, na, 0, a * m.width * .5f);
                vertex(m.to + ra, na, travel, a * m.width * .5f);
                vertex(m.to + rb, nb, travel, b * m.width * .5f);
                vertex(m.from + rb, nb, 0, b * m.width * .5f);
                bool positive = dot(cross(m.to - m.from, rb - ra), na + nb) > 0.f;
                if (positive) {
                    mesh.addTriangle(base, base + 1, base + 2);
                    mesh.addTriangle(base, base + 2, base + 3);
                } else {
                    mesh.addTriangle(base, base + 2, base + 1);
                    mesh.addTriangle(base, base + 3, base + 2);
                }
                auto cap = [&](V center, V x, V y, V n) {
                    auto index = static_cast<std::uint32_t>(mesh.getVertexCount());
                    vertex(center, n, .5f, .5f);
                    vertex(x, n, 0, 0);
                    vertex(y, n, 1, 0);
                    if (dot(cross(x - center, y - center), n) > 0)
                        mesh.addTriangle(index, index + 1, index + 2);
                    else
                        mesh.addTriangle(index, index + 2, index + 1);
                };
                cap(m.from, m.from + ra, m.from + rb, forward * -1.f);
                cap(m.to, m.to + ra, m.to + rb, forward);
            }
        }
    }
    // Float world coordinates can collapse tiny custom sections. Reject the private candidate,
    // rather than publishing NaN normals or zero-area triangles.
    for (int vertexIndex = 0; vertexIndex < mesh.getVertexCount(); ++vertexIndex) {
        if (!std::isfinite(mesh.getNormalX(vertexIndex)) || !std::isfinite(mesh.getNormalY(vertexIndex)) ||
            !std::isfinite(mesh.getNormalZ(vertexIndex)))
            return invalid<MeshBuild>("section precision is insufficient at these world coordinates");
    }
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int    a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        const double ux = double(mesh.getPositionX(b)) - mesh.getPositionX(a);
        const double uy = double(mesh.getPositionY(b)) - mesh.getPositionY(a);
        const double uz = double(mesh.getPositionZ(b)) - mesh.getPositionZ(a);
        const double vx = double(mesh.getPositionX(c)) - mesh.getPositionX(a);
        const double vy = double(mesh.getPositionY(c)) - mesh.getPositionY(a);
        const double vz = double(mesh.getPositionZ(c)) - mesh.getPositionZ(a);
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (!(nx * nx + ny * ny + nz * nz > 1e-24))
            return invalid<MeshBuild>("section precision collapsed a generated triangle");
    }
    return Result<MeshBuild>::success(std::move(mesh));
}

Result<BridgeParameters> bridgeParametersFromParams(const Params& params) {
    auto number = [&](const char* key, float defaultValue) {
        return params.getFloat(key, params.has(key) ? std::numeric_limits<float>::quiet_NaN() : defaultValue);
    };
    auto count = [&](const char* key, int defaultValue) {
        return params.getInt(key, params.has(key) ? -1 : defaultValue);
    };
    for (const auto* key : {"piers", "railings", "markings", "crossBracing", "connectionDetails"}) {
        if (params.has(key) && params.getBool(key, true) != params.getBool(key, false))
            return invalid<BridgeParameters>("bridge switches must be bool or exact numeric 0/1");
    }
    const auto type = params.getString("bridgeType", params.has("bridgeType") ? "invalid" : "beam");
    BridgeKind kind = static_cast<BridgeKind>(99);
    for (int i = 0; i < 10; ++i)
        if (bridgeKindName(static_cast<BridgeKind>(i)) == type) kind = static_cast<BridgeKind>(i);
    auto preset = bridgePreset(kind);
    if (!preset.ok()) return Result<BridgeParameters>::failure(preset.status());
    auto p                  = std::move(preset).takeValue();
    p.start.x               = number("startX", p.start.x);
    p.start.y               = number("startY", p.start.y);
    p.start.z               = number("startZ", p.start.z);
    p.end.x                 = number("endX", p.end.x);
    p.end.y                 = number("endY", p.end.y);
    p.end.z                 = number("endZ", p.end.z);
    p.width                 = number("deckWidth", p.width);
    p.sidewalkWidth         = number("sidewalkWidth", p.sidewalkWidth);
    p.deckThickness         = number("deckThickness", p.deckThickness);
    p.groundY               = number("groundY", p.groundY);
    p.memberWidth           = number("memberWidth", p.memberWidth);
    p.memberDepth           = number("memberDepth", p.memberDepth);
    p.girderHeight          = number("girderHeight", p.girderHeight);
    p.pierWidth             = number("pierWidth", p.pierWidth);
    p.pierDepth             = number("pierDepth", p.pierDepth);
    p.archRise              = number("archRise", p.archRise);
    p.archRibWidth          = number("archRibWidth", p.archRibWidth);
    p.archRibDepth          = number("archRibDepth", p.archRibDepth);
    p.towerHeight           = number("towerHeight", p.towerHeight);
    p.towerLegWidth         = number("towerLegWidth", p.towerLegWidth);
    p.trussHeight           = number("trussHeight", p.trussHeight);
    p.cableRadius           = number("cableRadius", p.cableRadius);
    p.cableSag              = number("cableSag", p.cableSag);
    p.towerInset            = number("towerInset", p.towerInset);
    p.towerTopWidthRatio    = number("towerTopWidthRatio", p.towerTopWidthRatio);
    p.railingHeight         = number("railingHeight", p.railingHeight);
    p.railingWidth          = number("railingWidth", p.railingWidth);
    p.foundationHeight      = number("foundationHeight", p.foundationHeight);
    p.foundationScale       = number("foundationScale", p.foundationScale);
    p.pierCapHeight         = number("pierCapHeight", p.pierCapHeight);
    p.abutmentDepth         = number("abutmentDepth", p.abutmentDepth);
    p.wingWallLength        = number("wingWallLength", p.wingWallLength);
    p.uvMeters              = number("uvMeters", p.uvMeters);
    p.portalInset           = number("portalInset", p.portalInset);
    p.cantileverCenterRatio = number("cantileverCenterRatio", p.cantileverCenterRatio);
    p.spans                 = count("spans", p.spans);
    p.panels                = count("panels", p.panels);
    p.curveSegments         = count("curveSegments", p.curveSegments);
    p.cableSides            = count("cableSides", p.cableSides);
    p.railingBars           = count("railingBars", p.railingBars);
    p.plankCount            = count("plankCount", p.plankCount);
    p.piers                 = params.getBool("piers", p.piers);
    p.railings              = params.getBool("railings", p.railings);
    p.markings              = params.getBool("markings", p.markings);
    p.crossBracing          = params.getBool("crossBracing", p.crossBracing);
    p.connectionDetails     = params.getBool("connectionDetails", p.connectionDetails);
    if (params.has("railingType")) {
        auto                       name    = params.getString("railingType", "invalid");
        constexpr std::string_view names[] = {"metal", "concrete", "masonry", "timber"};
        auto                       it      = std::find(std::begin(names), std::end(names), name);
        if (it == std::end(names)) return invalid<BridgeParameters>("unknown railingType");
        p.railing = static_cast<BridgeRailing>(it - std::begin(names));
    }
    auto valid = validate(p);
    if (!valid.ok()) return Result<BridgeParameters>::failure(valid.status());
    return Result<BridgeParameters>::success(p);
}
Result<void> applyBridgeRecipeDefaults(Params& params) {
    auto decoded = bridgeParametersFromParams(params);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto& p         = decoded.value();
    Params      candidate = params;
    if (!candidate.has("bridgeType")) candidate.setString("bridgeType", std::string(bridgeKindName(p.kind)));
    if (!candidate.has("startX")) candidate.setFloat("startX", p.start.x);
    if (!candidate.has("startY")) candidate.setFloat("startY", p.start.y);
    if (!candidate.has("startZ")) candidate.setFloat("startZ", p.start.z);
    if (!candidate.has("endX")) candidate.setFloat("endX", p.end.x);
    if (!candidate.has("endY")) candidate.setFloat("endY", p.end.y);
    if (!candidate.has("endZ")) candidate.setFloat("endZ", p.end.z);
    if (!candidate.has("deckWidth")) candidate.setFloat("deckWidth", p.width);
    if (!candidate.has("sidewalkWidth")) candidate.setFloat("sidewalkWidth", p.sidewalkWidth);
    if (!candidate.has("deckThickness")) candidate.setFloat("deckThickness", p.deckThickness);
    if (!candidate.has("groundY")) candidate.setFloat("groundY", p.groundY);
    if (!candidate.has("memberWidth")) candidate.setFloat("memberWidth", p.memberWidth);
    if (!candidate.has("memberDepth")) candidate.setFloat("memberDepth", p.memberDepth);
    if (!candidate.has("girderHeight")) candidate.setFloat("girderHeight", p.girderHeight);
    if (!candidate.has("pierWidth")) candidate.setFloat("pierWidth", p.pierWidth);
    if (!candidate.has("pierDepth")) candidate.setFloat("pierDepth", p.pierDepth);
    if (!candidate.has("archRise")) candidate.setFloat("archRise", p.archRise);
    if (!candidate.has("archRibWidth")) candidate.setFloat("archRibWidth", p.archRibWidth);
    if (!candidate.has("archRibDepth")) candidate.setFloat("archRibDepth", p.archRibDepth);
    if (!candidate.has("towerHeight")) candidate.setFloat("towerHeight", p.towerHeight);
    if (!candidate.has("towerLegWidth")) candidate.setFloat("towerLegWidth", p.towerLegWidth);
    if (!candidate.has("trussHeight")) candidate.setFloat("trussHeight", p.trussHeight);
    if (!candidate.has("cableRadius")) candidate.setFloat("cableRadius", p.cableRadius);
    if (!candidate.has("cableSag")) candidate.setFloat("cableSag", p.cableSag);
    if (!candidate.has("towerInset")) candidate.setFloat("towerInset", p.towerInset);
    if (!candidate.has("towerTopWidthRatio")) candidate.setFloat("towerTopWidthRatio", p.towerTopWidthRatio);
    if (!candidate.has("railingHeight")) candidate.setFloat("railingHeight", p.railingHeight);
    if (!candidate.has("railingWidth")) candidate.setFloat("railingWidth", p.railingWidth);
    if (!candidate.has("foundationHeight")) candidate.setFloat("foundationHeight", p.foundationHeight);
    if (!candidate.has("foundationScale")) candidate.setFloat("foundationScale", p.foundationScale);
    if (!candidate.has("pierCapHeight")) candidate.setFloat("pierCapHeight", p.pierCapHeight);
    if (!candidate.has("abutmentDepth")) candidate.setFloat("abutmentDepth", p.abutmentDepth);
    if (!candidate.has("wingWallLength")) candidate.setFloat("wingWallLength", p.wingWallLength);
    if (!candidate.has("uvMeters")) candidate.setFloat("uvMeters", p.uvMeters);
    if (!candidate.has("portalInset")) candidate.setFloat("portalInset", p.portalInset);
    if (!candidate.has("cantileverCenterRatio")) candidate.setFloat("cantileverCenterRatio", p.cantileverCenterRatio);
    if (!candidate.has("spans")) candidate.setInt("spans", p.spans);
    if (!candidate.has("panels")) candidate.setInt("panels", p.panels);
    if (!candidate.has("curveSegments")) candidate.setInt("curveSegments", p.curveSegments);
    if (!candidate.has("cableSides")) candidate.setInt("cableSides", p.cableSides);
    if (!candidate.has("railingBars")) candidate.setInt("railingBars", p.railingBars);
    if (!candidate.has("plankCount")) candidate.setInt("plankCount", p.plankCount);
    if (!candidate.has("piers")) candidate.setBool("piers", p.piers);
    if (!candidate.has("railings")) candidate.setBool("railings", p.railings);
    if (!candidate.has("markings")) candidate.setBool("markings", p.markings);
    if (!candidate.has("crossBracing")) candidate.setBool("crossBracing", p.crossBracing);
    if (!candidate.has("connectionDetails")) candidate.setBool("connectionDetails", p.connectionDetails);
    constexpr std::string_view railingNames[] = {"metal", "concrete", "masonry", "timber"};
    if (!candidate.has("railingType"))
        candidate.setString("railingType", std::string(railingNames[static_cast<unsigned>(p.railing)]));
    params = std::move(candidate);
    return Result<void>::success();
}
}  // namespace eve::procgen::road
