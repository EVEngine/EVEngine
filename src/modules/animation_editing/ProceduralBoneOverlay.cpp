#include "animation_editing/ProceduralBoneOverlay.h"

#include "animation/DynamicBoneSolver.h"
#include "animation/FootIKSolver.h"

#include <cmath>
#include <map>

namespace eve::animation_editing {
editing::GizmoSnapshot ProceduralBoneOverlayBuilder::build(std::string target,editing::Revision revision,const animation::DynamicBoneSolver* dynamicSolver,const animation::FootIKSolver* footSolver,const ProceduralBoneOverlayOptions& options) const {
    editing::GizmoSnapshot result;result.target=std::move(target);result.targetRevision=revision;
    if(result.target.empty()||options.maximumPrimitives==0||(!dynamicSolver&&!footSolver)){result.diagnostics.push_back(editing::ruleDiagnostic(DiagnosticCode::PreconditionViolation,editing::RuleId("editor.procedural-bone.overlay-input"),editing::DiagnosticSeverity::Error,"Procedural bone overlay requires a target, solver and non-zero budget"));return result;}
    auto append=[&](editing::GizmoPrimitive primitive){if(result.primitives.size()<options.maximumPrimitives)result.primitives.push_back(std::move(primitive));};
    if(dynamicSolver){const auto snapshot=dynamicSolver->debugSnapshot();if(options.showParticles){std::map<int,animation::DynamicBoneDebugParticle> previous;for(const auto& p:snapshot.particles){const std::array<double,4> color=p.sleeping?std::array<double,4>{0.45,0.45,0.45,1.0}:std::array<double,4>{0.2,0.85,1.0,1.0};append({"dynamic-chain-"+std::to_string(p.chain)+"-particle-"+std::to_string(p.particle),"dynamic-bone-particle",{p.x,p.y,p.z},{},{},color,std::max<double>(p.radius,0.015),0.0,p.sleeping});if(auto found=previous.find(p.chain);found!=previous.end()){const auto& a=found->second;const double dx=p.x-a.x,dy=p.y-a.y,dz=p.z-a.z,length=std::sqrt(dx*dx+dy*dy+dz*dz);if(length>1e-9)append({"dynamic-chain-"+std::to_string(p.chain)+"-segment-"+std::to_string(p.particle),"dynamic-bone-segment",{a.x,a.y,a.z},{},{dx/length,dy/length,dz/length},color,0.0,length,p.sleeping});}previous[p.chain]=p;}}
        if(options.showColliders)for(size_t i=0;i<snapshot.colliders.size();++i){const auto& c=snapshot.colliders[i];const std::array<double,4> color=!c.enabled?std::array<double,4>{0.4,0.4,0.4,0.7}:c.inside?std::array<double,4>{0.3,1.0,0.45,1.0}:std::array<double,4>{1.0,0.45,0.15,1.0};double dx=c.endX-c.startX,dy=c.endY-c.startY,dz=c.endZ-c.startZ,length=std::sqrt(dx*dx+dy*dy+dz*dz);append({"dynamic-collider-"+std::to_string(i),c.capsule?"dynamic-bone-capsule":"dynamic-bone-sphere",{c.startX,c.startY,c.startZ},{},length>1e-9?std::array<double,3>{dx/length,dy/length,dz/length}:std::array<double,3>{0.0,1.0,0.0},color,c.radius,c.capsule?length:0.0,!c.enabled});}}
    if(footSolver&&options.showFootIK){const auto snapshot=footSolver->debugSnapshot();auto foot=[&](const char* name,const animation::FootIKDebugFoot& f){if(!f.configured||!f.contact)return;const std::array<double,4> color=f.locked?std::array<double,4>{1.0,0.78,0.1,1.0}:std::array<double,4>{0.25,1.0,0.55,1.0};append({std::string("foot-ik-")+name+"-contact","foot-ik-contact",{f.x,f.y,f.z},{},{},color,0.035,0.0,false});append({std::string("foot-ik-")+name+"-normal","foot-ik-normal",{f.x,f.y,f.z},{},{f.nx,f.ny,f.nz},color,0.0,0.25,false});if(f.toeConfigured)append({std::string("foot-ik-")+name+"-toe","foot-ik-toe",{f.toeX,f.toeY,f.toeZ},{},{},color,0.025,0.0,false});};foot("left",snapshot.left);foot("right",snapshot.right);}
    if(result.primitives.size()>=options.maximumPrimitives)result.diagnostics.push_back(editing::ruleDiagnostic(DiagnosticCode::PreconditionViolation,editing::RuleId("editor.procedural-bone.overlay-budget"),editing::DiagnosticSeverity::Warning,"Procedural bone overlay reached its primitive budget"));result.status=editing::Status::Applied;return result;
}
}
