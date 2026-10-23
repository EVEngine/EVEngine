#include "graphics/SparseVolumeTexture.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include "graphics/VolumeDensityGraph.h"

// SparseVolumeTexture.cpp
namespace eve::graphics {
namespace {

constexpr char          kMagic[4] = {'E', 'V', 'S', 'V'};
constexpr std::uint32_t kVersion  = 1;

template <typename T>
bool writeValue(std::ofstream &stream, const T &value) {
    stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
    return bool(stream);
}

template <typename T>
bool readValue(std::ifstream &stream, T &value) {
    stream.read(reinterpret_cast<char *>(&value), sizeof(T));
    return bool(stream);
}

bool isVacuum(const FogFroxel &f) {
    return f.extinction == 0.f && f.scattering == glm::vec3(0.f) && f.emissive == glm::vec3(0.f);
}

}  // namespace

void SparseVolumeTexture::resize(int width, int height, int depth, int brickSize) {
    width_     = std::max(width, 1);
    height_    = std::max(height, 1);
    depth_     = std::max(depth, 1);
    brickSize_ = std::clamp(brickSize, 2, 32);
    bricks_.clear();
}

void SparseVolumeTexture::clear() { bricks_.clear(); }

std::size_t SparseVolumeTexture::getAllocatedVoxelCount() const {
    const std::size_t perBrick = std::size_t(brickSize_) * std::size_t(brickSize_) * std::size_t(brickSize_);
    return bricks_.size() * perBrick;
}

bool SparseVolumeTexture::inBounds(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < width_ && y < height_ && z < depth_;
}

std::uint64_t SparseVolumeTexture::brickKey(int bx, int by, int bz) const {
    return (std::uint64_t(std::uint32_t(bx)) << 42) | (std::uint64_t(std::uint32_t(by)) << 21) |
           std::uint64_t(std::uint32_t(bz));
}

std::size_t SparseVolumeTexture::localIndex(int x, int y, int z) const {
    const int lx = x % brickSize_;
    const int ly = y % brickSize_;
    const int lz = z % brickSize_;
    return (std::size_t(lz) * std::size_t(brickSize_) + std::size_t(ly)) * std::size_t(brickSize_) + std::size_t(lx);
}

void SparseVolumeTexture::setVoxel(int x, int y, int z, const FogFroxel &voxel) {
    if (!inBounds(x, y, z)) return;
    const std::uint64_t key   = brickKey(x / brickSize_, y / brickSize_, z / brickSize_);
    auto                found = bricks_.find(key);
    if (found == bricks_.end()) {
        if (isVacuum(voxel)) return;
        Brick brick;
        brick.voxels.resize(std::size_t(brickSize_) * std::size_t(brickSize_) * std::size_t(brickSize_));
        found = bricks_.emplace(key, std::move(brick)).first;
    }
    found->second.voxels[localIndex(x, y, z)] = voxel;
}

FogFroxel SparseVolumeTexture::getVoxel(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return {};
    const auto found = bricks_.find(brickKey(x / brickSize_, y / brickSize_, z / brickSize_));
    return found == bricks_.end() ? FogFroxel{} : found->second.voxels[localIndex(x, y, z)];
}

FogFroxel SparseVolumeTexture::sample(float u, float v, float w) const {
    return getVoxel(std::clamp(int(u * float(width_)), 0, width_ - 1),
                    std::clamp(int(v * float(height_)), 0, height_ - 1),
                    std::clamp(int(w * float(depth_)), 0, depth_ - 1));
}

void SparseVolumeTexture::bake(const VolumeDensityGraph &graph, const glm::vec3 &worldMin, const glm::vec3 &worldMax,
                               float extinctionScale, const glm::vec3 &albedo, float emptyThreshold, float time) {
    clear();
    const glm::vec3 omega = glm::clamp(albedo, glm::vec3(0.f), glm::vec3(1.f));
    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 uvw((float(x) + 0.5f) / float(width_), (float(y) + 0.5f) / float(height_),
                                    (float(z) + 0.5f) / float(depth_));
                const float     extinction =
                    std::max(0.f, graph.evaluate(worldMin + (worldMax - worldMin) * uvw, time) * extinctionScale);
                if (extinction <= emptyThreshold) continue;
                FogFroxel voxel;
                voxel.extinction = extinction;
                voxel.scattering = omega * extinction;
                setVoxel(x, y, z, voxel);
            }
        }
    }
}

bool SparseVolumeTexture::save(const std::string &path) const {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(kMagic, sizeof(kMagic));
    const std::uint32_t width = std::uint32_t(width_), height = std::uint32_t(height_);
    const std::uint32_t depth = std::uint32_t(depth_), brickSize = std::uint32_t(brickSize_);
    const std::uint64_t brickCount = std::uint64_t(bricks_.size());
    if (!writeValue(stream, kVersion) || !writeValue(stream, width) || !writeValue(stream, height) ||
        !writeValue(stream, depth) || !writeValue(stream, brickSize) || !writeValue(stream, brickCount))
        return false;
    std::vector<std::uint64_t> keys;
    keys.reserve(bricks_.size());
    for (const auto &entry : bricks_) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    for (std::uint64_t key : keys) {
        if (!writeValue(stream, key)) return false;
        const Brick &brick = bricks_.at(key);
        for (const FogFroxel &f : brick.voxels) {
            const float values[9] = {f.scattering.x, f.scattering.y, f.scattering.z, f.extinction,     f.emissive.x,
                                     f.emissive.y,   f.emissive.z,   f.anisotropy,   f.lightVisibility};
            stream.write(reinterpret_cast<const char *>(values), sizeof(values));
            if (!stream) return false;
        }
    }
    return true;
}

bool SparseVolumeTexture::load(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    char          magic[4]{};
    stream.read(magic, sizeof(magic));
    std::uint32_t version = 0, width = 0, height = 0, depth = 0, brickSize = 0;
    std::uint64_t brickCount = 0;
    if (!stream || !std::equal(std::begin(magic), std::end(magic), std::begin(kMagic)) || !readValue(stream, version) ||
        version != kVersion || !readValue(stream, width) || !readValue(stream, height) || !readValue(stream, depth) ||
        !readValue(stream, brickSize) || !readValue(stream, brickCount) || width == 0 || height == 0 || depth == 0 ||
        brickSize < 2 || brickSize > 32)
        return false;
    SparseVolumeTexture loaded;
    loaded.resize(int(width), int(height), int(depth), int(brickSize));
    const std::size_t voxelsPerBrick = std::size_t(brickSize) * std::size_t(brickSize) * std::size_t(brickSize);
    for (std::uint64_t i = 0; i < brickCount; ++i) {
        std::uint64_t key = 0;
        if (!readValue(stream, key)) return false;
        Brick brick;
        brick.voxels.resize(voxelsPerBrick);
        for (FogFroxel &f : brick.voxels) {
            float values[9]{};
            stream.read(reinterpret_cast<char *>(values), sizeof(values));
            if (!stream) return false;
            f.scattering      = {values[0], values[1], values[2]};
            f.extinction      = values[3];
            f.emissive        = {values[4], values[5], values[6]};
            f.anisotropy      = values[7];
            f.lightVisibility = values[8];
        }
        loaded.bricks_.emplace(key, std::move(brick));
    }
    *this = std::move(loaded);
    return true;
}

}  // namespace eve::graphics

// VolumeDensityGraph.cpp
namespace eve::graphics {
namespace {

float hash3(const glm::ivec3 &p, std::uint32_t seed) {
    std::uint32_t h = seed ^ (std::uint32_t(p.x) * 0x8da6b343u) ^ (std::uint32_t(p.y) * 0xd8163841u) ^
                      (std::uint32_t(p.z) * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return float(h & 0x00ffffffu) / float(0x01000000u);
}

}  // namespace

void VolumeDensityGraph::clear() {
    nodes_.clear();
    output_ = -1;
}

int VolumeDensityGraph::addNode(Op op, int inputA, int inputB, const glm::vec4 &params) {
    const int next = int(nodes_.size());
    Node      node;
    node.op     = op;
    node.inputA = inputA >= 0 && inputA < next ? inputA : -1;
    node.inputB = inputB >= 0 && inputB < next ? inputB : -1;
    node.params = params;
    nodes_.push_back(node);
    output_ = next;
    return next;
}

void VolumeDensityGraph::setOutput(int nodeIndex) {
    output_ = nodeIndex >= 0 && nodeIndex < int(nodes_.size()) ? nodeIndex : int(nodes_.size()) - 1;
}

float VolumeDensityGraph::valueNoise(const glm::vec3 &p, std::uint32_t seed) {
    const glm::ivec3 cell = glm::ivec3(glm::floor(p));
    const glm::vec3  f    = glm::fract(p);
    const glm::vec3  u    = f * f * (3.f - 2.f * f);
    float            corners[2][2][2];
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) corners[z][y][x] = hash3(cell + glm::ivec3(x, y, z), seed);
    const float z0 = glm::mix(glm::mix(corners[0][0][0], corners[0][0][1], u.x),
                              glm::mix(corners[0][1][0], corners[0][1][1], u.x), u.y);
    const float z1 = glm::mix(glm::mix(corners[1][0][0], corners[1][0][1], u.x),
                              glm::mix(corners[1][1][0], corners[1][1][1], u.x), u.y);
    return glm::mix(z0, z1, u.z);
}

float VolumeDensityGraph::evaluate(const glm::vec3 &worldPosition, float time) const {
    if (nodes_.empty()) return 0.f;
    std::vector<float> values(nodes_.size(), 0.f);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node &n = nodes_[i];
        const float a = n.inputA >= 0 ? values[std::size_t(n.inputA)] : 0.f;
        const float b = n.inputB >= 0 ? values[std::size_t(n.inputB)] : 0.f;
        switch (n.op) {
            case Op::constant: values[i] = n.params.x; break;
            case Op::height:
                values[i] = std::exp(-std::max(n.params.y, 0.f) * std::max(worldPosition.y - n.params.x, 0.f));
                break;
            case Op::sphere: {
                const float radius = std::max(n.params.w, 1e-4f);
                values[i] = std::clamp(1.f - glm::length(worldPosition - glm::vec3(n.params)) / radius, 0.f, 1.f);
                break;
            }
            case Op::box: {
                const glm::vec3 q      = glm::abs(worldPosition - glm::vec3(n.params));
                const float     extent = std::max(n.params.w, 1e-4f);
                values[i]              = std::clamp(1.f - std::max(std::max(q.x, q.y), q.z) / extent, 0.f, 1.f);
                break;
            }
            case Op::noise: {
                const float     scale = std::max(std::fabs(n.params.x), 1e-4f);
                const glm::vec3 wind(n.params.y, 0.f, n.params.z);
                values[i] = valueNoise((worldPosition + wind * time) / scale, std::uint32_t(std::max(n.params.w, 0.f)));
                break;
            }
            case Op::add: values[i] = a + b; break;
            case Op::multiply: values[i] = a * b; break;
            case Op::subtract: values[i] = a - b; break;
            case Op::clamp: values[i] = std::clamp(a, n.params.x, n.params.y); break;
        }
    }
    const int selected = output_ >= 0 ? output_ : int(nodes_.size()) - 1;
    return values[std::size_t(selected)];
}

}  // namespace eve::graphics
