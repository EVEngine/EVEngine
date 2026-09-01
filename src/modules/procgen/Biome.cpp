#include "procgen/Biome.h"

#include <algorithm>
#include <sstream>

namespace eve::procgen {
namespace {

uint32_t mixBiome(uint32_t value) {
    value += 0x9e3779b9u;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}

float biomeUnit(uint32_t seed) { return float(mixBiome(seed) >> 8u) * (1.f / 16777216.f); }

}  // namespace

void BiomeRules::clear() {
    layers_.clear();
    exclusions_.clear();
    error_.clear();
    lastCandidateCount_ = 0;
    lastOutputCount_    = 0;
}

bool BiomeRules::addLayer(const std::string& name, SpatialData* spatial, int priority, float density) {
    if (name.empty() || !spatial || hasLayer(name)) return false;
    BiomeLayerRule layer;
    layer.name     = name;
    layer.priority = priority;
    layer.density  = std::clamp(density, 0.f, 1.f);
    layer.spatial  = std::make_shared<SpatialData>(*spatial);
    layers_.push_back(std::move(layer));
    return true;
}

bool BiomeRules::removeLayer(const std::string& name) {
    const auto found =
        std::find_if(layers_.begin(), layers_.end(), [&](const BiomeLayerRule& layer) { return layer.name == name; });
    if (found == layers_.end()) return false;
    layers_.erase(found);
    return true;
}

bool BiomeRules::hasLayer(const std::string& name) const { return findLayer(name) != nullptr; }
int  BiomeRules::getLayerCount() const { return int(layers_.size()); }
std::string BiomeRules::getLayerName(int index) const {
    return index >= 0 && index < int(layers_.size()) ? layers_[size_t(index)].name : std::string();
}
int BiomeRules::getLayerPriority(const std::string& name) const {
    const auto* layer = findLayer(name);
    return layer ? layer->priority : 0;
}
float BiomeRules::getLayerDensity(const std::string& name) const {
    const auto* layer = findLayer(name);
    return layer ? layer->density : 0.f;
}

bool BiomeRules::addAsset(const std::string& layerName, const std::string& asset, float weight, float minScale,
                          float maxScale, bool randomYaw) {
    auto* layer = findLayer(layerName);
    if (!layer || asset.empty() || weight <= 0.f || minScale <= 0.f || maxScale <= 0.f) return false;
    if (minScale > maxScale) std::swap(minScale, maxScale);
    layer->assets.push_back({asset, weight, minScale, maxScale, randomYaw});
    return true;
}

int BiomeRules::getAssetCount(const std::string& layerName) const {
    const auto* layer = findLayer(layerName);
    return layer ? int(layer->assets.size()) : 0;
}
std::string BiomeRules::getAssetName(const std::string& layerName, int index) const {
    const auto* layer = findLayer(layerName);
    return layer && index >= 0 && index < int(layer->assets.size()) ? layer->assets[size_t(index)].asset
               : std::string();
}

bool BiomeRules::addExclusion(SpatialData* spatial) {
    if (!spatial) return false;
    exclusions_.push_back(std::make_shared<SpatialData>(*spatial));
    return true;
}
int BiomeRules::getExclusionCount() const { return int(exclusions_.size()); }

PointSet* BiomeRules::generate(SpatialData* domain, float spacing, uint32_t seed, float jitter) {
    error_.clear();
    lastCandidateCount_ = 0;
    lastOutputCount_    = 0;
    if (!domain || spacing <= 0.f) {
        error_ = "generate: requires domain and positive spacing";
        return nullptr;
    }
    if (layers_.empty()) {
        error_ = "generate: no biome layers";
        return nullptr;
    }
    PointSet candidates = domain->sample(spacing, seed, jitter);
    lastCandidateCount_ = candidates.getCount();
    PointSet output;
    for (size_t candidateIndex = 0; candidateIndex < candidates.points().size(); ++candidateIndex) {
        const auto& candidate = candidates.points()[candidateIndex];
        bool excluded = false;
        for (const auto& exclusion : exclusions_) {
            if (exclusion->contains(candidate.x, candidate.y, candidate.z)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        const BiomeLayerRule* selected = nullptr;
        for (const auto& layer : layers_) {
            if (!layer.spatial->contains(candidate.x, candidate.y, candidate.z)) continue;
            if (!selected || layer.priority > selected->priority) selected = &layer;
        }
        if (!selected || selected->assets.empty()) continue;
        const uint32_t densitySeed = mixBiome(seed ^ candidate.seed ^ 0x243f6a88u);
        if (biomeUnit(densitySeed) >= selected->density) continue;

        float totalWeight = 0.f;
        for (const auto& asset : selected->assets) totalWeight += asset.weight;
        float choice = biomeUnit(densitySeed ^ 0xb7e15162u) * totalWeight;
        const BiomeAssetRule* selectedAsset = &selected->assets.back();
        for (const auto& asset : selected->assets) {
            choice -= asset.weight;
            if (choice <= 0.f) {
                selectedAsset = &asset;
                break;
            }
        }

        ProcgenPoint point = candidate;
        point.density = selected->density;
        const float scaleT = biomeUnit(densitySeed ^ 0x9e3779b9u);
        const float scale  = selectedAsset->minScale + (selectedAsset->maxScale - selectedAsset->minScale) * scaleT;
        point.scaleX = point.scaleY = point.scaleZ = scale;
        if (selectedAsset->randomYaw) point.yaw = biomeUnit(densitySeed ^ 0xdeadbeefu) * 360.f;
        const int outputIndex =
            std::move(output.appendPointFrom(candidates, candidateIndex)).expect("biome output attribute schema");
        output.mutablePoint(size_t(outputIndex)) = std::move(point);
        output.trySetStringAttribute(outputIndex, "biome", selected->name).expect("biome metadata schema");
        output.trySetStringAttribute(outputIndex, "asset", selectedAsset->asset).expect("biome asset metadata schema");
    }
    lastOutputCount_ = output.getCount();
    return new PointSet(std::move(output));
}

std::string BiomeRules::getError() const { return error_; }
std::string BiomeRules::debugReport() const {
    std::ostringstream out;
    out << "layers=" << layers_.size() << " exclusions=" << exclusions_.size() << " candidates=" << lastCandidateCount_
        << " output=" << lastOutputCount_;
    if (!error_.empty()) out << " error=" << error_;
    return out.str();
}

BiomeLayerRule* BiomeRules::findLayer(const std::string& name) {
    const auto found =
        std::find_if(layers_.begin(), layers_.end(), [&](const BiomeLayerRule& layer) { return layer.name == name; });
    return found == layers_.end() ? nullptr : &*found;
}
const BiomeLayerRule* BiomeRules::findLayer(const std::string& name) const {
    const auto found =
        std::find_if(layers_.begin(), layers_.end(), [&](const BiomeLayerRule& layer) { return layer.name == name; });
    return found == layers_.end() ? nullptr : &*found;
}

}  // namespace eve::procgen
