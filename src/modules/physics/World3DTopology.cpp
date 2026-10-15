#include <utility>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace eve::physics {
void World3D::adoptPreparedTopology(World3D &prepared) {
    cancelForceStep();
    adoptCouplings3D(prepared);
    auto swapped = rigidBackend_->swapTopology(*prepared.rigidBackend_);
    if (!swapped) throw eve::Exception("%s", swapped.status().describe().c_str());
    std::swap(nextId_, prepared.nextId_);
    std::swap(nextShapeId_, prepared.nextShapeId_);
    std::swap(nextJointId_, prepared.nextJointId_);
    std::swap(nextBodyHandleIndex_, prepared.nextBodyHandleIndex_);
    std::swap(nextShapeHandleIndex_, prepared.nextShapeHandleIndex_);
    std::swap(nextJointHandleIndex_, prepared.nextJointHandleIndex_);
    std::swap(bodies_, prepared.bodies_);
    std::swap(shapes_, prepared.shapes_);
    std::swap(joints_, prepared.joints_);
    std::swap(mechanisms_, prepared.mechanisms_);
    std::swap(restoredBodies_, prepared.restoredBodies_);
    std::swap(restoredShapes_, prepared.restoredShapes_);
    std::swap(restoredJoints_, prepared.restoredJoints_);
    std::swap(shapeRecords_, prepared.shapeRecords_);
    std::swap(shapeHandles_, prepared.shapeHandles_);
    std::swap(shapeRawHandles_, prepared.shapeRawHandles_);
    std::swap(jointHandles_, prepared.jointHandles_);
    std::swap(simulationTick_, prepared.simulationTick_);
    for (Body3D *body : bodies_) body->world_ = this;
    for (Shape3D *shape : shapes_) shape->world_ = this;
    for (Joint3D *joint : joints_) joint->world_ = this;
    for (Body3D *body : prepared.bodies_) body->world_ = &prepared;
    for (Shape3D *shape : prepared.shapes_) shape->world_ = &prepared;
    for (Joint3D *joint : prepared.joints_) joint->world_ = &prepared;
}

}  // namespace eve::physics
