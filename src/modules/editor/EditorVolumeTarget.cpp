#include "editor/EditorVolumeTarget.h"

#ifdef EVENGINE_HAS_VOXEL
#include "voxel/VoxelWorld.h"
#endif

#include <utility>

namespace eve::editor {

void EditVolume::include(int x, int y, int z) {
    if (empty()) {
        minX = maxX = x;
        minY = maxY = y;
        minZ = maxZ = z;
        return;
    }
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
    if (z < minZ) minZ = z;
    if (z > maxZ) maxZ = z;
}

void EditVolume::include(const EditVolume& other) {
    if (other.empty()) return;
    include(other.minX, other.minY, other.minZ);
    include(other.maxX, other.maxY, other.maxZ);
}

EditRegion EditVolume::xzRegion() const {
    EditRegion region;
    if (!empty()) {
        region.include(minX, minZ);
        region.include(maxX, maxZ);
    }
    return region;
}

#ifdef EVENGINE_HAS_VOXEL
VoxelWorldTarget::VoxelWorldTarget(std::string id, voxel::VoxelWorld* world)
    : id_(std::move(id)), world_(world) {}

unsigned long long VoxelWorldTarget::revision() const { return world_ ? world_->getRevision() : 0; }

TargetDescriptor VoxelWorldTarget::describe() const {
    TargetDescriptor descriptor;
    descriptor.id = TargetId(id_);
    descriptor.type = "voxel-world";
    descriptor.revision = revision();
    descriptor.capabilities = {IIntVolumeTarget::editorCapabilityId()};
    return descriptor;
}

void* VoxelWorldTarget::queryCapability(const CapabilityId& capability) {
    if (capability == IIntVolumeTarget::editorCapabilityId()) return static_cast<IIntVolumeTarget*>(this);
    return nullptr;
}

int VoxelWorldTarget::readInt3(int x, int y, int z) const {
    return world_ ? static_cast<int>(world_->getVoxel(x, y, z)) : 0;
}

bool VoxelWorldTarget::writeInt3(int x, int y, int z, int value) {
    if (!world_ || value < 0 || value > 255 || readInt3(x, y, z) == value) return false;
    world_->setVoxel(x, y, z, static_cast<uint8_t>(value));
    dirty_.include(x, y, z);
    return true;
}
#endif

IntVolumeEditCommand::IntVolumeEditCommand(std::string name, IEditableTarget* target)
    : name_(std::move(name)), target_(target) {}

bool IntVolumeEditCommand::record(int x, int y, int z, int after) {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return false;
    for (auto& change : changes_) {
        if (change.x == x && change.y == y && change.z == z) {
            change.after = after;
            return true;
        }
    }
    const int before = volume->readInt3(x, y, z);
    if (before == after) return false;
    changes_.push_back({x, y, z, before, after});
    dirty_.include(x, y, z);
    return true;
}

bool IntVolumeEditCommand::apply() {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return false;
    for (const auto& change : changes_) volume->writeInt3(change.x, change.y, change.z, change.after);
    return !changes_.empty();
}

void IntVolumeEditCommand::revert() {
    auto* volume = target_ ? target_->query<IIntVolumeTarget>() : nullptr;
    if (!volume) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it)
        volume->writeInt3(it->x, it->y, it->z, it->before);
}

std::unique_ptr<IEditCommand> IntVolumeEditCommand::clone() const {
    return std::make_unique<IntVolumeEditCommand>(*this);
}

bool IntVolumeEditCommand::mergeWith(const IEditCommand& laterBase) {
    const auto* later = dynamic_cast<const IntVolumeEditCommand*>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto& incoming : later->changes_) {
        bool merged = false;
        for (auto& current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y && current.z == incoming.z) {
                current.after = incoming.after;
                merged = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

}  // namespace eve::editor
