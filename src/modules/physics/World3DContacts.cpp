#include "physics/Joint3D.h"
#include "physics/World3DInternal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace eve::physics {
using namespace world3d_detail;


void World3D::clearContactEvents() {
    beginContacts_.clear();
    endContacts_.clear();
    beginTriggers_.clear();
    endTriggers_.clear();
    hits_.clear();
    jointStressEvents_.clear();
    contactPoints_.clear();
}

int World3D::queryBodyContacts(int bodyId, int maxPoints) {
    contactPoints_.clear();
    if (bodyId <= 0) throw eve::Exception("World3D.queryBodyContacts: bodyId must be a positive stable id");
    if (maxPoints < 1 || maxPoints > 4096)
        throw eve::Exception("World3D.queryBodyContacts: maxPoints must be in [1, 4096]");
    const auto *body = findBodyById(bodyId);
    if (!body) return 0;
    auto result = queryBodyContactsOwned(body->runtimeHandle(), maxPoints);
    if (!result) throw eve::Exception("World3D.queryBodyContacts: %s", result.status().describe().c_str());
    contactPoints_ = std::move(result).takeValue().points;
    return static_cast<int>(contactPoints_.size());
}

eve::Result<BodyContacts3D> World3D::queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const {
    return rigidBackend_->queryBodyContactsOwned(body, maxPoints);
}


const World3D::ContactEvent &World3D::contactEventAt(const std::vector<ContactEvent> &events, int index,
                                                     const char *operation) const {
    if (index < 0 || index >= static_cast<int>(events.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return events[static_cast<size_t>(index)];
}

const World3D::TriggerEvent &World3D::triggerEventAt(const std::vector<TriggerEvent> &events, int index,
                                                     const char *operation) const {
    if (index < 0 || index >= static_cast<int>(events.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return events[static_cast<size_t>(index)];
}

const World3D::HitEvent &World3D::hitEventAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(hits_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return hits_[static_cast<size_t>(index)];
}

const World3D::JointStressEvent &World3D::jointStressEventAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(jointStressEvents_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return jointStressEvents_[static_cast<size_t>(index)];
}

#define EV_CONTACT_GETTER(name, events, member) \
    int World3D::name(int index) const { return contactEventAt(events, index, #name).member; }

EV_CONTACT_GETTER(getBeginContactBodyAId, beginContacts_, shapeA.bodyId)
EV_CONTACT_GETTER(getBeginContactBodyBId, beginContacts_, shapeB.bodyId)
EV_CONTACT_GETTER(getBeginContactShapeAId, beginContacts_, shapeA.shapeId)
EV_CONTACT_GETTER(getBeginContactShapeBId, beginContacts_, shapeB.shapeId)
EV_CONTACT_GETTER(getBeginContactShapeATag, beginContacts_, shapeA.shapeTag)
EV_CONTACT_GETTER(getBeginContactShapeBTag, beginContacts_, shapeB.shapeTag)
EV_CONTACT_GETTER(getEndContactBodyAId, endContacts_, shapeA.bodyId)
EV_CONTACT_GETTER(getEndContactBodyBId, endContacts_, shapeB.bodyId)
EV_CONTACT_GETTER(getEndContactShapeAId, endContacts_, shapeA.shapeId)
EV_CONTACT_GETTER(getEndContactShapeBId, endContacts_, shapeB.shapeId)
EV_CONTACT_GETTER(getEndContactShapeATag, endContacts_, shapeA.shapeTag)
EV_CONTACT_GETTER(getEndContactShapeBTag, endContacts_, shapeB.shapeTag)

#undef EV_CONTACT_GETTER

#define EV_TRIGGER_GETTER(name, events, member) \
    int World3D::name(int index) const { return triggerEventAt(events, index, #name).member; }

EV_TRIGGER_GETTER(getBeginTriggerSensorBodyId, beginTriggers_, sensor.bodyId)
EV_TRIGGER_GETTER(getBeginTriggerVisitorBodyId, beginTriggers_, visitor.bodyId)
EV_TRIGGER_GETTER(getBeginTriggerSensorShapeId, beginTriggers_, sensor.shapeId)
EV_TRIGGER_GETTER(getBeginTriggerVisitorShapeId, beginTriggers_, visitor.shapeId)
EV_TRIGGER_GETTER(getBeginTriggerSensorShapeTag, beginTriggers_, sensor.shapeTag)
EV_TRIGGER_GETTER(getBeginTriggerVisitorShapeTag, beginTriggers_, visitor.shapeTag)
EV_TRIGGER_GETTER(getEndTriggerSensorBodyId, endTriggers_, sensor.bodyId)
EV_TRIGGER_GETTER(getEndTriggerVisitorBodyId, endTriggers_, visitor.bodyId)
EV_TRIGGER_GETTER(getEndTriggerSensorShapeId, endTriggers_, sensor.shapeId)
EV_TRIGGER_GETTER(getEndTriggerVisitorShapeId, endTriggers_, visitor.shapeId)
EV_TRIGGER_GETTER(getEndTriggerSensorShapeTag, endTriggers_, sensor.shapeTag)
EV_TRIGGER_GETTER(getEndTriggerVisitorShapeTag, endTriggers_, visitor.shapeTag)

#undef EV_TRIGGER_GETTER

#define EV_HIT_INT_GETTER(name, member) \
    int World3D::name(int index) const { return hitEventAt(index, #name).member; }

EV_HIT_INT_GETTER(getHitBodyAId, shapeA.bodyId)
EV_HIT_INT_GETTER(getHitBodyBId, shapeB.bodyId)
EV_HIT_INT_GETTER(getHitShapeAId, shapeA.shapeId)
EV_HIT_INT_GETTER(getHitShapeBId, shapeB.shapeId)
EV_HIT_INT_GETTER(getHitShapeATag, shapeA.shapeTag)
EV_HIT_INT_GETTER(getHitShapeBTag, shapeB.shapeTag)

#undef EV_HIT_INT_GETTER

#define EV_HIT_FLOAT_GETTER(name, member) \
    float World3D::name(int index) const { return hitEventAt(index, #name).member; }

EV_HIT_FLOAT_GETTER(getHitPointX, pointX)
EV_HIT_FLOAT_GETTER(getHitPointY, pointY)
EV_HIT_FLOAT_GETTER(getHitPointZ, pointZ)
EV_HIT_FLOAT_GETTER(getHitNormalX, normalX)
EV_HIT_FLOAT_GETTER(getHitNormalY, normalY)
EV_HIT_FLOAT_GETTER(getHitNormalZ, normalZ)
EV_HIT_FLOAT_GETTER(getHitApproachSpeed, approachSpeed)
EV_HIT_FLOAT_GETTER(getHitNormalImpulse, normalImpulse)

#undef EV_HIT_FLOAT_GETTER

#define EV_JOINT_STRESS_INT_GETTER(name, member) \
    int World3D::name(int index) const { return jointStressEventAt(index, #name).member; }
EV_JOINT_STRESS_INT_GETTER(getJointStressJointId, jointId)
EV_JOINT_STRESS_INT_GETTER(getJointStressBodyAId, bodyAId)
EV_JOINT_STRESS_INT_GETTER(getJointStressBodyBId, bodyBId)
EV_JOINT_STRESS_INT_GETTER(getJointStressKind, kind)
#undef EV_JOINT_STRESS_INT_GETTER

#define EV_JOINT_STRESS_FLOAT_GETTER(name, member) \
    float World3D::name(int index) const { return jointStressEventAt(index, #name).member; }
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceX, forceX)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceY, forceY)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressForceZ, forceZ)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueX, torqueX)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueY, torqueY)
EV_JOINT_STRESS_FLOAT_GETTER(getJointStressTorqueZ, torqueZ)
#undef EV_JOINT_STRESS_FLOAT_GETTER

const World3D::ContactPointResult &World3D::contactPointAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(contactPoints_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return contactPoints_[static_cast<size_t>(index)];
}

#define EV_CONTACT_POINT_INT_GETTER(name, member) \
    int World3D::name(int index) const { return contactPointAt(index, #name).member; }

EV_CONTACT_POINT_INT_GETTER(getContactPointShapeId, shapeId)
EV_CONTACT_POINT_INT_GETTER(getContactPointShapeTag, shapeTag)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherBodyId, otherBodyId)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherShapeId, otherShapeId)
EV_CONTACT_POINT_INT_GETTER(getContactPointOtherShapeTag, otherShapeTag)

#undef EV_CONTACT_POINT_INT_GETTER

#define EV_CONTACT_POINT_FLOAT_GETTER(name, member) \
    float World3D::name(int index) const { return contactPointAt(index, #name).member; }

EV_CONTACT_POINT_FLOAT_GETTER(getContactPointX, x)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointY, y)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointZ, z)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalX, normalX)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalY, normalY)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalZ, normalZ)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointSeparation, separation)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalImpulse, normalImpulse)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointTotalNormalImpulse, totalNormalImpulse)
EV_CONTACT_POINT_FLOAT_GETTER(getContactPointNormalVelocity, normalVelocity)

#undef EV_CONTACT_POINT_FLOAT_GETTER

bool World3D::isContactPointPersisted(int index) const {
    return contactPointAt(index, "isContactPointPersisted").persisted;
}

}  // namespace eve::physics
