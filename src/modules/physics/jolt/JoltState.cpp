// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/RegisterTypes.h>
#include <cstdarg>
#include <cstdio>
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltJoint.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
namespace {
void trace(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[Jolt] ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}
struct Runtime {
    Runtime() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &trace;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line) {
            std::fprintf(stderr, "[Jolt] assertion %s at %s:%u: %s\n", expression, file, line, message ? message : "");
            return true;
        };
#endif
        JPH::Factory::sInstance = new JPH::Factory;
        JPH::RegisterTypes();
    }
    ~Runtime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
float combine(float a, float b, uint64_t policyA, uint64_t policyB, int shift, bool friction) {
    switch (std::max((policyA >> shift) & 7, (policyB >> shift) & 7)) {
        case 1: return .5f * (a + b);
        case 2: return std::min(a, b);
        case 3: return a * b;
        case 4: return std::max(a, b);
        default: return friction ? std::sqrt(a * b) : std::max(a, b);
    }
}
void configure(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings& settings) {
    auto* sa = child(a, m.mSubShapeID1);
    auto* sb = child(b, m.mSubShapeID2);
    if (!sa || !sb) return;
    const auto ma       = contactMaterial(a, m.mSubShapeID1, *sa);
    const auto mb       = contactMaterial(b, m.mSubShapeID2, *sb);
    auto       velocity = [](const JPH::Body& body, const ShapeState& shape, const SurfaceMaterial3D& material) {
        const auto& g = shape.geometry;
        return body.GetRotation() *
               (JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw).Normalized() *
                JPH::Vec3(material.tangentVelocity.x, material.tangentVelocity.y, material.tangentVelocity.z));
    };
    settings.mRelativeLinearSurfaceVelocity = velocity(b, *sb, mb) - velocity(a, *sa, ma);
    const auto point                        = m.GetWorldSpaceContactPointOn1(0);
    const auto normal                       = m.mWorldSpaceNormal;
    settings.mIsSensor                      = sa->sensor || sb->sensor || !sa->owner || !sb->owner ||
                         !WorldBackend::isContactAllowed(*sa->owner, *sb->owner, point.GetX(), point.GetY(),
                                                         point.GetZ(), normal.GetX(), normal.GetY(), normal.GetZ());
    settings.mCombinedFriction = combine(ma.friction, mb.friction, ma.userMaterialId, mb.userMaterialId, 32, true);
    settings.mCombinedRestitution =
        combine(ma.restitution, mb.restitution, ma.userMaterialId, mb.userMaterialId, 35, false);
    const auto radius = [](const ShapeState& shape) {
        if (shape.geometry.kind == ShapeKind3D::Sphere) return shape.geometry.a;
        if (shape.geometry.kind == ShapeKind3D::Capsule) return shape.geometry.b;
        if (shape.geometry.kind == ShapeKind3D::Box || shape.geometry.kind == ShapeKind3D::ConvexHull)
            return .25f * shape.native->GetInnerRadius();
        return 0.f;
    };
    settings.mCombinedRollingResistance =
        std::max(ma.rollingResistance, mb.rollingResistance) * std::max(radius(*sa), radius(*sb));
}
}  // namespace
void initializeRuntime() { static Runtime runtime; }
State::State(float x, float y, float z, bool allowSleep) : sleep(allowSleep) {
    system.Init(16384, 0, 65536, 16384, layers, layers, layers);
    system.SetGravity({x, y, z});
    listener        = std::make_unique<Listener>();
    listener->state = this;
    system.SetContactListener(listener.get());
    system.AddStepListener(listener.get());
}
State::~State() {
    system.RemoveStepListener(listener.get());
    system.SetContactListener(nullptr);
}
void Listener::OnStep(const JPH::PhysicsStepListenerContext&) {
    ++substep;
    // Jolt owns all body/constraint locks here. Never call the locking BodyInterface.
    for (const auto& weak : state->joints)
        if (auto joint = weak.lock(); joint && joint->alive && joint->native &&
                                      (joint->kind == JointKind3D::Motor || joint->kind == JointKind3D::Spherical)) {
            auto& constraint = joint->velocity ? *joint->velocity : static_cast<JPH::SixDOFConstraint&>(*joint->native);
            const auto frame =
                constraint.GetBody1()->GetCenterOfMassTransform() * constraint.GetConstraintToBody1Matrix();
            constraint.SetTargetAngularVelocityCS(frame.Multiply3x3Transposed(joint->worldAngularVelocity));
            if (joint->kind == JointKind3D::Motor)
                constraint.SetTargetVelocityCS(frame.Multiply3x3Transposed(joint->worldLinearVelocity));
        }
}
JPH::ValidateResult Listener::OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg,
                                                const JPH::CollideShapeResult& result) {
    if (state->suppressedPairs.contains(pairKey(a.GetID(), b.GetID())))
        return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    const auto* sa = child(a, result.mSubShapeID1);
    const auto* sb = child(b, result.mSubShapeID2);
    if (!sa || !sb || !sa->owner || !sb->owner || !WorldBackend::isPairAllowed(*sa->owner, *sb->owner))
        return JPH::ValidateResult::RejectContact;
    const auto& fa     = sa->filter;
    const auto& fb     = sb->filter;
    bool        accept = fa.groupIndex && fa.groupIndex == fb.groupIndex
                             ? fa.groupIndex > 0
                             : (fa.maskBits & fb.categoryBits) && (fb.maskBits & fa.categoryBits);
    return accept ? JPH::ValidateResult::AcceptContact : JPH::ValidateResult::RejectContact;
}
void Listener::OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                              JPH::ContactSettings& settings) {
    configure(a, b, m, settings);
    auto* sa = child(a, m.mSubShapeID1);
    auto* sb = child(b, m.mSubShapeID2);
    if (!sa || !sb || !sa->owner || !sb->owner) return;
    auto ba = sa->body.lock();
    auto bb = sb->body.lock();
    if (!ba || !bb || !ba->owner || !bb->owner || (settings.mIsSensor && !sa->sensor && !sb->sensor)) return;
    ContactPair pair{ba->owner->getId(), sa->owner->getId(),  sa->owner->getTag(), bb->owner->getId(),
                     sb->owner->getId(), sb->owner->getTag(), sa->sensor,          sb->sensor};
    contacts.insert_or_assign(JPH::SubShapeIDPair(a.GetID(), m.mSubShapeID1, b.GetID(), m.mSubShapeID2), pair);
    begins.push_back(pair);
}
void Listener::OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                                  JPH::ContactSettings& settings) {
    const JPH::SubShapeIDPair key(a.GetID(), m.mSubShapeID1, b.GetID(), m.mSubShapeID2);
    configure(a, b, m, settings);
    const auto* sa = child(a, m.mSubShapeID1);
    const auto* sb = child(b, m.mSubShapeID2);
    if (settings.mIsSensor && sa && sb && !sa->sensor && !sb->sensor) {
        OnContactRemoved(key);
    } else if (!contacts.contains(key)) {
        OnContactAdded(a, b, m, settings);
    }
}
void Listener::OnContactRemoved(const JPH::SubShapeIDPair& key) {
    auto i = contacts.find(key);
    if (i != contacts.end()) {
        ends.push_back(i->second);
        contacts.erase(i);
    }
}
}  // namespace eve::physics::jolt
