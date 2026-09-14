#include "graphics/hair/GroomAsset.h"

#include "common/Diagnostic.h"

#include <string>

namespace eve::graphics::hair {

Result<void> GroomAsset::addGroup(GroomGroup group) {
    if (group.name.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomAsset::addGroup: empty name",
            "hair.groom.group.name"));
    }
    for (const GroomGroup &existing : groups_) {
        if (existing.groupId == group.groupId) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::AlreadyExists, "GroomAsset::addGroup: duplicate groupId",
                "hair.groom.group.id"));
        }
    }
    if (group.lods.empty()) {
        GroomLod lod;
        lod.screenSize = 1.f;
        lod.representation = Representation::Strands;
        lod.curveFraction = 1.f;
        lod.thicknessScale = 1.f;
        group.lods.push_back(lod);
    }
    auto strandsOk = group.strands.validate();
    if (!strandsOk.ok()) return Result<void>::failure(strandsOk.status());
    groups_.push_back(std::move(group));
    return Result<void>::success();
}

const GroomGroup *GroomAsset::groupAt(size_t index) const {
    if (index >= groups_.size()) return nullptr;
    return &groups_[index];
}

const GroomGroup *GroomAsset::findGroupById(uint32_t groupId) const {
    for (const GroomGroup &g : groups_) {
        if (g.groupId == groupId) return &g;
    }
    return nullptr;
}

void GroomAsset::clear() { groups_.clear(); }

Result<void> GroomAsset::validate() const {
    if (groups_.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomAsset: no groups", "hair.groom.groups"));
    }
    for (size_t i = 0; i < groups_.size(); ++i) {
        auto ok = groups_[i].strands.validate();
        if (!ok.ok()) return Result<void>::failure(ok.status());
        if (groups_[i].lods.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "GroomAsset: group has empty LOD table",
                "hair.groom.groups[" + std::to_string(i) + "].lods"));
        }
    }
    return Result<void>::success();
}

size_t selectLodIndex(const std::vector<GroomLod> &lods, float screenSize) {
    if (lods.empty()) return 0;
    // Ordered finest → coarsest. Pick the last entry whose threshold is still
    // >= the current screen size (i.e. the coarsest LOD that still applies).
    size_t best = 0;
    for (size_t i = 0; i < lods.size(); ++i) {
        if (screenSize <= lods[i].screenSize) best = i;
    }
    return best;
}

}  // namespace eve::graphics::hair
