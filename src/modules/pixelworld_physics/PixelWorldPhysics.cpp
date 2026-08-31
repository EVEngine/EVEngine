#include "pixelworld_physics/PixelWorldPhysics.h"

#include "common/Exception.h"
#include "physics/Body.h"
#include "physics/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace eve::pixelworld_physics {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "pixelworld_physics"));
}

std::vector<FragmentCollisionRect> decompose(const eve::pixelworld::PixelFragment& fragment) {
    const int width = fragment.width, height = fragment.height;
    std::vector<std::uint8_t> consumed(std::size_t(width) * std::size_t(height));
    std::vector<FragmentCollisionRect> result;
    const auto occupied = [&](int x, int y) {
        return fragment.cells[std::size_t(y) * std::size_t(width) + std::size_t(x)].material !=
               eve::pixelworld::MaterialId::Air;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::size_t start = std::size_t(y) * std::size_t(width) + std::size_t(x);
            if (consumed[start] || !occupied(x, y)) continue;
            int rectWidth = 1;
            while (x + rectWidth < width && !consumed[start + std::size_t(rectWidth)] &&
                   occupied(x + rectWidth, y))
                ++rectWidth;
            int rectHeight = 1;
            for (; y + rectHeight < height; ++rectHeight) {
                bool complete = true;
                for (int ox = 0; ox < rectWidth; ++ox) {
                    const std::size_t index = std::size_t(y + rectHeight) * std::size_t(width) +
                                              std::size_t(x + ox);
                    if (consumed[index] || !occupied(x + ox, y + rectHeight)) {
                        complete = false;
                        break;
                    }
                }
                if (!complete) break;
            }
            for (int oy = 0; oy < rectHeight; ++oy)
                for (int ox = 0; ox < rectWidth; ++ox)
                    consumed[std::size_t(y + oy) * std::size_t(width) + std::size_t(x + ox)] = 1;
            result.push_back({x, y, rectWidth, rectHeight});
        }
    return result;
}

eve::pixelworld::PixelFragment rotated(const eve::pixelworld::PixelFragment& source, int turns) {
    turns = ((turns % 4) + 4) % 4;
    eve::pixelworld::PixelFragment result = source;
    if (turns == 0) return result;
    result.width = (turns % 2 == 0) ? source.width : source.height;
    result.height = (turns % 2 == 0) ? source.height : source.width;
    result.cells.assign(std::size_t(result.width) * std::size_t(result.height), {});
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x) {
            int rx = 0, ry = 0;
            if (turns == 1) {
                rx = source.height - 1 - y;
                ry = x;
            } else if (turns == 2) {
                rx = source.width - 1 - x;
                ry = source.height - 1 - y;
            } else {
                rx = y;
                ry = source.width - 1 - x;
            }
            result.cells[std::size_t(ry) * std::size_t(result.width) + std::size_t(rx)] =
                source.cells[std::size_t(y) * std::size_t(source.width) + std::size_t(x)];
        }
    return result;
}

struct GridPoint {
    int x = 0;
    int y = 0;
    auto operator<=>(const GridPoint&) const = default;
};

struct GridEdge {
    GridPoint from;
    GridPoint to;
};

int direction(const GridEdge& edge) {
    if (edge.to.x > edge.from.x) return 0;
    if (edge.to.y > edge.from.y) return 1;
    if (edge.to.x < edge.from.x) return 2;
    return 3;
}

int turnRank(int previous, int next) {
    const int turn = (next - previous + 4) % 4;
    if (turn == 1) return 0;
    if (turn == 0) return 1;
    if (turn == 3) return 2;
    return 3;
}

bool collinear(GridPoint a, GridPoint b, GridPoint c) {
    return (b.x - a.x) * (c.y - b.y) == (b.y - a.y) * (c.x - b.x);
}

bool validCellBounds(float minimum, float maximum) {
    return std::isfinite(minimum) && std::isfinite(maximum) &&
           double(minimum) >= double(std::numeric_limits<int>::min()) + 2.0 &&
           double(maximum) <= double(std::numeric_limits<int>::max()) - 2.0;
}

}  // namespace

eve::Result<std::vector<TerrainCollisionContour>> extractTerrainContours(
    const eve::pixelworld::PixelWorld& pixelWorld, int chunkX, int chunkY,
    std::uint32_t maximumVertices) {
    if (maximumVertices < 2)
        return failure<std::vector<TerrainCollisionContour>>(
            eve::DiagnosticCode::InvalidArgument, "maximumVertices must be at least two",
            "maximumVertices");
    constexpr int size = eve::pixelworld::kPixelChunkSize;
    const std::int64_t wideOriginX = std::int64_t(chunkX) * size;
    const std::int64_t wideOriginY = std::int64_t(chunkY) * size;
    if (wideOriginX < std::numeric_limits<int>::min() ||
        wideOriginX + size > std::numeric_limits<int>::max() ||
        wideOriginY < std::numeric_limits<int>::min() ||
        wideOriginY + size > std::numeric_limits<int>::max())
        return failure<std::vector<TerrainCollisionContour>>(
            eve::DiagnosticCode::InvalidArgument, "Chunk coordinates exceed world coordinate range",
            "chunk");
    const int originX = int(wideOriginX), originY = int(wideOriginY);
    const auto solid = [&](int localX, int localY) {
        return pixelWorld.isSolidMaterial(
            pixelWorld.getCell(originX + localX, originY + localY).material);
    };
    std::vector<GridEdge> edges;
    edges.reserve(size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            if (!solid(x, y)) continue;
            if (!solid(x, y - 1)) edges.push_back({{x, y}, {x + 1, y}});
            if (!solid(x + 1, y)) edges.push_back({{x + 1, y}, {x + 1, y + 1}});
            if (!solid(x, y + 1)) edges.push_back({{x + 1, y + 1}, {x, y + 1}});
            if (!solid(x - 1, y)) edges.push_back({{x, y + 1}, {x, y}});
        }
    std::map<GridPoint, std::vector<std::size_t>> outgoing;
    for (std::size_t index = 0; index < edges.size(); ++index)
        outgoing[edges[index].from].push_back(index);
    std::vector<std::uint8_t> used(edges.size());
    std::vector<TerrainCollisionContour> contours;
    std::uint64_t vertexCount = 0;
    for (std::size_t first = 0; first < edges.size(); ++first) {
        if (used[first]) continue;
        std::vector<GridPoint> points{edges[first].from};
        std::size_t current = first;
        bool loop = false;
        while (true) {
            used[current] = 1;
            const GridPoint end = edges[current].to;
            if (end == points.front()) {
                loop = true;
                break;
            }
            points.push_back(end);
            const auto found = outgoing.find(end);
            if (found == outgoing.end()) break;
            std::size_t next = edges.size();
            int bestRank = 5;
            for (const std::size_t candidate : found->second) {
                if (used[candidate]) continue;
                const int rank = turnRank(direction(edges[current]), direction(edges[candidate]));
                if (rank < bestRank || (rank == bestRank && candidate < next)) {
                    bestRank = rank;
                    next = candidate;
                }
            }
            if (next == edges.size()) break;
            current = next;
        }
        bool changed = true;
        while (changed && points.size() > (loop ? 3U : 2U)) {
            changed = false;
            for (std::size_t index = 0; index < points.size(); ++index) {
                if (!loop && (index == 0 || index + 1 == points.size())) continue;
                const std::size_t before = (index + points.size() - 1) % points.size();
                const std::size_t after = (index + 1) % points.size();
                if (collinear(points[before], points[index], points[after])) {
                    points.erase(points.begin() + std::ptrdiff_t(index));
                    changed = true;
                    break;
                }
            }
        }
        if (points.size() < (loop ? 3U : 2U)) continue;
        vertexCount += points.size();
        if (vertexCount > maximumVertices)
            return failure<std::vector<TerrainCollisionContour>>(
                eve::DiagnosticCode::PreconditionViolation,
                "terrain contour exceeds maximumVertices", "maximumVertices");
        TerrainCollisionContour contour;
        contour.loop = loop;
        contour.vertices.reserve(points.size() * 2);
        for (const GridPoint point : points) {
            contour.vertices.push_back(float(point.x));
            contour.vertices.push_back(float(point.y));
        }
        contours.push_back(std::move(contour));
    }
    return eve::Result<std::vector<TerrainCollisionContour>>::success(std::move(contours));
}

eve::Result<PixelTerrainContact> probeTerrainCircle(
    const eve::pixelworld::PixelWorld& pixelWorld, float centerX, float centerY, float radius,
    std::uint32_t maximumCells) {
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(radius) ||
        radius <= 0.f || maximumCells == 0)
        return failure<PixelTerrainContact>(eve::DiagnosticCode::InvalidArgument,
                                            "circle probe parameters are invalid", "probe");
    if (!validCellBounds(centerX - radius, centerX + radius) ||
        !validCellBounds(centerY - radius, centerY + radius))
        return failure<PixelTerrainContact>(eve::DiagnosticCode::InvalidArgument,
                                            "circle probe exceeds world coordinate range", "probe");
    const int minX = int(std::floor(centerX - radius));
    const int maxX = int(std::floor(centerX + radius));
    const int minY = int(std::floor(centerY - radius));
    const int maxY = int(std::floor(centerY + radius));
    const std::uint64_t width = std::uint64_t(std::int64_t(maxX) - minX + 1);
    const std::uint64_t height = std::uint64_t(std::int64_t(maxY) - minY + 1);
    if (width > maximumCells || height > maximumCells || width * height > maximumCells)
        return failure<PixelTerrainContact>(eve::DiagnosticCode::PreconditionViolation,
                                            "circle probe exceeds maximumCells", "maximumCells");
    PixelTerrainContact best;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const auto material = pixelWorld.getCell(x, y).material;
            if (!pixelWorld.isSolidMaterial(material)) continue;
            const float closestX = std::clamp(centerX, float(x), float(x + 1));
            const float closestY = std::clamp(centerY, float(y), float(y + 1));
            const float dx = centerX - closestX, dy = centerY - closestY;
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared >= radius * radius) continue;
            const float distance = std::sqrt(distanceSquared);
            const float depth = radius - distance;
            if (best.hit && depth < best.depth) continue;
            if (best.hit && depth == best.depth && std::pair(y, x) >= std::pair(best.cellY, best.cellX))
                continue;
            best.hit = true;
            best.material = material;
            best.cellX = x;
            best.cellY = y;
            best.pointX = closestX;
            best.pointY = closestY;
            best.depth = depth;
            if (distance > 0.f) {
                best.normalX = dx / distance;
                best.normalY = dy / distance;
            } else {
                const float left = centerX - float(x), right = float(x + 1) - centerX;
                const float top = centerY - float(y), bottom = float(y + 1) - centerY;
                const float nearest = std::min({left, right, top, bottom});
                if (nearest == left) best.normalX = -1.f;
                else if (nearest == right) best.normalX = 1.f;
                else if (nearest == top) best.normalY = -1.f;
                else best.normalY = 1.f;
            }
        }
    return eve::Result<PixelTerrainContact>::success(best);
}

eve::Result<PixelTerrainContact> sweepTerrainCircle(
    const eve::pixelworld::PixelWorld& pixelWorld, float startX, float startY, float endX,
    float endY, float radius, std::uint32_t maximumCells) {
    if (!std::isfinite(startX) || !std::isfinite(startY) || !std::isfinite(endX) ||
        !std::isfinite(endY) || !std::isfinite(radius) || radius <= 0.f || maximumCells == 0)
        return failure<PixelTerrainContact>(eve::DiagnosticCode::InvalidArgument,
                                            "circle sweep parameters are invalid", "sweep");
    if (!validCellBounds(std::min(startX, endX) - radius,
                         std::max(startX, endX) + radius) ||
        !validCellBounds(std::min(startY, endY) - radius,
                         std::max(startY, endY) + radius))
        return failure<PixelTerrainContact>(eve::DiagnosticCode::InvalidArgument,
                                            "circle sweep exceeds world coordinate range", "sweep");
    auto initial = probeTerrainCircle(pixelWorld, startX, startY, radius, maximumCells);
    if (!initial.ok()) return initial;
    if (initial.value().hit) return initial;
    const int minX = int(std::floor(std::min(startX, endX) - radius));
    const int maxX = int(std::floor(std::max(startX, endX) + radius));
    const int minY = int(std::floor(std::min(startY, endY) - radius));
    const int maxY = int(std::floor(std::max(startY, endY) + radius));
    const std::uint64_t width = std::uint64_t(std::int64_t(maxX) - minX + 1);
    const std::uint64_t height = std::uint64_t(std::int64_t(maxY) - minY + 1);
    if (width > maximumCells || height > maximumCells || width * height > maximumCells)
        return failure<PixelTerrainContact>(eve::DiagnosticCode::PreconditionViolation,
                                            "circle sweep exceeds maximumCells", "maximumCells");
    const float deltaX = endX - startX, deltaY = endY - startY;
    PixelTerrainContact best;
    best.fraction = 1.f;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const auto material = pixelWorld.getCell(x, y).material;
            if (!pixelWorld.isSolidMaterial(material)) continue;
            float cellFraction = 2.f, cellNormalX = 0.f, cellNormalY = 0.f;
            const auto consider = [&](float fraction, float nx, float ny) {
                if (fraction >= 0.f && fraction <= 1.f && fraction < cellFraction) {
                    cellFraction = fraction;
                    cellNormalX = nx;
                    cellNormalY = ny;
                }
            };
            if (deltaX > 0.f) {
                const float fraction = (float(x) - radius - startX) / deltaX;
                const float centerY = startY + deltaY * fraction;
                if (centerY >= float(y) && centerY <= float(y + 1))
                    consider(fraction, -1.f, 0.f);
            } else if (deltaX < 0.f) {
                const float fraction = (float(x + 1) + radius - startX) / deltaX;
                const float centerY = startY + deltaY * fraction;
                if (centerY >= float(y) && centerY <= float(y + 1))
                    consider(fraction, 1.f, 0.f);
            }
            if (deltaY > 0.f) {
                const float fraction = (float(y) - radius - startY) / deltaY;
                const float centerX = startX + deltaX * fraction;
                if (centerX >= float(x) && centerX <= float(x + 1))
                    consider(fraction, 0.f, -1.f);
            } else if (deltaY < 0.f) {
                const float fraction = (float(y + 1) + radius - startY) / deltaY;
                const float centerX = startX + deltaX * fraction;
                if (centerX >= float(x) && centerX <= float(x + 1))
                    consider(fraction, 0.f, 1.f);
            }
            const float quadraticA = deltaX * deltaX + deltaY * deltaY;
            if (quadraticA > 0.f) {
                struct Corner {
                    float x;
                    float y;
                    int quadrantX;
                    int quadrantY;
                };
                const Corner corners[] = {
                    {float(x), float(y), -1, -1}, {float(x + 1), float(y), 1, -1},
                    {float(x + 1), float(y + 1), 1, 1}, {float(x), float(y + 1), -1, 1}};
                for (const Corner corner : corners) {
                    const float offsetX = startX - corner.x, offsetY = startY - corner.y;
                    const float quadraticB = 2.f * (offsetX * deltaX + offsetY * deltaY);
                    const float quadraticC = offsetX * offsetX + offsetY * offsetY - radius * radius;
                    const float discriminant = quadraticB * quadraticB - 4.f * quadraticA * quadraticC;
                    if (discriminant < 0.f) continue;
                    const float fraction =
                        (-quadraticB - std::sqrt(discriminant)) / (2.f * quadraticA);
                    if (fraction < 0.f || fraction > 1.f) continue;
                    const float hitX = startX + deltaX * fraction;
                    const float hitY = startY + deltaY * fraction;
                    if ((corner.quadrantX < 0 && hitX > corner.x) ||
                        (corner.quadrantX > 0 && hitX < corner.x) ||
                        (corner.quadrantY < 0 && hitY > corner.y) ||
                        (corner.quadrantY > 0 && hitY < corner.y))
                        continue;
                    consider(fraction, (hitX - corner.x) / radius,
                             (hitY - corner.y) / radius);
                }
            }
            if (cellFraction > 1.f) continue;
            if (best.hit && cellFraction > best.fraction) continue;
            if (best.hit && cellFraction == best.fraction &&
                std::pair(y, x) >= std::pair(best.cellY, best.cellX))
                continue;
            best.hit = true;
            best.material = material;
            best.cellX = x;
            best.cellY = y;
            best.fraction = cellFraction;
            best.pointX = startX + deltaX * cellFraction;
            best.pointY = startY + deltaY * cellFraction;
            best.normalX = cellNormalX;
            best.normalY = cellNormalY;
        }
    if (!best.hit) best.fraction = 1.f;
    return eve::Result<PixelTerrainContact>::success(best);
}

PixelFragmentBody::PixelFragmentBody(eve::pixelworld::PixelFragment fragment,
                                     eve::physics::PhysicsLink link,
                                     std::vector<FragmentCollisionRect> rectangles)
    : fragment_(std::move(fragment)), link_(link), rectangles_(std::move(rectangles)) {}

eve::Result<std::unique_ptr<PixelFragmentBody>> PixelFragmentBody::create(
    eve::physics::World& world, eve::pixelworld::PixelFragment fragment, FragmentBodyConfig config) {
    if (fragment.width <= 0 || fragment.height <= 0 || fragment.id == 0 ||
        std::uint64_t(fragment.width) * std::uint64_t(fragment.height) != fragment.cells.size())
        return failure<std::unique_ptr<PixelFragmentBody>>(
            eve::DiagnosticCode::InvalidArgument, "fragment bitmap metadata is invalid", "fragment");
    if (!std::isfinite(config.density) || config.density <= 0.f ||
        !std::isfinite(config.friction) || config.friction < 0.f ||
        !std::isfinite(config.restitution) || config.restitution < 0.f || config.maximumFixtures == 0)
        return failure<std::unique_ptr<PixelFragmentBody>>(
            eve::DiagnosticCode::InvalidArgument, "fragment body config is invalid", "config");
    auto rectangles = decompose(fragment);
    if (rectangles.empty() || rectangles.size() > config.maximumFixtures)
        return failure<std::unique_ptr<PixelFragmentBody>>(
            eve::DiagnosticCode::PreconditionViolation,
            "fragment collision decomposition is empty or exceeds maximumFixtures", "maximumFixtures");

    eve::physics::Body* body = nullptr;
    try {
        const float centerX = float(fragment.originX) + float(fragment.width) * 0.5f;
        const float centerY = float(fragment.originY) + float(fragment.height) * 0.5f;
        body = world.newBody("dynamic", centerX, centerY);
        for (const FragmentCollisionRect& rect : rectangles) {
            const float offsetX = float(rect.x) + float(rect.width) * 0.5f - float(fragment.width) * 0.5f;
            const float offsetY = float(rect.y) + float(rect.height) * 0.5f - float(fragment.height) * 0.5f;
            body->newRectangleFixtureAt(float(rect.width), float(rect.height), offsetX, offsetY,
                                        config.density, config.friction, config.restitution);
        }
        auto link = eve::physics::PhysicsLink::fromBody(*body);
        if (!link.ok()) {
            body->destroy();
            return eve::Result<std::unique_ptr<PixelFragmentBody>>::failure(link.status());
        }
        return eve::Result<std::unique_ptr<PixelFragmentBody>>::success(
            std::unique_ptr<PixelFragmentBody>(new PixelFragmentBody(
                std::move(fragment), std::move(link).takeValue(), std::move(rectangles))));
    } catch (const std::exception& error) {
        if (body && body->isValid()) body->destroy();
        return failure<std::unique_ptr<PixelFragmentBody>>(
            eve::DiagnosticCode::Failed, std::string("failed to create fragment fixtures: ") + error.what(),
            "physics");
    }
}

const std::vector<FragmentCollisionRect>& PixelFragmentBody::collisionRects() const noexcept {
    return rectangles_;
}

std::uint64_t PixelFragmentBody::fragmentId() const noexcept { return fragment_.id; }
eve::physics::PhysicsLink PixelFragmentBody::physicsLink() const noexcept { return link_; }
bool PixelFragmentBody::isRasterized() const noexcept { return rasterized_; }

eve::Result<FragmentSettleReceipt> PixelFragmentBody::settleIfSleeping(
    eve::physics::World& physicsWorld, eve::pixelworld::PixelWorld& pixelWorld) {
    if (rasterized_ || physicsReleased_)
        return failure<FragmentSettleReceipt>(eve::DiagnosticCode::PreconditionViolation,
                                              "fragment body is no longer active", "state");
    auto resolved = link_.resolve(physicsWorld);
    if (!resolved.ok()) return eve::Result<FragmentSettleReceipt>::failure(resolved.status());
    eve::physics::Body* body = resolved.value();
    if (body->isAwake())
        return eve::Result<FragmentSettleReceipt>::success(
            {FragmentSettleDisposition::StillAwake, fragment_.id, 0, 0});

    constexpr double halfPi = 1.57079632679489661923;
    const int turns = int(std::llround(double(body->getAngle()) / halfPi));
    auto candidate = rotated(fragment_, turns);
    const int originX = int(std::lround(double(body->getX()) - double(candidate.width) * 0.5));
    const int originY = int(std::lround(double(body->getY()) - double(candidate.height) * 0.5));
    auto rasterized = pixelWorld.rasterizeFragment(candidate, originX, originY);
    if (!rasterized.ok()) return eve::Result<FragmentSettleReceipt>::failure(rasterized.status());
    body->destroy();
    physicsReleased_ = true;
    rasterized_ = true;
    return eve::Result<FragmentSettleReceipt>::success(
        {FragmentSettleDisposition::Rasterized, fragment_.id,
         rasterized.value().cellsPlaced, ((turns % 4) + 4) % 4});
}

eve::Result<void> PixelFragmentBody::releasePhysics(eve::physics::World& physicsWorld) {
    if (physicsReleased_) return eve::Result<void>::success();
    auto resolved = link_.resolve(physicsWorld);
    if (!resolved.ok()) return eve::Result<void>::failure(resolved.status());
    resolved.value()->destroy();
    physicsReleased_ = true;
    return eve::Result<void>::success();
}

struct PixelTerrainCollisionCache::Impl {
    using Coord = std::pair<int, int>;
    std::map<Coord, eve::physics::PhysicsLink> bodies;
    eve::pixelworld::PixelWorldLink pixelWorld;
    eve::physics::PhysicsWorldHandle physicsWorld = eve::physics::PhysicsWorldHandle::invalid();
    std::uint64_t revision = 0;
};

PixelTerrainCollisionCache::PixelTerrainCollisionCache() : impl_(std::make_unique<Impl>()) {}
PixelTerrainCollisionCache::~PixelTerrainCollisionCache() = default;
PixelTerrainCollisionCache::PixelTerrainCollisionCache(PixelTerrainCollisionCache&&) noexcept = default;
PixelTerrainCollisionCache& PixelTerrainCollisionCache::operator=(PixelTerrainCollisionCache&&) noexcept = default;

eve::Result<TerrainCollisionSyncReceipt> PixelTerrainCollisionCache::sync(
    eve::physics::World& physicsWorld, const eve::pixelworld::PixelWorld& pixelWorld,
    std::uint32_t maximumFixturesPerChunk) {
    if (maximumFixturesPerChunk == 0)
        return failure<TerrainCollisionSyncReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                    "maximumFixturesPerChunk must be positive",
                                                    "maximumFixturesPerChunk");
    const bool samePixelWorld = impl_->pixelWorld == pixelWorld.worldLink();
    const bool samePhysicsWorld = impl_->physicsWorld == physicsWorld.runtimeHandle();
    const std::uint64_t since = (samePixelWorld && samePhysicsWorld) ? impl_->revision : 0;
    const auto changed = pixelWorld.snapshotChangedChunks(since);
    std::set<Impl::Coord> rebuildCoords;
    for (const auto& chunk : changed) {
        const Impl::Coord coord{chunk.x, chunk.y};
        rebuildCoords.insert(coord);
        constexpr Impl::Coord neighbors[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto [dx, dy] : neighbors) {
            const Impl::Coord neighbor{chunk.x + dx, chunk.y + dy};
            if (impl_->bodies.contains(neighbor)) rebuildCoords.insert(neighbor);
        }
    }

    struct Candidate {
        Impl::Coord coord;
        eve::physics::PhysicsLink link;
        std::uint32_t fixtures = 0;
    };
    std::vector<Candidate> staged;
    staged.reserve(rebuildCoords.size());
    const auto destroyStaged = [&] {
        for (const Candidate& candidate : staged) {
            auto resolved = candidate.link.resolve(physicsWorld);
            if (resolved.ok()) resolved.value()->destroy();
        }
    };

    for (const auto& coord : rebuildCoords) {
        auto extracted = extractTerrainContours(pixelWorld, coord.first, coord.second);
        if (!extracted.ok()) {
            destroyStaged();
            return eve::Result<TerrainCollisionSyncReceipt>::failure(extracted.status());
        }
        auto contours = std::move(extracted).takeValue();
        if (contours.size() > maximumFixturesPerChunk) {
            destroyStaged();
            return failure<TerrainCollisionSyncReceipt>(
                eve::DiagnosticCode::PreconditionViolation,
                "terrain Chunk contours exceed maximumFixturesPerChunk", "chunk");
        }
        if (contours.empty()) {
            staged.push_back({coord, {}, 0});
            continue;
        }
        eve::physics::Body* body = nullptr;
        try {
            body = physicsWorld.newBody(
                "static", float(coord.first) * float(eve::pixelworld::kPixelChunkSize),
                float(coord.second) * float(eve::pixelworld::kPixelChunkSize));
            for (const TerrainCollisionContour& contour : contours)
                body->newChainFixture(contour.vertices, contour.loop, 0.5f, 0.f);
            auto link = eve::physics::PhysicsLink::fromBody(*body);
            if (!link.ok()) {
                body->destroy();
                destroyStaged();
                return eve::Result<TerrainCollisionSyncReceipt>::failure(link.status());
            }
            staged.push_back({coord, std::move(link).takeValue(),
                              std::uint32_t(contours.size())});
        } catch (const std::exception& error) {
            if (body && body->isValid()) body->destroy();
            destroyStaged();
            return failure<TerrainCollisionSyncReceipt>(
                eve::DiagnosticCode::Failed,
                std::string("failed to stage terrain collision body: ") + error.what(), "physics");
        }
    }

    TerrainCollisionSyncReceipt receipt;
    receipt.sourceRevision = pixelWorld.revision();
    receipt.chunksRebuilt = std::uint32_t(staged.size());
    if (!samePhysicsWorld) {
        impl_->bodies.clear();
    } else if (!samePixelWorld) {
        for (const auto& [coord, link] : impl_->bodies) {
            (void)coord;
            auto resolved = link.resolve(physicsWorld);
            if (resolved.ok()) resolved.value()->destroy();
            ++receipt.bodiesRemoved;
        }
        impl_->bodies.clear();
    }
    for (const Candidate& candidate : staged) {
        const auto old = impl_->bodies.find(candidate.coord);
        if (old != impl_->bodies.end()) {
            auto resolved = old->second.resolve(physicsWorld);
            if (resolved.ok()) resolved.value()->destroy();
            impl_->bodies.erase(old);
            ++receipt.bodiesRemoved;
        }
        if (candidate.fixtures != 0) {
            impl_->bodies.emplace(candidate.coord, candidate.link);
            receipt.fixturesCreated += candidate.fixtures;
        }
    }
    impl_->pixelWorld = pixelWorld.worldLink();
    impl_->physicsWorld = physicsWorld.runtimeHandle();
    impl_->revision = pixelWorld.revision();
    return eve::Result<TerrainCollisionSyncReceipt>::success(receipt);
}

eve::Result<void> PixelTerrainCollisionCache::clearPhysics(eve::physics::World& physicsWorld) {
    if (impl_->physicsWorld.isValid() && impl_->physicsWorld != physicsWorld.runtimeHandle())
        return failure<void>(eve::DiagnosticCode::StaleHandle,
                             "terrain collision cache belongs to another physics world", "physicsWorld");
    for (const auto& [coord, link] : impl_->bodies) {
        (void)coord;
        auto resolved = link.resolve(physicsWorld);
        if (resolved.ok()) resolved.value()->destroy();
    }
    impl_->bodies.clear();
    impl_->pixelWorld = {};
    impl_->physicsWorld = eve::physics::PhysicsWorldHandle::invalid();
    impl_->revision = 0;
    return eve::Result<void>::success();
}

std::uint64_t PixelTerrainCollisionCache::sourceRevision() const noexcept { return impl_->revision; }
std::size_t PixelTerrainCollisionCache::bodyCount() const noexcept { return impl_->bodies.size(); }

}  // namespace eve::pixelworld_physics
