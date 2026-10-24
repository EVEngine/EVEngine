#include "procgen/mesh/ArrowGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {
using V = std::array<double, 3>;
V      add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V      sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V      mul(V a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V      cross(V a, V b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double length(V a) { return std::sqrt(dot(a, a)); }
V      unit(V a) { return mul(a, 1. / length(a)); }
bool   valid(V a) {
    return std::all_of(a.begin(), a.end(), [](double v) { return std::isfinite(v) && std::abs(v) <= 1e6; });
}
Diagnostic invalid(std::string message = "Invalid arrow parameters or degenerate path") {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "arrow", {}, "procgen.arrow");
}
void triangle(MeshBuild& mesh, V a, V b, V c, double u0, double u1, double u2) {
    const V      n    = unit(cross(sub(b, a), sub(c, a)));
    const auto   base = static_cast<uint32_t>(mesh.getVertexCount());
    const V      points[]{a, b, c};
    const double us[]{u0, u1, u2};
    for (int i = 0; i < 3; ++i)
        mesh.addVertex(static_cast<float>(points[i][0]), static_cast<float>(points[i][1]),
                       static_cast<float>(points[i][2]), static_cast<float>(n[0]), static_cast<float>(n[1]),
                       static_cast<float>(n[2]), static_cast<float>(us[i]), i == 0 ? 0.f : 1.f);
    mesh.addTriangle(base, base + 1, base + 2);
}
void quad(MeshBuild& m, V a, V b, V c, V d, double u, double v) {
    triangle(m, a, b, c, u, u, v);
    triangle(m, a, c, d, u, v, v);
}
double flowingRatio(double t) { return 0.08 + 0.92 * std::sin(t * std::numbers::pi * 0.5); }
struct PathSample {
    double progress, lateral;
};
PathSample samplePath(V p, const std::vector<V>& path, const std::vector<double>& arc) {
    double best = std::numeric_limits<double>::max(), result = 1.;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const V      delta      = sub(path[i + 1], path[i]);
        const double t          = std::clamp(dot(sub(p, path[i]), delta) / dot(delta, delta), 0., 1.);
        const V      difference = sub(p, add(path[i], mul(delta, t)));
        const double squared    = dot(difference, difference);
        if (squared < best) {
            best   = squared;
            result = (arc[i] + (arc[i + 1] - arc[i]) * t) / arc.back();
        }
    }
    const double z = p[2] - path.front()[2];
    return {result, std::sqrt(std::max(0., best - z * z))};
}
// Closed thin extrusion with rounded outline corners and quarter-round rim bevels.
double            turn(V a, V b, V c) { return cross(sub(b, a), sub(c, a))[2]; }
Result<MeshBuild> flatArrow(const std::vector<V>& path, const std::vector<V>& normals, V tip, double shaft, double head,
                            double thickness, double radius, int steps, bool flowing) {
    if (radius >= shaft * 0.25) return Result<MeshBuild>::failure(invalid());
    std::vector<double> arc{0.};
    for (std::size_t i = 1; i < path.size(); ++i) arc.push_back(arc.back() + length(sub(path[i], path[i - 1])));
    const auto     taper    = [&](double t) { return flowing ? flowingRatio(t) : 1.; };
    const auto     progress = [&](V p) { return samplePath(p, path, arc).progress; };
    std::vector<V> outline;
    for (std::size_t i = 0; i < path.size(); ++i)
        outline.push_back(sub(path[i], mul(normals[i], shaft * taper(arc[i] / arc.back()) * 0.5)));
    outline.push_back(sub(path.back(), mul(normals.back(), head * 0.5)));
    outline.push_back(tip);
    outline.push_back(add(path.back(), mul(normals.back(), head * 0.5)));
    for (std::size_t i = path.size(); i-- > 0;)
        outline.push_back(add(path[i], mul(normals[i], shaft * taper(arc[i] / arc.back()) * 0.5)));
    // Remove collinear samples before rounding so straight shafts remain inexpensive.
    for (std::size_t i = 0; outline.size() > 3 && i < outline.size();) {
        const std::size_t n = outline.size();
        if (std::abs(turn(outline[(i + n - 1) % n], outline[i], outline[(i + 1) % n])) < 1e-12) {
            outline.erase(outline.begin() + static_cast<std::ptrdiff_t>(i));
            if (i > 0) --i;
        } else
            ++i;
    }
    if (flowing) {
        // Reserve a visible fillet span at both shoulders instead of letting
        // dense curve samples force the corner radius down to one tiny edge.
        const V      necks[]{sub(path.back(), mul(normals.back(), shaft * 0.5)),
                             add(path.back(), mul(normals.back(), shaft * 0.5))};
        const double span = std::min(shaft * 0.7, radius * 10.);
        for (V neck : necks) {
            bool shortened = true;
            while (shortened && outline.size() > 7) {
                shortened               = false;
                const auto        found = std::find(outline.begin(), outline.end(), neck);
                const std::size_t i     = static_cast<std::size_t>(found - outline.begin());
                for (std::size_t j : {(i + outline.size() - 1) % outline.size(), (i + 1) % outline.size()}) {
                    if (length(sub(outline[j], neck)) < span && outline[j] != necks[0] && outline[j] != necks[1] &&
                        outline[j] != tip) {
                        outline.erase(outline.begin() + static_cast<std::ptrdiff_t>(j));
                        shortened = true;
                        break;
                    }
                }
            }
        }
    }
    std::vector<V> contour;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const V p = outline[(i + outline.size() - 1) % outline.size()], v = outline[i],
                q             = outline[(i + 1) % outline.size()];
        const V      incoming = sub(p, v), outgoing = sub(q, v);
        const double cosine     = dot(unit(incoming), unit(outgoing));
        const double desiredCut = 2. * radius * (flowing && v != tip ? 3. : 1.) *
                                  std::sqrt(std::max(0., (1. + cosine) * 0.5)) / std::max(1e-8, (1. - cosine) * 0.5);
        const double cut = std::min({desiredCut, length(incoming) * 0.45, length(outgoing) * 0.45});
        const V      a = add(v, mul(unit(incoming), cut)), b = add(v, mul(unit(outgoing), cut));
        // Already smooth path samples need no additional corner subdivision.
        if (dot(unit(incoming), unit(outgoing)) < -0.99) {
            contour.push_back(v);
            continue;
        }
        for (int j = 0; j <= steps; ++j) {
            const double t = static_cast<double>(j) / steps;
            contour.push_back(add(add(mul(a, (1 - t) * (1 - t)), mul(v, 2 * t * (1 - t))), mul(b, t * t)));
        }
    }
    const std::size_t count = contour.size();
    std::vector<V>    insetDirections;
    for (std::size_t i = 0; i < count; ++i) {
        const V      prev = unit(sub(contour[i], contour[(i + count - 1) % count]));
        const V      next = unit(sub(contour[(i + 1) % count], contour[i]));
        const V      n0{-prev[1], prev[0], 0.}, n1{-next[1], next[0], 0.};
        const double denominator = 1. + dot(n0, n1);
        if (denominator < 1e-6) return Result<MeshBuild>::failure(invalid());
        insetDirections.push_back(mul(add(n0, n1), 1. / denominator));
    }
    std::vector<double> radii, thicknesses;
    for (V point : contour) {
        const double ratio = flowing ? taper(progress(point)) : 1.;
        const double h     = thickness;
        thicknesses.push_back(h);
        radii.push_back(flowing ? std::min({radius, shaft * ratio * 0.2, h * 0.45}) : radius);
    }
    std::vector<std::vector<V>> rings;
    const auto                  appendRing = [&](double insetFactor, double sign, double riseFactor) {
        std::vector<V> ring;
        for (std::size_t i = 0; i < count; ++i) {
            V v = add(contour[i], mul(insetDirections[i], radii[i] * insetFactor));
            v[2] += sign * thicknesses[i] * 0.5 + radii[i] * riseFactor;
            ring.push_back(v);
        }
        rings.push_back(std::move(ring));
    };
    for (int j = 0; j <= steps; ++j) {
        const double angle = std::numbers::pi * 0.5 * j / steps;
        appendRing(1. - std::sin(angle), -1., 1. - std::cos(angle));
    }
    appendRing(0., 1., -1.);
    for (int j = 1; j <= steps; ++j) {
        const double angle = std::numbers::pi * 0.5 * j / steps;
        appendRing(1. - std::cos(angle), 1., std::sin(angle) - 1.);
    }
    // Reject overlapping inset contours instead of publishing a broken bevel.
    for (const auto& ring : rings) {
        for (std::size_t i = 0; i < count; ++i)
            for (std::size_t j = i + 2; j < count; ++j) {
                if (i == 0 && j + 1 == count) continue;
                const V a = ring[i], b = ring[(i + 1) % count], c = ring[j], d = ring[(j + 1) % count];
                if (turn(a, b, c) * turn(a, b, d) < 0. && turn(c, d, a) * turn(c, d, b) < 0.)
                    return Result<MeshBuild>::failure(invalid("Flat arrow inset intersects itself at edges " +
                                                              std::to_string(i) + ", " + std::to_string(j)));
            }
    }
    MeshBuild mesh;
    mesh.setActiveGroup("bevel");
    for (std::size_t r = 0; r + 1 < rings.size(); ++r)
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t j = (i + 1) % count;
            quad(mesh, rings[r][i], rings[r][j], rings[r + 1][j], rings[r + 1][i], 0., 1.);
        }
    mesh.setActiveGroup("face");
    std::vector<std::size_t> remaining;
    for (std::size_t i = 0; i < count; ++i) remaining.push_back(i);
    const auto& top    = rings.back();
    const auto& bottom = rings.front();
    while (remaining.size() > 2) {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i],
                       c = remaining[(i + 1) % remaining.size()];
            if (turn(top[a], top[b], top[c]) <= 1e-12) continue;
            bool occupied = false;
            for (auto k : remaining) {
                if (k == a || k == b || k == c) continue;
                if (turn(top[a], top[b], top[k]) >= -1e-12 && turn(top[b], top[c], top[k]) >= -1e-12 &&
                    turn(top[c], top[a], top[k]) >= -1e-12) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) continue;
            triangle(mesh, top[a], top[b], top[c], 0., 0.5, 1.);
            triangle(mesh, bottom[c], bottom[b], bottom[a], 1., 0.5, 0.);
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) return Result<MeshBuild>::failure(invalid("Flat arrow cap cannot be triangulated"));
    }
    return Result<MeshBuild>::success(std::move(mesh));
}
// Split the surface along the thickness transition before deformation, so cap
// triangles cannot stretch the head's extra height across the thin shaft.
struct HeadVertex {
    V      position;
    double u, v;
};
MeshBuild shapeFlatSurface(const MeshBuild& source, V base, V axis, double length, double centerZ, double scale,
                           const std::vector<V>& path, bool flowing, double shaftWidth, double headWidth) {
    MeshBuild           output;
    int                 active   = -2;
    const auto          distance = [&](V p) { return dot(sub(p, base), axis); };
    std::vector<double> arc{0.};
    for (std::size_t i = 1; i < path.size(); ++i)
        arc.push_back(arc.back() + std::sqrt(dot(sub(path[i], path[i - 1]), sub(path[i], path[i - 1]))));
    std::vector<std::pair<V, double>> cuts;
    if (flowing) {
        const V      chord       = sub(path.back(), path.front());
        const double chordLength = std::sqrt(dot(chord, chord));
        const V      direction   = mul(chord, 1. / chordLength);
        for (int i = 1; i < 16; ++i) cuts.emplace_back(direction, dot(path.front(), direction) + chordLength * i / 16.);
    }
    if (flowing) {
        // Sample the crowned head in both directions, not only along its perimeter.
        for (int i = 1; i < 13; ++i) cuts.emplace_back(axis, dot(base, axis) + length * i / 13.);
        const V transverse{-axis[1], axis[0], 0.};
        for (int i = 1; i < 10; ++i) cuts.emplace_back(transverse, dot(base, transverse) + headWidth * (i / 10. - 0.5));
    }
    if (scale != 1.)
        for (int i = 0; i <= 4; ++i) cuts.emplace_back(axis, dot(base, axis) + length * (-0.25 + 0.25 * i / 4.));
    for (int i = 0; i < source.getIndexCount(); i += 3) {
        const int group = source.getTriangleGroup(i / 3);
        if (group != active) {
            output.setActiveGroup(source.getGroupName(group));
            active = group;
        }
        std::vector<std::vector<HeadVertex>> polygons(1);
        for (int j = 0; j < 3; ++j) {
            const int index = source.getIndex(i + j);
            polygons[0].push_back({{source.getPositionX(index), source.getPositionY(index), source.getPositionZ(index)},
                                   source.getUvU(index),
                                   source.getUvV(index)});
        }
        for (const auto& [clipAxis, plane] : cuts) {
            const auto projection   = [&](V point) { return dot(point, clipAxis); };
            const auto intersection = [&](HeadVertex a, HeadVertex b) {
                if (b.position < a.position) std::swap(a, b);
                const double t = (plane - projection(a.position)) / (projection(b.position) - projection(a.position));
                V            point = add(a.position, mul(sub(b.position, a.position), t));
                for (auto& value : point) value = static_cast<float>(value);
                return HeadVertex{point, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
            };
            std::vector<std::vector<HeadVertex>> next;
            for (auto polygon : polygons) {
                const double tolerance = length * 1e-6;
                for (auto& vertex : polygon) {
                    const double delta = projection(vertex.position) - plane;
                    if (std::abs(delta) <= tolerance) {
                        vertex.position = sub(vertex.position, mul(clipAxis, delta));
                        for (auto& value : vertex.position) value = static_cast<float>(value);
                    }
                }
                double minimum = projection(polygon[0].position), maximum = minimum;
                for (const auto& vertex : polygon) {
                    minimum = std::min(minimum, projection(vertex.position));
                    maximum = std::max(maximum, projection(vertex.position));
                }
                if (minimum >= plane - tolerance || maximum <= plane + tolerance) {
                    next.push_back(polygon);
                    continue;
                }
                std::vector<HeadVertex> low, high;
                for (std::size_t k = 0; k < polygon.size(); ++k) {
                    const auto a = polygon[k], b = polygon[(k + 1) % polygon.size()];
                    double     da = projection(a.position) - plane, db = projection(b.position) - plane;
                    if (std::abs(da) <= tolerance) da = 0.;
                    if (std::abs(db) <= tolerance) db = 0.;
                    if (da <= 0.) low.push_back(a);
                    if (da >= 0.) high.push_back(a);
                    if ((da < 0. && db > 0.) || (da > 0. && db < 0.)) {
                        const auto cut = intersection(a, b);
                        low.push_back(cut);
                        high.push_back(cut);
                    }
                }
                if (low.size() >= 3) next.push_back(std::move(low));
                if (high.size() >= 3) next.push_back(std::move(high));
            }
            polygons = std::move(next);
        }
        for (auto& polygon : polygons) {
            for (auto& vertex : polygon) {
                const double t      = std::clamp((distance(vertex.position) / length + 0.25) * 4., 0., 1.);
                const double weight = t * t * (3. - 2. * t);
                double       taper = 1., crown = 1.;
                if (flowing) {
                    const auto sample          = samplePath(vertex.position, path, arc);
                    taper                      = flowingRatio(sample.progress);
                    const double headDistance  = distance(vertex.position);
                    const double headHalfWidth = headWidth * 0.5 * std::clamp(1. - headDistance / length, 0., 1.);
                    const double halfWidth     = (1. - weight) * shaftWidth * 0.5 * taper + weight * headHalfWidth;
                    const double lateral =
                        headDistance >= 0. ? std::abs(cross(axis, sub(vertex.position, base))[2]) : sample.lateral;
                    const double fraction = halfWidth > 1e-8 ? lateral / halfWidth : 1.;
                    crown                 = 1. + 0.35 * std::max(0., 1. - fraction * fraction);
                }
                vertex.position[2] =
                    centerZ + (vertex.position[2] - centerZ) * taper * crown * (1. + (scale - 1.) * weight);
                vertex.position[2] = static_cast<float>(vertex.position[2]);
            }
            for (std::size_t j = 1; j + 1 < polygon.size(); ++j) {
                const HeadVertex tri[]{polygon[0], polygon[j], polygon[j + 1]};
                const V          normalVector =
                    cross(sub(tri[1].position, tri[0].position), sub(tri[2].position, tri[0].position));
                if (dot(normalVector, normalVector) < 1e-32) continue;
                const V    n     = unit(normalVector);
                const auto first = static_cast<uint32_t>(output.getVertexCount());
                for (const auto& vertex : tri)
                    output.addVertex(static_cast<float>(vertex.position[0]), static_cast<float>(vertex.position[1]),
                                     static_cast<float>(vertex.position[2]), static_cast<float>(n[0]),
                                     static_cast<float>(n[1]), static_cast<float>(n[2]), static_cast<float>(vertex.u),
                                     static_cast<float>(vertex.v));
                output.addTriangle(first, first + 1, first + 2);
            }
        }
    }
    return output;
}
// Contract the front nose edge/face into one geometric apex at the target.
// Thickness and lateral width both taper; merely sharpening the XY silhouette
// would retain the old vertical edge at the end of an extruded arrow.
Result<MeshBuild> pointFlatTip(const MeshBuild& source, V base, V axis, V tip, double headLength) {
    const double start     = headLength * 0.55;
    const double tolerance = headLength * 1e-6;
    const auto   distance  = [&](V p) { return dot(sub(p, base), axis); };
    double       front     = 0.;
    for (int i = 0; i < source.getVertexCount(); ++i)
        front = std::max(front, distance({source.getPositionX(i), source.getPositionY(i), source.getPositionZ(i)}));
    if (front <= start + tolerance || front > headLength + tolerance)
        return Result<MeshBuild>::failure(invalid("Flat arrow has no valid nose interval before its target"));
    std::vector<V> positions;
    for (int i = 0; i < source.getVertexCount(); ++i) {
        V            p{source.getPositionX(i), source.getPositionY(i), source.getPositionZ(i)};
        const double d = distance(p);
        if (d >= front - tolerance)
            p = tip;
        else if (d > start) {
            const double t       = std::clamp((d - start) / (front - start), 0., 1.);
            const double forward = d + (headLength - front) * t * t * (3. - 2. * t);
            const V      lateral = sub(sub(p, base), mul(axis, d));
            p    = add(base, add(mul(axis, forward), mul(V{lateral[0], lateral[1], 0.}, std::sqrt(1. - t))));
            p[2] = tip[2] + (lateral[2]) * (1. - t * t);
        }
        for (auto& value : p) value = static_cast<float>(value);
        positions.push_back(p);
    }
    MeshBuild mesh;
    int       active = -2;
    for (int i = 0; i < source.getIndexCount(); i += 3) {
        const int indices[]{source.getIndex(i), source.getIndex(i + 1), source.getIndex(i + 2)};
        const V   a = positions[indices[0]], b = positions[indices[1]], c = positions[indices[2]];
        // These faces belonged to the contracted nose edge and carry no remaining area.
        if (a == b || b == c || c == a) continue;
        const V area = cross(sub(b, a), sub(c, a));
        if (length(area) < 1e-16)
            return Result<MeshBuild>::failure(invalid("Pointed arrow tip exceeds float mesh precision"));
        const V   normal = unit(area);
        const int group  = source.getTriangleGroup(i / 3);
        if (group != active) {
            mesh.setActiveGroup(source.getGroupName(group));
            active = group;
        }
        const auto first = static_cast<uint32_t>(mesh.getVertexCount());
        for (int index : indices) {
            const V p = positions[index];
            mesh.addVertex(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]),
                           static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2]),
                           source.getUvU(index), source.getUvV(index));
        }
        mesh.addTriangle(first, first + 1, first + 2);
    }
    return Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace

Result<void> ArrowGenerator::setEndpoints(float ax, float ay, float az, float bx, float by, float bz) {
    const V a{ax, ay, az}, b{bx, by, bz};
    if (!valid(a) || !valid(b) || length(sub(b, a)) < 1e-5) return Result<void>::failure(invalid());
    a_ = a;
    b_ = b;
    return Result<void>::success();
}
Result<void> ArrowGenerator::setBend(float x, float y, float z) {
    const V value{x, y, z};
    if (!valid(value)) return Result<void>::failure(invalid());
    bend_ = value;
    return Result<void>::success();
}
Result<void> ArrowGenerator::setMode(std::string_view mode) {
    if (mode != "2d" && mode != "3d" && mode != "flat") return Result<void>::failure(invalid());
    planar_ = mode != "3d";
    flat_   = mode == "flat";
    return Result<void>::success();
}
Result<void> ArrowGenerator::setSize(float shaftWidth, float headWidth, float headLength) {
    if (!valid({shaftWidth, headWidth, headLength}) || shaftWidth < 1e-5f || headWidth <= shaftWidth ||
        headLength < 1e-5f)
        return Result<void>::failure(invalid());
    shaftWidth_ = shaftWidth;
    headWidth_  = headWidth;
    headLength_ = headLength;
    return Result<void>::success();
}
Result<void> ArrowGenerator::setFlatProfile(std::string_view profile) {
    if (profile != "uniform" && profile != "flowing") return Result<void>::failure(invalid());
    flowing_ = profile == "flowing";
    return Result<void>::success();
}
Result<void> ArrowGenerator::setHeadThicknessScale(float scale) {
    if (!std::isfinite(scale) || scale < 1.f || scale > 4.f) return Result<void>::failure(invalid());
    headThicknessScale_ = scale;
    return Result<void>::success();
}
Result<void> ArrowGenerator::setBevel(float thickness, float radius, int segments) {
    if (!valid({thickness, radius, 0.}) || radius < 1e-5f || thickness <= 2. * radius || segments < 2 || segments > 12)
        return Result<void>::failure(invalid());
    thickness_     = thickness;
    bevelRadius_   = radius;
    bevelSegments_ = segments;
    return Result<void>::success();
}
Result<void> ArrowGenerator::setResolution(int segments, int sides) {
    if (segments < 2 || segments > 256 || sides < 3 || sides > 64) return Result<void>::failure(invalid());
    segments_ = segments;
    sides_    = sides;
    return Result<void>::success();
}
Result<MeshBuild> ArrowGenerator::buildMeshResult() const {
    const V      chord       = sub(b_, a_);
    const double chordLength = length(chord);
    // A strictly forward control polygon excludes cusps and repeated samples.
    if (std::abs(dot(bend_, unit(chord))) >= chordLength * 0.499 || (planar_ && (a_[2] != b_[2] || bend_[2] != 0.)))
        return Result<MeshBuild>::failure(invalid());
    const V             control = add(mul(add(a_, b_), 0.5), bend_);
    std::vector<V>      points{a_};
    std::vector<double> arc{0.};
    for (int i = 1; i <= segments_; ++i) {
        const double t = static_cast<double>(i) / segments_;
        const V      p = i == segments_
                             ? b_
                             : add(add(mul(a_, (1. - t) * (1. - t)), mul(control, 2. * t * (1. - t))), mul(b_, t * t));
        arc.push_back(arc.back() + length(sub(p, points.back())));
        points.push_back(p);
    }
    const double total = arc.back();
    if (total <= headLength_ + 1e-5 || headLength_ >= chordLength * 0.9) return Result<MeshBuild>::failure(invalid());
    // Trim by distance from B: the final cone has the requested axial length.
    std::size_t cut = points.size() - 2;
    while (cut > 0 && length(sub(points[cut], b_)) < headLength_) --cut;
    V lo = points[cut], hi = points[cut + 1];
    for (int i = 0; i < 48; ++i) {
        const V mid = mul(add(lo, hi), 0.5);
        if (length(sub(mid, b_)) > headLength_)
            lo = mid;
        else
            hi = mid;
    }
    const V base = mul(add(lo, hi), 0.5);
    points.resize(cut + 1);
    arc.resize(cut + 1);
    const double last = length(sub(base, points.back()));
    if (last > 1e-8) {
        points.push_back(base);
        arc.push_back(arc.back() + last);
    } else
        points.back() = base;
    const V headAxis = unit(sub(b_, base));
    if (flat_ && flowing_) {
        // Align the neck with the head while retaining the tail and distant curve.
        const double neckSpan = std::min(headLength_ * 0.5, length(sub(base, points.front())) * 0.5);
        for (auto& point : points) {
            const V      offset  = sub(point, base);
            const double t       = std::clamp(1. - length(offset) / neckSpan, 0., 1.);
            const double weight  = t * t * (3. - 2. * t);
            const V      aligned = add(base, mul(headAxis, dot(offset, headAxis)));
            point                = add(mul(point, 1. - weight), mul(aligned, weight));
        }
    }
    std::vector<V> tangents, normals;
    for (std::size_t i = 0; i < points.size(); ++i) {
        V t = i + 1 == points.size() ? headAxis : unit(sub(points[i + 1], points[i]));
        if (i > 0 && i + 1 < points.size()) t = unit(add(t, unit(sub(points[i], points[i - 1]))));
        tangents.push_back(t);
        if (planar_)
            normals.push_back({-t[1], t[0], 0.});
        else {
            const V reference = i == 0 ? (std::abs(t[2]) < 0.9 ? V{0., 0., 1.} : V{0., 1., 0.}) : normals.back();
            const V projected = sub(reference, mul(t, dot(reference, t)));
            if (length(projected) < 1e-8) return Result<MeshBuild>::failure(invalid());
            normals.push_back(unit(projected));
        }
    }
    MeshBuild  mesh;
    const int  sides     = planar_ ? 2 : sides_;
    const auto ringPoint = [&](std::size_t i, int j, double radius) {
        if (planar_) return add(points[i], mul(normals[i], j == 0 ? radius : -radius));
        const double angle = 2. * std::numbers::pi * j / sides;
        return add(
            points[i],
            mul(add(mul(normals[i], std::cos(angle)), mul(cross(tangents[i], normals[i]), std::sin(angle))), radius));
    };
    if (flat_) {
        auto result =
            flatArrow(points, normals, b_, shaftWidth_, headWidth_, thickness_, bevelRadius_, bevelSegments_, flowing_);
        if (!result.ok()) return Result<MeshBuild>::failure(result.status());
        mesh = std::move(result).takeValue();
        if (headThicknessScale_ != 1. || flowing_)
            mesh = shapeFlatSurface(mesh, base, headAxis, headLength_, a_[2], headThicknessScale_, points, flowing_,
                                    shaftWidth_, headWidth_);
    } else {
        mesh.setActiveGroup("shaft");
        for (std::size_t i = 0; i + 1 < points.size(); ++i)
            for (int j = 0; j < (planar_ ? 1 : sides); ++j) {
                const int k = (j + 1) % sides;
                quad(mesh, ringPoint(i, j, shaftWidth_ / 2.), ringPoint(i, k, shaftWidth_ / 2.),
                     ringPoint(i + 1, k, shaftWidth_ / 2.), ringPoint(i + 1, j, shaftWidth_ / 2.), arc[i] / total,
                     arc[i + 1] / total);
            }
        const std::size_t end = points.size() - 1;
        mesh.setActiveGroup("head");
        if (planar_)
            triangle(mesh, ringPoint(end, 1, headWidth_ / 2.), b_, ringPoint(end, 0, headWidth_ / 2.),
                     arc.back() / total, 1., arc.back() / total);
        else {
            for (int j = 0; j < sides; ++j) {
                const int k = (j + 1) % sides;
                triangle(mesh, ringPoint(end, j, headWidth_ / 2.), ringPoint(end, k, headWidth_ / 2.), b_,
                         arc.back() / total, arc.back() / total, 1.);
                quad(mesh, ringPoint(end, j, shaftWidth_ / 2.), ringPoint(end, k, shaftWidth_ / 2.),
                     ringPoint(end, k, headWidth_ / 2.), ringPoint(end, j, headWidth_ / 2.), arc.back() / total,
                     arc.back() / total);
                triangle(mesh, points[0], ringPoint(0, k, shaftWidth_ / 2.), ringPoint(0, j, shaftWidth_ / 2.), 0., 0.,
                         0.);
            }
        }
    }
    if (flat_) {
        auto pointed = pointFlatTip(mesh, base, headAxis, b_, headLength_);
        if (!pointed.ok()) return Result<MeshBuild>::failure(pointed.status());
        mesh = std::move(pointed).takeValue();
    }
    // Float mesh streams must still represent every generated face at the chosen world scale.
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        V p[3];
        for (int j = 0; j < 3; ++j) {
            const int index = mesh.getIndex(i + j);
            p[j]            = {mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
        }
        if (length(cross(sub(p[1], p[0]), sub(p[2], p[0]))) < 1e-16)
            return Result<MeshBuild>::failure(invalid("Mesh contains a degenerate float triangle"));
    }
    if (flat_ && flowing_) {
        // Angle-weighted normals are stable across uneven cap tessellation and preserve face UV seams.
        std::map<V, V> accumulated;
        for (int i = 0; i < mesh.getIndexCount(); i += 3) {
            V p[3];
            for (int j = 0; j < 3; ++j) {
                const int k = mesh.getIndex(i + j);
                p[j]        = {mesh.getPositionX(k), mesh.getPositionY(k), mesh.getPositionZ(k)};
            }
            const V normal = unit(cross(sub(p[1], p[0]), sub(p[2], p[0])));
            for (int j = 0; j < 3; ++j) {
                const V      a = unit(sub(p[(j + 1) % 3], p[j])), b = unit(sub(p[(j + 2) % 3], p[j]));
                const double angle = std::acos(std::clamp(dot(a, b), -1., 1.));
                accumulated[p[j]]  = add(accumulated[p[j]], mul(normal, angle));
            }
        }
        for (int i = 0; i < mesh.getVertexCount(); ++i) {
            const V point{mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i)};
            const V sum = accumulated.at(point);
            if (length(sum) < 1e-16)
                return Result<MeshBuild>::failure(invalid("Flowing arrow has an undefined normal"));
            const V average = unit(sum);
            const V face{mesh.getNormalX(i), mesh.getNormalY(i), mesh.getNormalZ(i)};
            // Preserve a crease when a shared average exceeds 75 degrees from this face.
            const V normal = dot(average, face) >= 0.2588190451 ? average : face;
            for (int j = 0; j < 3; ++j)
                mesh.normals()[static_cast<std::size_t>(i) * 3 + j] = static_cast<float>(normal[j]);
        }
    }
    return Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace eve::procgen
