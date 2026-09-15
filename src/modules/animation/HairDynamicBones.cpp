#include "animation/HairDynamicBones.h"

#include "animation/AnimSkeleton.h"
#include "animation/DynamicBoneSolver.h"
#include "common/Diagnostic.h"

namespace eve::animation::hair {
namespace {

Result<int> invalidInt(std::string message, std::string path) {
    return Result<int>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalidVoid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

}  // namespace

Result<int> addChain(DynamicBoneSolver &solver, const ChainDesc &desc) {
    if (desc.rootBone.empty() || desc.tipBone.empty())
        return invalidInt("hair chain requires rootBone and tipBone", "hair.chain");
    if (desc.stiffness < 0.f || desc.stiffness > 1.f || desc.damping < 0.f || desc.damping > 1.f ||
        desc.inertia < 0.f || desc.inertia > 1.f || desc.radius < 0.f || desc.iterations < 1)
        return invalidInt("hair chain parameters out of range", "hair.chain");

    const int chain =
        solver.addChainByName(desc.rootBone, desc.tipBone, desc.stiffness, desc.damping,
                              desc.inertia, desc.gravityScale, desc.radius, desc.iterations);
    if (chain < 0)
        return invalidInt("hair chain bone path cannot be resolved",
                          desc.rootBone + "->" + desc.tipBone);

    solver.setChainSelfCollision(chain, desc.selfCollision);
    if (desc.endLength > 0.f) solver.setChainEndLength(chain, desc.endLength);
    return Result<int>::success(chain);
}

Result<void> setupChains(DynamicBoneSolver &solver, const std::vector<ChainDesc> &chains,
                         bool clearFirst) {
    if (clearFirst) solver.clearChains();
    for (size_t i = 0; i < chains.size(); ++i) {
        auto added = addChain(solver, chains[i]);
        if (!added) {
            const Diagnostic *diag = added.status().primaryDiagnostic();
            return invalidVoid(diag ? std::string(diag->message()) : added.status().describe(),
                               "hair.chains[" + std::to_string(i) + "]");
        }
    }
    return Result<void>::success();
}

Result<int> addHeadCollider(DynamicBoneSolver &solver, const std::string &boneName, float radius,
                            float offsetX, float offsetY, float offsetZ) {
    if (boneName.empty()) return invalidInt("head collider bone name is empty", "hair.headBone");
    if (!(radius > 0.f))
        return invalidInt("head collider radius must be positive", "hair.headRadius");
    AnimSkeleton *skeleton = solver.getSkeleton();
    if (!skeleton) return invalidInt("dynamic bone solver has no skeleton", "hair.skeleton");
    const int bone = skeleton->findBone(boneName);
    if (bone < 0) return invalidInt("head collider bone not found", boneName);
    solver.addBoneColliderSphere(bone, offsetX, offsetY, offsetZ, radius);
    return Result<int>::success(solver.getColliderCount());
}

}  // namespace eve::animation::hair
