#include "physics/cloth/Cloth3D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace eve::physics {

float Cloth3D::getTearThreshold() const { return tearThreshold_; }
int Cloth3D::getMaxTearsPerStep() const { return maxTearsPerStep_; }
int Cloth3D::getTornConstraintCount() const { return tornConstraintCount_; }

void Cloth3D::setTearThreshold(float strain) {
    tearThreshold_ = std::isfinite(strain) && strain > 1.f ? strain : 0.f;
}

void Cloth3D::setMaxTearsPerStep(int count) { maxTearsPerStep_ = std::max(0, count); }

void Cloth3D::tearConstraint(int particleA, int particleB) {
    if (!validIndex(particleA) || !validIndex(particleB))
        throw Exception("Cloth3D.tearConstraint: particle index out of range");
    const auto found = std::find_if(links_.begin(), links_.end(), [particleA, particleB](const Link& link) {
        return link.kind == ClothConstraintKind::Structural &&
               ((link.a == particleA && link.b == particleB) || (link.a == particleB && link.b == particleA));
    });
    if (found == links_.end()) throw Exception("Cloth3D.tearConstraint: structural edge not found");
    tearLink(static_cast<size_t>(std::distance(links_.begin(), found)));
}

void Cloth3D::applyAutomaticTearing() {
    if (tearThreshold_ <= 1.f || maxTearsPerStep_ <= 0) return;
    int torn = 0;
    for (size_t index = 0; index < links_.size() && torn < maxTearsPerStep_;) {
        const Link& link = links_[index];
        if (link.kind != ClothConstraintKind::Structural || link.rest <= 1e-7f) {
            ++index;
            continue;
        }
        const Particle& a = particles_[static_cast<size_t>(link.a)];
        const Particle& b = particles_[static_cast<size_t>(link.b)];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float dz = b.z - a.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= link.rest * tearThreshold_) {
            ++index;
            continue;
        }
        tearLink(index);
        ++torn;
    }
}

void Cloth3D::tearLink(size_t linkIndex) {
    const int a = links_[linkIndex].a;
    const int b = links_[linkIndex].b;
    links_.erase(links_.begin() + static_cast<std::ptrdiff_t>(linkIndex));
    std::erase_if(triangles_, [a, b](const Tri& triangle) {
        bool hasA = false;
        bool hasB = false;
        for (int vertex : triangle.v) {
            hasA = hasA || vertex == a;
            hasB = hasB || vertex == b;
        }
        return hasA && hasB;
    });
    ++tornConstraintCount_;
    buildLinkKeys();
    rebuildFoldPairsFromTriangles();
}

void Cloth3D::rebuildFoldPairsFromTriangles() {
    struct EdgeUse {
        int a = 0;
        int b = 0;
        std::vector<int> opposites;
    };
    std::map<std::pair<int, int>, EdgeUse> edges;
    for (const Tri& triangle : triangles_) {
        for (int edge = 0; edge < 3; ++edge) {
            const int first    = triangle.v[edge];
            const int second   = triangle.v[(edge + 1) % 3];
            const int opposite = triangle.v[(edge + 2) % 3];
            const auto key = std::minmax(first, second);
            EdgeUse& use = edges[{key.first, key.second}];
            use.a = key.first;
            use.b = key.second;
            use.opposites.push_back(opposite);
        }
    }
    foldPairs_.clear();
    for (const auto& [key, use] : edges)
        if (use.opposites.size() == 2)
            foldPairs_.push_back({use.a, use.b, use.opposites[0], use.opposites[1]});
}

}  // namespace eve::physics
