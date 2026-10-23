#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/box3d/Box3DWorld.h"
#include "physics/box3d/Box3DWorldInternal.h"
namespace eve::physics::box3d {
using namespace world3d_detail;

World3D::EventShape WorldBackend::do_eventShapeFrom(b3ShapeId shapeId) const {
    auto found = owner_->shapeRecords_.find(b3StoreShapeId(shapeId));
    if (found != owner_->shapeRecords_.end()) return found->second;
    Shape3D *shape = shapeFromShape(shapeId);
    Body3D  *body  = bodyFromShape(shapeId);
    if (!shape || !body) return {};
    return EventShape{body->getId(), shape->getId(), shape->getTag()};
}

eve::Result<BodyContacts3D> WorldBackend::do_queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const {
    if (maxPoints < 1 || maxPoints > 4096)
        return eve::Result<BodyContacts3D>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "maxPoints must be in [1,4096]", "physics.contacts.maxPoints"));
    const auto *target = owner_->findBody(body);
    if (!target)
        return eve::Result<BodyContacts3D>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Body does not belong to this live world", "physics.contacts.body"));
    BodyContacts3D result;
    result.body                   = body;
    auto         &points          = result.points;
    constexpr int maxContactPairs = 4096;
    const int     availablePairs  = b3Body_GetContactCapacity(box3d::Access::bodyId(*target));
    const int     capacity        = std::min(availablePairs, maxContactPairs + 1);
    if (capacity <= 0) return eve::Result<BodyContacts3D>::success(std::move(result));
    std::vector<b3ContactData> contacts(static_cast<size_t>(capacity));
    const int contactCount = b3Body_GetContactData(box3d::Access::bodyId(*target), contacts.data(), capacity);
    if (contactCount > maxContactPairs) result.coverage = ContactQueryCoverage3D::Truncated;
    points.reserve(static_cast<size_t>(std::min(maxPoints, contactCount * 2)));

    for (int contactIndex = 0; contactIndex < std::min(contactCount, maxContactPairs); ++contactIndex) {
        const b3ContactData &contact = contacts[static_cast<size_t>(contactIndex)];
        Shape3D             *shapeA  = shapeFromShape(contact.shapeIdA);
        Shape3D             *shapeB  = shapeFromShape(contact.shapeIdB);
        Body3D              *bodyA   = bodyFromShape(contact.shapeIdA);
        Body3D              *bodyB   = bodyFromShape(contact.shapeIdB);
        if (!shapeA || !shapeB || !bodyA || !bodyB) {
            result.coverage = ContactQueryCoverage3D::Truncated;
            continue;
        }
        const bool targetIsA = bodyA == target;
        if (!targetIsA && bodyB != target) continue;
        Shape3D    *ownShape     = targetIsA ? shapeA : shapeB;
        Shape3D    *otherShape   = targetIsA ? shapeB : shapeA;
        Body3D     *otherBody    = targetIsA ? bodyB : bodyA;
        const b3Pos centerOfMass = b3Body_GetWorldCenterOfMass(box3d::Access::bodyId(*target));

        for (int manifoldIndex = 0; manifoldIndex < contact.manifoldCount; ++manifoldIndex) {
            const b3Manifold &manifold = contact.manifolds[manifoldIndex];
            const b3Vec3      normal   = targetIsA ? manifold.normal : -manifold.normal;
            for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex) {
                if (static_cast<int>(points.size()) >= maxContactPairs) {
                    result.coverage = ContactQueryCoverage3D::Truncated;
                    break;
                }
                const b3ManifoldPoint &point = manifold.points[pointIndex];
                const b3Pos worldPoint       = b3OffsetPos(centerOfMass, targetIsA ? point.anchorA : point.anchorB);
                points.push_back({ownShape->getId(), ownShape->getTag(), otherBody->getId(), otherShape->getId(),
                                  otherShape->getTag(), point.featureId, static_cast<float>(worldPoint.x),
                                  static_cast<float>(worldPoint.y), static_cast<float>(worldPoint.z), normal.x,
                                  normal.y, normal.z, point.separation, point.normalImpulse, point.totalNormalImpulse,
                                  point.normalVelocity, point.persisted});
            }
        }
    }

    std::sort(points.begin(), points.end(), [](const ContactPointResult &a, const ContactPointResult &b) {
        if (a.shapeId != b.shapeId) return a.shapeId < b.shapeId;
        if (a.otherShapeId != b.otherShapeId) return a.otherShapeId < b.otherShapeId;
        if (a.featureId != b.featureId) return a.featureId < b.featureId;
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    if (static_cast<int>(points.size()) > maxPoints) {
        points.resize(static_cast<size_t>(maxPoints));
        result.coverage = ContactQueryCoverage3D::Truncated;
    }
    return eve::Result<BodyContacts3D>::success(std::move(result));
}

eve::Result<BodyContacts3D> WorldBackend::queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const {
    return do_queryBodyContactsOwned(body, maxPoints);
}

eve::Result<void> WorldBackend::collectEvents() {
    if (!isValid())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "World is no longer valid"));


    b3ContactEvents contacts = b3World_GetContactEvents(worldId_);
    for (int i = 0; i < contacts.beginCount; ++i) {
        EventShape a = do_eventShapeFrom(contacts.beginEvents[i].shapeIdA);
        EventShape b = do_eventShapeFrom(contacts.beginEvents[i].shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;
        if (a.shapeId > b.shapeId) std::swap(a, b);
        owner_->beginContacts_.push_back({a, b});
    }
    for (int i = 0; i < contacts.endCount; ++i) {
        EventShape a = do_eventShapeFrom(contacts.endEvents[i].shapeIdA);
        EventShape b = do_eventShapeFrom(contacts.endEvents[i].shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;
        if (a.shapeId > b.shapeId) std::swap(a, b);
        owner_->endContacts_.push_back({a, b});
    }

    for (int i = 0; i < contacts.hitCount; ++i) {
        const b3ContactHitEvent &source = contacts.hitEvents[i];
        EventShape               a      = do_eventShapeFrom(source.shapeIdA);
        EventShape               b      = do_eventShapeFrom(source.shapeIdB);
        if (a.shapeId == 0 || b.shapeId == 0) continue;

        float normalImpulse = 0.f;
        if (b3Contact_IsValid(source.contactId)) {
            const b3ContactData data = b3Contact_GetData(source.contactId);
            for (int manifoldIndex = 0; manifoldIndex < data.manifoldCount; ++manifoldIndex) {
                const b3Manifold &manifold = data.manifolds[manifoldIndex];
                for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex)
                    normalImpulse += manifold.points[pointIndex].totalNormalImpulse;
            }
        }

        b3Vec3 normal = source.normal;
        if (a.shapeId > b.shapeId) {
            std::swap(a, b);
            normal = -normal;
        }
        owner_->hits_.push_back({a, b, static_cast<float>(source.point.x), static_cast<float>(source.point.y),
                                 static_cast<float>(source.point.z), normal.x, normal.y, normal.z, source.approachSpeed,
                                 normalImpulse});
    }

    b3SensorEvents sensors = b3World_GetSensorEvents(worldId_);
    for (int i = 0; i < sensors.beginCount; ++i) {
        EventShape sensor  = do_eventShapeFrom(sensors.beginEvents[i].sensorShapeId);
        EventShape visitor = do_eventShapeFrom(sensors.beginEvents[i].visitorShapeId);
        if (sensor.shapeId == 0 || visitor.shapeId == 0) continue;
        owner_->beginTriggers_.push_back({sensor, visitor});
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        EventShape sensor  = do_eventShapeFrom(sensors.endEvents[i].sensorShapeId);
        EventShape visitor = do_eventShapeFrom(sensors.endEvents[i].visitorShapeId);
        if (sensor.shapeId == 0 || visitor.shapeId == 0) continue;
        owner_->endTriggers_.push_back({sensor, visitor});
    }

    const b3JointEvents jointEvents = b3World_GetJointEvents(worldId_);
    for (int i = 0; i < jointEvents.count; ++i) {
        const b3JointEvent &source = jointEvents.jointEvents[i];
        auto               *joint  = static_cast<Joint3D *>(source.userData);
        if (!joint || owner_->joints_.find(joint) == owner_->joints_.end() || !joint->isValid() ||
            !b3Joint_IsValid(source.jointId) || b3Joint_GetUserData(source.jointId) != joint)
            continue;
        const b3Vec3 force  = b3Joint_GetConstraintForce(source.jointId);
        const b3Vec3 torque = b3Joint_GetConstraintTorque(source.jointId);
        owner_->jointStressEvents_.push_back({joint->getId(), joint->getBodyAId(), joint->getBodyBId(),
                                              static_cast<int>(joint->kind_), force.x, force.y, force.z, torque.x,
                                              torque.y, torque.z});
    }

    // Invalid handles are retained only until the step that can report their end event.
    // This keeps destruction-safe metadata without allowing the history map to grow forever.
    for (auto it = owner_->shapeRecords_.begin(); it != owner_->shapeRecords_.end();) {
        if (!b3Shape_IsValid(b3LoadShapeId(it->first)))
            it = owner_->shapeRecords_.erase(it);
        else
            ++it;
    }
    for (auto it = owner_->shapeRawHandles_.begin(); it != owner_->shapeRawHandles_.end();) {
        if (!b3Shape_IsValid(b3LoadShapeId(it->first)))
            it = owner_->shapeRawHandles_.erase(it);
        else
            ++it;
    }
    for (auto it = owner_->shapeHandles_.begin(); it != owner_->shapeHandles_.end();) {
        if (!it->second || !it->second->isValid())
            it = owner_->shapeHandles_.erase(it);
        else
            ++it;
    }

    return eve::Result<void>::success();
}

}  // namespace eve::physics::box3d
