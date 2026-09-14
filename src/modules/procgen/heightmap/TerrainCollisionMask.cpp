#include "procgen/heightmap/TerrainCollisionMask.h"

#include <algorithm>
#include <vector>

#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
using namespace raster_detail;

struct TerrainCollisionMaskStack::Impl {
    struct Layer {
        Heightmap mask;
        TerrainCollisionMaskType type;
        bool active;
        bool invert;
    };
    std::vector<Layer> layers;
};

TerrainCollisionMaskStack::TerrainCollisionMaskStack() : impl_(std::make_unique<Impl>()) {}
TerrainCollisionMaskStack::~TerrainCollisionMaskStack() = default;
TerrainCollisionMaskStack::TerrainCollisionMaskStack(TerrainCollisionMaskStack&&) noexcept = default;
TerrainCollisionMaskStack& TerrainCollisionMaskStack::operator=(TerrainCollisionMaskStack&&) noexcept = default;

Result<int> TerrainCollisionMaskStack::addLayer(const Heightmap& mask, TerrainCollisionMaskType type, bool active,
                                                 bool invert) {
    if (!impl_ || !validRaster(mask) || type < TerrainCollisionMaskType::RadiusTree ||
        type > TerrainCollisionMaskType::LayerTree)
        return invalid("terrain.collisionMask: finite mask and valid source type required");
    auto candidate = impl_->layers;
    candidate.push_back({mask, type, active, invert});
    impl_->layers.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->layers.size()));
}

void TerrainCollisionMaskStack::clear() {
    if (impl_) impl_->layers.clear();
}

int TerrainCollisionMaskStack::getLayerCount() const noexcept {
    return impl_ ? static_cast<int>(impl_->layers.size()) : 0;
}

Result<int> TerrainCollisionMaskStack::apply(Heightmap& target, const Heightmap& input, const Heightmap& curve,
                                              TerrainMaskBlend mode) const {
    if (!impl_ || !validRaster(target) || !validRaster(input) || !validRaster(curve) ||
        target.getWidth() != input.getWidth() || target.getHeight() != input.getHeight() ||
        curve.getHeight() != 1 || mode < TerrainMaskBlend::Multiply || mode > TerrainMaskBlend::Subtract)
        return invalid("terrain.collisionMask.apply: compatible finite target, input, curve and mode required");
    std::vector<float> output(target.data().size());
    for (int z = 0; z < target.getHeight(); ++z) {
        for (int x = 0; x < target.getWidth(); ++x) {
            const double u = (x + 0.5) / target.getWidth();
            const double v = (z + 0.5) / target.getHeight();
            double collisionFree = 1;
            for (const auto& layer : impl_->layers) {
                if (!layer.active) continue;
                double baked = textureSample(layer.mask, u, v);
                const bool layerType = layer.type == TerrainCollisionMaskType::LayerGameObject ||
                                       layer.type == TerrainCollisionMaskType::LayerTree;
                if (layerType ? !layer.invert : layer.invert) baked = 1 - baked;
                collisionFree = std::min(collisionFree, baked);
            }
            const double filtered = curveSample(curve, collisionFree);
            const std::size_t index = static_cast<std::size_t>(z) * target.getWidth() + x;
            const double old = input.data()[index];
            double result = old;
            switch (mode) {
                case TerrainMaskBlend::Multiply: result = old * filtered; break;
                case TerrainMaskBlend::Maximum: result = std::max(old, filtered); break;
                case TerrainMaskBlend::Minimum: result = std::min(old, filtered); break;
                case TerrainMaskBlend::Add: result = old + filtered; break;
                case TerrainMaskBlend::Subtract: result = old - filtered; break;
            }
            if (!isRepresentable(result))
                return invalid("terrain.collisionMask.apply: output exceeds finite float range");
            output[index] = static_cast<float>(result);
        }
    }
    return publish(target, std::move(output));
}

}  // namespace eve::procgen
