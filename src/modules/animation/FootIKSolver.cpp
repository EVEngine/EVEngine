#include "animation/FootIKSolver.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"
#include <algorithm>
#include <cmath>

namespace eve::animation {
namespace {
float clamp01(float v) { return std::clamp(v,0.f,1.f); }
float blendRate(float response,float dt) {
    return response<=0.f?1.f:1.f-std::exp(-response*std::max(dt,0.f));
}
}
FootIKSolver::FootIKSolver(AnimSkeleton* skeleton):skeleton_(skeleton) {
    if(!skeleton_) throw Exception("FootIKSolver: skeleton is null");
}
FootIKSolver::~FootIKSolver()=default;
void FootIKSolver::setSkeleton(AnimSkeleton* skeleton) {
    if(skeleton_==skeleton) return;
    skeleton_=skeleton; pelvisBone_=-1; left_={}; right_={};
}
void FootIKSolver::setPelvisBone(int bone) {
    if(!skeleton_||bone<0||bone>=skeleton_->getBoneCount())
        throw Exception("FootIKSolver.setPelvisBone: invalid bone");
    pelvisBone_=bone;
}
void FootIKSolver::configure(Leg& leg,int hip,int knee,int foot,float soleOffset) {
    if(!skeleton_||hip<0||knee<0||foot<0||hip>=skeleton_->getBoneCount()||
       knee>=skeleton_->getBoneCount()||foot>=skeleton_->getBoneCount()||
       skeleton_->getParent(knee)!=hip||skeleton_->getParent(foot)!=knee)
        throw Exception("FootIKSolver.configure: invalid leg hierarchy");
    leg={}; leg.hip=hip; leg.knee=knee; leg.foot=foot;
    leg.soleOffset=soleOffset; leg.configured=true;
}
void FootIKSolver::configureLeftLeg(int h,int k,int f,float o){configure(left_,h,k,f,o);}
void FootIKSolver::configureRightLeg(int h,int k,int f,float o){configure(right_,h,k,f,o);}
void FootIKSolver::configureToe(Leg& leg,int toe,float soleOffset) {
    if(!leg.configured||!skeleton_||toe<0||toe>=skeleton_->getBoneCount()||skeleton_->getParent(toe)!=leg.foot)
        throw Exception("FootIKSolver.configureToe: invalid toe hierarchy");
    leg.toe=toe; leg.toeSoleOffset=soleOffset; leg.toeConfigured=true; leg.toeInitialized=false;
}
void FootIKSolver::configureLeftToe(int toe,float offset){configureToe(left_,toe,offset);}
void FootIKSolver::configureRightToe(int toe,float offset){configureToe(right_,toe,offset);}
void FootIKSolver::setGroundQuery(float startHeight,float distance){groundStartHeight_=std::max(startHeight,0.f);groundQueryDistance_=std::max(distance,0.f);}
void FootIKSolver::contact(Leg& leg,bool hit,float x,float y,float z,float nx,float ny,float nz,float weight) {
    const float n=std::sqrt(nx*nx+ny*ny+nz*nz);
    if(hit&&n<1e-6f) throw Exception("FootIKSolver.contact: ground normal is zero");
    leg.target.x=x; leg.target.y=y; leg.target.z=z;
    leg.target.weight=hit&&ny/n>=minGroundNormalY_?clamp01(weight):0.f;
    if(n>=1e-6f){leg.target.nx=nx/n;leg.target.ny=ny/n;leg.target.nz=nz/n;}
}
void FootIKSolver::setLeftContact(bool h,float x,float y,float z,float nx,float ny,float nz,float w){
    contact(left_,h,x,y,z,nx,ny,nz,w);
}
void FootIKSolver::setRightContact(bool h,float x,float y,float z,float nx,float ny,float nz,float w){
    contact(right_,h,x,y,z,nx,ny,nz,w);
}
void FootIKSolver::setMaxPelvisOffset(float v){maxPelvisOffset_=std::max(v,0.f);}
void FootIKSolver::setMinGroundNormalY(float v){minGroundNormalY_=clamp01(v);}
void FootIKSolver::setPositionResponse(float v){positionResponse_=std::max(v,0.f);}
void FootIKSolver::setRotationResponse(float v){rotationResponse_=std::max(v,0.f);}
void FootIKSolver::setContactGraceTime(float v){contactGraceTime_=std::max(v,0.f);}
void FootIKSolver::setFootLockEnabled(bool enabled){footLockEnabled_=enabled;if(!enabled){left_.locked=false;right_.locked=false;}}
void FootIKSolver::setFootLockThresholds(float enter,float exit){lockEnterWeight_=clamp01(enter);lockExitWeight_=std::min(clamp01(exit),lockEnterWeight_);}
void FootIKSolver::reset(){left_.initialized=right_.initialized=false;left_.toeInitialized=right_.toeInitialized=false;left_.locked=right_.locked=false;left_.missingTime=right_.missingTime=0.f;}
void FootIKSolver::updateLock(Leg& leg) {
    if(!footLockEnabled_){leg.locked=false;return;}
    if(leg.locked&&leg.target.weight<=lockExitWeight_)leg.locked=false;
    if(!leg.locked&&leg.target.weight>=lockEnterWeight_){leg.locked=true;leg.lockedContact=leg.target;}
    if(leg.locked)leg.target=leg.lockedContact;
}
void FootIKSolver::queryGround(Leg& leg,const AnimPose& pose,float dt) {
    if(!groundProvider_||!leg.configured)return;
    const auto& foot=pose.world(leg.foot);float x=0.f,y=0.f,z=0.f,nx=0.f,ny=1.f,nz=0.f;
    const auto status=groundProvider_->queryGround(foot.px,foot.py+groundStartHeight_,foot.pz,groundStartHeight_+groundQueryDistance_,x,y,z,nx,ny,nz);
    if(status==FootIKGroundQueryStatus::Hit){leg.missingTime=0.f;contact(leg,true,x,y,z,nx,ny,nz,1.f);}
    else if(status==FootIKGroundQueryStatus::NoHit){leg.missingTime+=std::max(dt,0.f);if(leg.missingTime>contactGraceTime_)contact(leg,false,0.f,0.f,0.f,0.f,1.f,0.f,0.f);}
    if(leg.toeConfigured&&status==FootIKGroundQueryStatus::Hit){
        const auto& toe=pose.world(leg.toe);
        const auto toeStatus=groundProvider_->queryGround(toe.px,toe.py+groundStartHeight_,toe.pz,groundStartHeight_+groundQueryDistance_,x,y,z,nx,ny,nz);
        if(toeStatus==FootIKGroundQueryStatus::Hit){const float n=std::sqrt(nx*nx+ny*ny+nz*nz);if(n>1e-6f){leg.toeTarget={x,y,z,nx/n,ny/n,nz/n,ny/n>=minGroundNormalY_?1.f:0.f};}}
        else leg.toeTarget.weight=0.f;
    }
    updateLock(leg);
}
void FootIKSolver::interpolate(Leg& leg,float dt) {
    if(!leg.configured)return;
    if(!leg.initialized){leg.smooth=leg.target;leg.initialized=true;return;}
    const float p=blendRate(positionResponse_,dt),r=blendRate(rotationResponse_,dt);
    leg.smooth.x+=(leg.target.x-leg.smooth.x)*p; leg.smooth.y+=(leg.target.y-leg.smooth.y)*p;
    leg.smooth.z+=(leg.target.z-leg.smooth.z)*p; leg.smooth.weight+=(leg.target.weight-leg.smooth.weight)*p;
    leg.smooth.nx+=(leg.target.nx-leg.smooth.nx)*r; leg.smooth.ny+=(leg.target.ny-leg.smooth.ny)*r;
    leg.smooth.nz+=(leg.target.nz-leg.smooth.nz)*r;
    const float n=std::sqrt(leg.smooth.nx*leg.smooth.nx+leg.smooth.ny*leg.smooth.ny+leg.smooth.nz*leg.smooth.nz);
    if(n>1e-6f){leg.smooth.nx/=n;leg.smooth.ny/=n;leg.smooth.nz/=n;}
}
float FootIKSolver::pelvisOffset(const Leg& leg,const AnimPose& pose) const {
    if(!leg.configured||leg.smooth.weight<=0.f)return 0.f;
    return std::min(0.f,(leg.smooth.y+leg.soleOffset-pose.world(leg.foot).py)*leg.smooth.weight);
}
void FootIKSolver::solve(const Leg& leg,AnimPose& pose,float rotationWeight) const {
    if(!leg.configured||leg.smooth.weight<=1e-4f)return;
    pose.solveTwoBoneIK(skeleton_,leg.hip,leg.knee,leg.foot,leg.smooth.x,
                        leg.smooth.y+leg.soleOffset,leg.smooth.z,leg.smooth.weight);
    pose.computeWorld(skeleton_);
    const auto& foot=pose.world(leg.foot);
    pose.aimBone(skeleton_,leg.foot,foot.px+leg.smooth.nx,foot.py+leg.smooth.ny,
                 foot.pz+leg.smooth.nz,rotationWeight*leg.smooth.weight);
    if(leg.toeConfigured&&leg.toeSmooth.weight>1e-4f){pose.computeWorld(skeleton_);pose.aimBone(skeleton_,leg.toe,leg.toeSmooth.x,leg.toeSmooth.y+leg.toeSoleOffset,leg.toeSmooth.z,rotationWeight*leg.toeSmooth.weight);}
}
void FootIKSolver::apply(AnimPose* pose,float dt) {
    if(!pose||!skeleton_)throw Exception("FootIKSolver.apply: pose or skeleton is null");
    if(!std::isfinite(dt)||dt<0.f)throw Exception("FootIKSolver.apply: delta time is invalid");
    if(pose->getBoneCount()!=skeleton_->getBoneCount())throw Exception("FootIKSolver.apply: bone count mismatch");
    pose->computeWorld(skeleton_); queryGround(left_,*pose,dt); queryGround(right_,*pose,dt);
    interpolate(left_,dt); interpolate(right_,dt);
    auto interpolateToe=[&](Leg& leg){if(!leg.toeConfigured)return;if(!leg.toeInitialized){leg.toeSmooth=leg.toeTarget;leg.toeInitialized=true;return;}const float p=blendRate(positionResponse_,dt);leg.toeSmooth.x+=(leg.toeTarget.x-leg.toeSmooth.x)*p;leg.toeSmooth.y+=(leg.toeTarget.y-leg.toeSmooth.y)*p;leg.toeSmooth.z+=(leg.toeTarget.z-leg.toeSmooth.z)*p;leg.toeSmooth.weight+=(leg.toeTarget.weight-leg.toeSmooth.weight)*p;};
    interpolateToe(left_);interpolateToe(right_);
    if(pelvisBone_>=0){
        const float offset=std::clamp(std::min(pelvisOffset(left_,*pose),pelvisOffset(right_,*pose)),
                                      -maxPelvisOffset_,maxPelvisOffset_);
        pose->local(pelvisBone_).py+=offset; pose->computeWorld(skeleton_);
    }
    const float rotationWeight=blendRate(rotationResponse_,dt);
    solve(left_,*pose,rotationWeight); solve(right_,*pose,rotationWeight);
    pose->computeWorld(skeleton_);
}
FootIKDebugSnapshot FootIKSolver::debugSnapshot() const {
    auto capture=[](const Leg& leg){FootIKDebugFoot out;out.configured=leg.configured;out.contact=leg.smooth.weight>1e-4f;out.locked=leg.locked;out.toeConfigured=leg.toeConfigured;out.x=leg.smooth.x;out.y=leg.smooth.y;out.z=leg.smooth.z;out.nx=leg.smooth.nx;out.ny=leg.smooth.ny;out.nz=leg.smooth.nz;out.toeX=leg.toeSmooth.x;out.toeY=leg.toeSmooth.y;out.toeZ=leg.toeSmooth.z;return out;};
    return {capture(left_),capture(right_)};
}
}  // namespace eve::animation
