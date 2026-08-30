#include "physics/DistanceField3D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::physics {

namespace {
constexpr double maxQueryIntervals = 1000000.0;

int queryIntervals(double length, double interval, const char *operation) {
    const double count = std::ceil(length / interval);
    if (!std::isfinite(count) || count > maxQueryIntervals)
        throw eve::Exception("%s: sweep or capsule is too long", operation);
    return std::max(1, static_cast<int>(count));
}
}  // namespace

DistanceField3D::DistanceField3D(int width, int height, int depth, float cellSize,
                                 float originX, float originY, float originZ,
                                 float outsideDistance)
    : width_(width), height_(height), depth_(depth), cellSize_(cellSize), originX_(originX),
      originY_(originY), originZ_(originZ), outsideDistance_(outsideDistance) {
    if (width < 2 || height < 2 || depth < 2 || !(cellSize > 0.f) || !std::isfinite(cellSize) ||
        !std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ) ||
        !std::isfinite(outsideDistance))
        throw eve::Exception("DistanceField3D: dimensions must be >= 2 and cellSize finite > 0");
    constexpr size_t maxSamples = 64u * 1024u * 1024u;
    const size_t wh = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (static_cast<size_t>(width) > maxSamples / static_cast<size_t>(height) ||
        wh > maxSamples / static_cast<size_t>(depth))
        throw eve::Exception("DistanceField3D: grid is too large");
    const size_t count = wh * static_cast<size_t>(depth);
    distances_.assign(count, outsideDistance);
    moverSkinWidth_ = std::max(1e-4f, cellSize_ * 1e-3f);
}

bool DistanceField3D::validIndex(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < width_ && y < height_ && z < depth_;
}

bool DistanceField3D::validRegion(int x, int y, int z, int width, int height, int depth) const {
    return x >= 0 && y >= 0 && z >= 0 && width > 0 && height > 0 && depth > 0 &&
           width <= width_ - x && height <= height_ - y && depth <= depth_ - z;
}

size_t DistanceField3D::index(int x, int y, int z) const {
    return (static_cast<size_t>(z) * static_cast<size_t>(height_) + static_cast<size_t>(y)) *
               static_cast<size_t>(width_) +
           static_cast<size_t>(x);
}

void DistanceField3D::setDistances(const std::vector<float> &distances) {
    if (distances.size() != distances_.size())
        throw eve::Exception("DistanceField3D.setDistances: sample count does not match grid");
    if (!std::all_of(distances.begin(), distances.end(),
                     [](float distance) { return std::isfinite(distance); }))
        throw eve::Exception("DistanceField3D.setDistances: all distances must be finite");
    distances_ = distances;
    ++revision_;
}

void DistanceField3D::fill(float distance) {
    if (!std::isfinite(distance))
        throw eve::Exception("DistanceField3D.fill: distance must be finite");
    std::fill(distances_.begin(), distances_.end(), distance);
    ++revision_;
}

void DistanceField3D::setDistanceRegion(int x, int y, int z, int width, int height, int depth,
                                        const std::vector<float> &distances) {
    if (!validRegion(x, y, z, width, height, depth))
        throw eve::Exception("DistanceField3D.setDistanceRegion: region is out of range");
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) *
                         static_cast<size_t>(depth);
    if (distances.size() != count)
        throw eve::Exception(
            "DistanceField3D.setDistanceRegion: sample count does not match region");
    if (!std::all_of(distances.begin(), distances.end(),
                     [](float distance) { return std::isfinite(distance); }))
        throw eve::Exception(
            "DistanceField3D.setDistanceRegion: all distances must be finite");
    size_t source = 0;
    for (int localZ = 0; localZ < depth; ++localZ) {
        for (int localY = 0; localY < height; ++localY) {
            const size_t destination = index(x, y + localY, z + localZ);
            std::copy_n(distances.begin() + static_cast<std::ptrdiff_t>(source), width,
                        distances_.begin() + static_cast<std::ptrdiff_t>(destination));
            source += static_cast<size_t>(width);
        }
    }
    ++revision_;
}

void DistanceField3D::fillRegion(int x, int y, int z, int width, int height, int depth,
                                 float distance) {
    if (!validRegion(x, y, z, width, height, depth))
        throw eve::Exception("DistanceField3D.fillRegion: region is out of range");
    if (!std::isfinite(distance))
        throw eve::Exception("DistanceField3D.fillRegion: distance must be finite");
    for (int localZ = 0; localZ < depth; ++localZ) {
        for (int localY = 0; localY < height; ++localY) {
            const size_t destination = index(x, y + localY, z + localZ);
            std::fill_n(distances_.begin() + static_cast<std::ptrdiff_t>(destination), width,
                        distance);
        }
    }
    ++revision_;
}

void DistanceField3D::setDistance(int x, int y, int z, float distance) {
    if (!validIndex(x, y, z)) throw eve::Exception("DistanceField3D.setDistance: index out of range");
    if (!std::isfinite(distance))
        throw eve::Exception("DistanceField3D.setDistance: distance must be finite");
    distances_[index(x, y, z)] = distance;
    ++revision_;
}

float DistanceField3D::getDistance(int x, int y, int z) const {
    return validIndex(x, y, z) ? distances_[index(x, y, z)] : outsideDistance_;
}

float DistanceField3D::sample(float x, float y, float z) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("DistanceField3D.sample: coordinates must be finite");
    const float gx = (x - originX_) / cellSize_;
    const float gy = (y - originY_) / cellSize_;
    const float gz = (z - originZ_) / cellSize_;
    if (gx < 0.f || gy < 0.f || gz < 0.f || gx > width_ - 1.f || gy > height_ - 1.f ||
        gz > depth_ - 1.f)
        return outsideDistance_;

    const int x0 = std::min(static_cast<int>(std::floor(gx)), width_ - 2);
    const int y0 = std::min(static_cast<int>(std::floor(gy)), height_ - 2);
    const int z0 = std::min(static_cast<int>(std::floor(gz)), depth_ - 2);
    const float tx = std::clamp(gx - x0, 0.f, 1.f);
    const float ty = std::clamp(gy - y0, 0.f, 1.f);
    const float tz = std::clamp(gz - z0, 0.f, 1.f);
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = mix(getDistance(x0, y0, z0), getDistance(x0 + 1, y0, z0), tx);
    const float c10 = mix(getDistance(x0, y0 + 1, z0), getDistance(x0 + 1, y0 + 1, z0), tx);
    const float c01 = mix(getDistance(x0, y0, z0 + 1), getDistance(x0 + 1, y0, z0 + 1), tx);
    const float c11 = mix(getDistance(x0, y0 + 1, z0 + 1),
                          getDistance(x0 + 1, y0 + 1, z0 + 1), tx);
    return mix(mix(c00, c10, ty), mix(c01, c11, ty), tz);
}

void DistanceField3D::sampleNormal(float x, float y, float z) {
    const float h = cellSize_ * 0.5f;
    const float maxX = originX_ + cellSize_ * static_cast<float>(width_ - 1);
    const float maxY = originY_ + cellSize_ * static_cast<float>(height_ - 1);
    const float maxZ = originZ_ + cellSize_ * static_cast<float>(depth_ - 1);
    if (x < originX_ || y < originY_ || z < originZ_ || x > maxX || y > maxY || z > maxZ) {
        normalX_ = 0.f;
        normalY_ = 1.f;
        normalZ_ = 0.f;
        return;
    }
    const float center = sample(x, y, z);
    const float nx = x - h < originX_   ? sample(x + h, y, z) - center
                     : x + h > maxX     ? center - sample(x - h, y, z)
                                        : sample(x + h, y, z) - sample(x - h, y, z);
    const float ny = y - h < originY_   ? sample(x, y + h, z) - center
                     : y + h > maxY     ? center - sample(x, y - h, z)
                                        : sample(x, y + h, z) - sample(x, y - h, z);
    const float nz = z - h < originZ_   ? sample(x, y, z + h) - center
                     : z + h > maxZ     ? center - sample(x, y, z - h)
                                        : sample(x, y, z + h) - sample(x, y, z - h);
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 1e-6f) {
        normalX_ = nx / length;
        normalY_ = ny / length;
        normalZ_ = nz / length;
    } else {
        normalX_ = 0.f;
        normalY_ = 1.f;
        normalZ_ = 0.f;
    }
}

void DistanceField3D::setCollisionResult(float x, float y, float z, float radius) {
    collisionX_ = x;
    collisionY_ = y;
    collisionZ_ = z;
    collisionDistance_ = sample(x, y, z) - radius;
    sampleNormal(x, y, z);
    updateContactPoints(x, y, z, radius);
}

void DistanceField3D::updateContactPoints(float x, float y, float z, float radius) {
    const float fieldDistance = collisionDistance_ + radius;
    surfaceX_ = x - normalX_ * fieldDistance;
    surfaceY_ = y - normalY_ * fieldDistance;
    surfaceZ_ = z - normalZ_ * fieldDistance;
    shapeContactX_ = x - normalX_ * radius;
    shapeContactY_ = y - normalY_ * radius;
    shapeContactZ_ = z - normalZ_ * radius;
}

bool DistanceField3D::checkSphere(float x, float y, float z, float radius) {
    if (!(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("DistanceField3D.checkSphere: radius must be finite and >= 0");
    setCollisionResult(x, y, z, radius);
    return collisionDistance_ <= 0.f;
}

bool DistanceField3D::checkCapsule(float ax, float ay, float az, float bx, float by, float bz,
                                   float radius) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
        !std::isfinite(by) || !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception(
            "DistanceField3D.checkCapsule: coordinates and radius must be finite; radius >= 0");
    float bestX, bestY, bestZ;
    const float best = capsuleClearance(ax, ay, az, bx, by, bz, radius, bestX, bestY, bestZ);
    collisionX_ = bestX;
    collisionY_ = bestY;
    collisionZ_ = bestZ;
    collisionDistance_ = best;
    sampleNormal(bestX, bestY, bestZ);
    updateContactPoints(bestX, bestY, bestZ, radius);
    return best <= 0.f;
}

float DistanceField3D::capsuleClearance(float ax, float ay, float az, float bx, float by,
                                        float bz, float radius, float &closestX, float &closestY,
                                        float &closestZ) const {
    const float dx = bx - ax, dy = by - ay, dz = bz - az;
    const float length = std::hypot(dx, dy, dz);
    // A half-cell maximum interval is a conservative practical resolution for a sampled SDF.
    const int steps = queryIntervals(length, cellSize_ * 0.5, "DistanceField3D.checkCapsule");
    float best = std::numeric_limits<float>::infinity();
    closestX = ax;
    closestY = ay;
    closestZ = az;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = ax + dx * t, y = ay + dy * t, z = az + dz * t;
        const float clearance = sample(x, y, z) - radius;
        if (clearance < best) {
            best = clearance;
            closestX = x;
            closestY = y;
            closestZ = z;
        }
    }
    return best;
}

void DistanceField3D::setCastResult(float fraction, float travelLength, float x, float y, float z,
                                    float clearance, float radius, bool startedInside) {
    castFraction_ = fraction;
    castDistance_ = fraction * travelLength;
    castStartedInside_ = startedInside;
    collisionX_ = x;
    collisionY_ = y;
    collisionZ_ = z;
    collisionDistance_ = clearance;
    sampleNormal(x, y, z);
    updateContactPoints(x, y, z, radius);
}

bool DistanceField3D::castSphere(float x, float y, float z, float radius, float deltaX,
                                 float deltaY, float deltaZ) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !(radius >= 0.f) || !std::isfinite(radius) || !std::isfinite(deltaX) ||
        !std::isfinite(deltaY) || !std::isfinite(deltaZ))
        throw eve::Exception(
            "DistanceField3D.castSphere: coordinates, radius, and delta must be finite; radius >= 0");
    const float travel = std::hypot(deltaX, deltaY, deltaZ);
    const float initial = sample(x, y, z) - radius;
    if (initial <= 0.f) {
        setCastResult(0.f, travel, x, y, z, initial, radius, true);
        return true;
    }
    castFraction_ = 1.f;
    castDistance_ = travel;
    castStartedInside_ = false;
    if (travel <= 1e-7f) return false;

    const int steps = queryIntervals(travel, cellSize_ * 0.25, "DistanceField3D.castSphere");
    float previous = 0.f;
    for (int i = 1; i <= steps; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(steps);
        const float px = x + deltaX * fraction;
        const float py = y + deltaY * fraction;
        const float pz = z + deltaZ * fraction;
        if (sample(px, py, pz) - radius > 0.f) {
            previous = fraction;
            continue;
        }
        float low = previous, high = fraction;
        for (int iteration = 0; iteration < 14; ++iteration) {
            const float middle = 0.5f * (low + high);
            if (sample(x + deltaX * middle, y + deltaY * middle, z + deltaZ * middle) -
                    radius <=
                0.f)
                high = middle;
            else
                low = middle;
        }
        const float hx = x + deltaX * high;
        const float hy = y + deltaY * high;
        const float hz = z + deltaZ * high;
        setCastResult(high, travel, hx, hy, hz, sample(hx, hy, hz) - radius, radius, false);
        return true;
    }
    return false;
}

bool DistanceField3D::castCapsule(float ax, float ay, float az, float bx, float by, float bz,
                                  float radius, float deltaX, float deltaY, float deltaZ) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) ||
        !std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bz) ||
        !(radius >= 0.f) || !std::isfinite(radius) || !std::isfinite(deltaX) ||
        !std::isfinite(deltaY) || !std::isfinite(deltaZ))
        throw eve::Exception(
            "DistanceField3D.castCapsule: coordinates, radius, and delta must be finite; radius >= 0");
    const float travel = std::hypot(deltaX, deltaY, deltaZ);
    float hitX, hitY, hitZ;
    const float initial =
        capsuleClearance(ax, ay, az, bx, by, bz, radius, hitX, hitY, hitZ);
    if (initial <= 0.f) {
        setCastResult(0.f, travel, hitX, hitY, hitZ, initial, radius, true);
        return true;
    }
    castFraction_ = 1.f;
    castDistance_ = travel;
    castStartedInside_ = false;
    if (travel <= 1e-7f) return false;

    const int steps = queryIntervals(travel, cellSize_ * 0.25, "DistanceField3D.castCapsule");
    float previous = 0.f;
    for (int i = 1; i <= steps; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(steps);
        float closestX, closestY, closestZ;
        const float clearance = capsuleClearance(
            ax + deltaX * fraction, ay + deltaY * fraction, az + deltaZ * fraction,
            bx + deltaX * fraction, by + deltaY * fraction, bz + deltaZ * fraction, radius,
            closestX, closestY, closestZ);
        if (clearance > 0.f) {
            previous = fraction;
            continue;
        }
        float low = previous, high = fraction;
        for (int iteration = 0; iteration < 14; ++iteration) {
            const float middle = 0.5f * (low + high);
            float ignoredX, ignoredY, ignoredZ;
            if (capsuleClearance(
                    ax + deltaX * middle, ay + deltaY * middle, az + deltaZ * middle,
                    bx + deltaX * middle, by + deltaY * middle, bz + deltaZ * middle, radius,
                    ignoredX, ignoredY, ignoredZ) <= 0.f)
                high = middle;
            else
                low = middle;
        }
        const float finalClearance = capsuleClearance(
            ax + deltaX * high, ay + deltaY * high, az + deltaZ * high,
            bx + deltaX * high, by + deltaY * high, bz + deltaZ * high, radius, hitX, hitY,
            hitZ);
        setCastResult(high, travel, hitX, hitY, hitZ, finalClearance, radius, false);
        return true;
    }
    return false;
}

void DistanceField3D::setMoverUp(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("DistanceField3D.setMoverUp: direction must be finite");
    const float magnitude = std::hypot(x, y, z);
    if (!(magnitude > 1e-6f))
        throw eve::Exception("DistanceField3D.setMoverUp: direction must be non-zero");
    moverUpX_ = x / magnitude;
    moverUpY_ = y / magnitude;
    moverUpZ_ = z / magnitude;
}

void DistanceField3D::setMoverSlopeLimit(float degrees) {
    if (!std::isfinite(degrees))
        throw eve::Exception("DistanceField3D.setMoverSlopeLimit: degrees must be finite");
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    moverSlopeCos_ = std::cos(std::clamp(degrees, 0.f, 89.9f) * radiansPerDegree);
}

void DistanceField3D::setMoverSkinWidth(float width) {
    if (!(width >= 0.f) || !std::isfinite(width))
        throw eve::Exception("DistanceField3D.setMoverSkinWidth: width must be finite and >= 0");
    moverSkinWidth_ = width;
}

void DistanceField3D::setMoverGroundSnap(float distance) {
    if (!(distance >= 0.f) || !std::isfinite(distance))
        throw eve::Exception(
            "DistanceField3D.setMoverGroundSnap: distance must be finite and >= 0");
    moverGroundSnap_ = distance;
}

void DistanceField3D::setMoverStepHeight(float height) {
    if (!(height >= 0.f) || !std::isfinite(height))
        throw eve::Exception(
            "DistanceField3D.setMoverStepHeight: height must be finite and >= 0");
    moverStepHeight_ = height;
}

bool DistanceField3D::moveCapsule(float ax, float ay, float az, float bx, float by, float bz,
                                  float radius, float deltaX, float deltaY, float deltaZ) {
    // Validation and pathological capsule-length protection are shared with the overlap query.
    checkCapsule(ax, ay, az, bx, by, bz, radius);
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(deltaZ))
        throw eve::Exception("DistanceField3D.moveCapsule: delta must be finite");

    struct Vec3 {
        float x, y, z;
    };
    auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto length = [&](Vec3 value) { return std::sqrt(dot(value, value)); };

    constexpr int maxIterations = 6;
    constexpr int maxPlanes = 4;
    // Retain a numerical epsilon even when the caller requests zero visible skin, otherwise an
    // exact distance==radius contact would be classified as penetration on every solver pass.
    const float skin = std::max(moverSkinWidth_, cellSize_ * 1e-6f);
    Vec3 position{0.f, 0.f, 0.f};
    Vec3 remaining{deltaX, deltaY, deltaZ};
    Vec3 planes[maxPlanes]{};
    int planeCount = 0;
    bool constrained = false;
    moverIterations_ = 0;
    moverNormalX_ = 0.f;
    moverNormalY_ = 1.f;
    moverNormalZ_ = 0.f;
    moverGroundDot_ = -1.f;
    moverGrounded_ = false;
    moverHitWall_ = false;
    auto recordNormal = [&](Vec3 normal) {
        moverNormalX_ = normal.x;
        moverNormalY_ = normal.y;
        moverNormalZ_ = normal.z;
        const float upDot =
            normal.x * moverUpX_ + normal.y * moverUpY_ + normal.z * moverUpZ_;
        moverGroundDot_ = std::max(moverGroundDot_, upDot);
        if (upDot < moverSlopeCos_) moverHitWall_ = true;
    };

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        moverIterations_ = iteration + 1;
        const float currentAx = ax + position.x, currentAy = ay + position.y,
                    currentAz = az + position.z;
        const float currentBx = bx + position.x, currentBy = by + position.y,
                    currentBz = bz + position.z;

        if (checkCapsule(currentAx, currentAy, currentAz, currentBx, currentBy, currentBz,
                         radius)) {
            const float recovery = -collisionDistance_ + skin;
            Vec3 normal{normalX_, normalY_, normalZ_};
            position.x += normal.x * recovery;
            position.y += normal.y * recovery;
            position.z += normal.z * recovery;
            recordNormal(normal);
            constrained = true;
            if (planeCount < maxPlanes) planes[planeCount++] = normal;
            continue;
        }

        const float travel = length(remaining);
        if (travel <= 1e-6f) break;
        if (!castCapsule(currentAx, currentAy, currentAz, currentBx, currentBy, currentBz, radius,
                         remaining.x, remaining.y, remaining.z)) {
            position.x += remaining.x;
            position.y += remaining.y;
            position.z += remaining.z;
            remaining = {};
            break;
        }

        constrained = true;
        const Vec3 normal{normalX_, normalY_, normalZ_};
        recordNormal(normal);
        if (planeCount < maxPlanes) planes[planeCount++] = normal;

        const float safeFraction = std::max(0.f, castFraction_ - skin / travel);
        position.x += remaining.x * safeFraction;
        position.y += remaining.y * safeFraction;
        position.z += remaining.z * safeFraction;
        remaining.x *= 1.f - safeFraction;
        remaining.y *= 1.f - safeFraction;
        remaining.z *= 1.f - safeFraction;

        // Project against every accumulated contact plane. Repeating handles corners where
        // projecting against one plane can reintroduce motion into another.
        for (int pass = 0; pass < planeCount; ++pass) {
            for (int plane = 0; plane < planeCount; ++plane) {
                const float into = dot(remaining, planes[plane]);
                if (into < 0.f) {
                    remaining.x -= planes[plane].x * into;
                    remaining.y -= planes[plane].y * into;
                    remaining.z -= planes[plane].z * into;
                }
            }
        }
    }

    moverGrounded_ = moverGroundDot_ >= moverSlopeCos_;
    if (!moverGrounded_ && moverGroundSnap_ > 0.f) {
        const Vec3 snap{-moverUpX_ * moverGroundSnap_, -moverUpY_ * moverGroundSnap_,
                        -moverUpZ_ * moverGroundSnap_};
        if (castCapsule(ax + position.x, ay + position.y, az + position.z, bx + position.x,
                        by + position.y, bz + position.z, radius, snap.x, snap.y, snap.z) &&
            !castStartedInside_) {
            const Vec3 snapNormal{normalX_, normalY_, normalZ_};
            const float groundDot = snapNormal.x * moverUpX_ + snapNormal.y * moverUpY_ +
                                    snapNormal.z * moverUpZ_;
            if (groundDot >= moverSlopeCos_) {
                const float safeFraction =
                    std::max(0.f, castFraction_ - skin / moverGroundSnap_);
                position.x += snap.x * safeFraction;
                position.y += snap.y * safeFraction;
                position.z += snap.z * safeFraction;
                recordNormal(snapNormal);
                moverGrounded_ = true;
                constrained = true;
                ++moverIterations_;
            }
        }
    }
    moverDeltaX_ = position.x;
    moverDeltaY_ = position.y;
    moverDeltaZ_ = position.z;

    const Vec3 requested{deltaX, deltaY, deltaZ};
    const float requestedUp =
        requested.x * moverUpX_ + requested.y * moverUpY_ + requested.z * moverUpZ_;
    const Vec3 lateral{requested.x - moverUpX_ * requestedUp,
                       requested.y - moverUpY_ * requestedUp,
                       requested.z - moverUpZ_ * requestedUp};
    const float lateralLength = length(lateral);
    if (!moverStepping_ && moverStepHeight_ > 0.f && moverHitWall_ && lateralLength > 1e-6f) {
        struct MoverState {
            float dx, dy, dz, nx, ny, nz, groundDot;
            int iterations;
            bool grounded, hitWall;
        };
        const MoverState direct{moverDeltaX_,      moverDeltaY_,     moverDeltaZ_,
                                moverNormalX_,     moverNormalY_,    moverNormalZ_,
                                moverGroundDot_,   moverIterations_, moverGrounded_,
                                moverHitWall_};
        auto restore = [&](const MoverState &state) {
            moverDeltaX_ = state.dx;
            moverDeltaY_ = state.dy;
            moverDeltaZ_ = state.dz;
            moverNormalX_ = state.nx;
            moverNormalY_ = state.ny;
            moverNormalZ_ = state.nz;
            moverGroundDot_ = state.groundDot;
            moverIterations_ = state.iterations;
            moverGrounded_ = state.grounded;
            moverHitWall_ = state.hitWall;
        };
        const Vec3 up{moverUpX_ * moverStepHeight_, moverUpY_ * moverStepHeight_,
                      moverUpZ_ * moverStepHeight_};

        // The entire raised capsule must have ceiling clearance before trying the forward phase.
        if (!castCapsule(ax, ay, az, bx, by, bz, radius, up.x, up.y, up.z)) {
            moverStepping_ = true;
            try {
                moveCapsule(ax + up.x, ay + up.y, az + up.z, bx + up.x, by + up.y,
                            bz + up.z, radius, deltaX, deltaY, deltaZ);
            } catch (...) {
                moverStepping_ = false;
                throw;
            }
            moverStepping_ = false;

            Vec3 candidate{up.x + moverDeltaX_, up.y + moverDeltaY_, up.z + moverDeltaZ_};
            const int candidateIterations = moverIterations_;
            const bool candidateHitWall = moverHitWall_;
            const float downDistance = moverStepHeight_ + moverGroundSnap_ + skin;
            const Vec3 down{-moverUpX_ * downDistance, -moverUpY_ * downDistance,
                            -moverUpZ_ * downDistance};
            if (castCapsule(ax + candidate.x, ay + candidate.y, az + candidate.z,
                            bx + candidate.x, by + candidate.y, bz + candidate.z, radius, down.x,
                            down.y, down.z) &&
                !castStartedInside_) {
                const Vec3 landingNormal{normalX_, normalY_, normalZ_};
                const float landingDot = landingNormal.x * moverUpX_ +
                                         landingNormal.y * moverUpY_ +
                                         landingNormal.z * moverUpZ_;
                if (landingDot >= moverSlopeCos_) {
                    const float safeFraction =
                        std::max(0.f, castFraction_ - skin / downDistance);
                    candidate.x += down.x * safeFraction;
                    candidate.y += down.y * safeFraction;
                    candidate.z += down.z * safeFraction;
                    const float directProgress =
                        (direct.dx * lateral.x + direct.dy * lateral.y + direct.dz * lateral.z) /
                        lateralLength;
                    const float candidateProgress =
                        (candidate.x * lateral.x + candidate.y * lateral.y +
                         candidate.z * lateral.z) /
                        lateralLength;
                    if (candidateProgress > directProgress + std::max(1e-4f, skin)) {
                        moverDeltaX_ = candidate.x;
                        moverDeltaY_ = candidate.y;
                        moverDeltaZ_ = candidate.z;
                        moverNormalX_ = landingNormal.x;
                        moverNormalY_ = landingNormal.y;
                        moverNormalZ_ = landingNormal.z;
                        moverGroundDot_ = landingDot;
                        moverGrounded_ = true;
                        moverHitWall_ = candidateHitWall;
                        moverIterations_ = candidateIterations + 2;
                        return true;
                    }
                }
            }
        }
        restore(direct);
    }
    return constrained;
}

}  // namespace eve::physics
