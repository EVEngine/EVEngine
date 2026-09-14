#include "animation/DynamicBoneSolver.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {
namespace {
constexpr float kEpsilon = 1e-6f;
float clamp01(float value) { return std::clamp(value, 0.f, 1.f); }
float length(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }
struct Quat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };
Quat multiply(const Quat &a, const Quat &b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quat inverse(const Quat &q) { return {-q.x, -q.y, -q.z, q.w}; }
Quat normalized(Quat q) {
    const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n <= kEpsilon) return {};
    q.x/=n; q.y/=n; q.z/=n; q.w/=n; return q;
}
Quat fromTo(float ax, float ay, float az, float bx, float by, float bz) {
    const float al=length(ax,ay,az), bl=length(bx,by,bz);
    if (al<=kEpsilon || bl<=kEpsilon) return {};
    ax/=al; ay/=al; az/=al; bx/=bl; by/=bl; bz/=bl;
    const float dot=std::clamp(ax*bx+ay*by+az*bz,-1.f,1.f);
    if (dot < -0.9999f) {
        const float ox=std::fabs(ax)<0.8f?1.f:0.f, oy=std::fabs(ax)<0.8f?0.f:1.f;
        return normalized({-az*oy, az*ox, ax*oy-ay*ox, 0.f});
    }
    return normalized({ay*bz-az*by, az*bx-ax*bz, ax*by-ay*bx, 1.f+dot});
}
Quat blend(Quat a, Quat b, float weight) {
    if (a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w < 0.f) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; }
    const float t=clamp01(weight);
    return normalized({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
}
void rotate(const Quat &q, float x, float y, float z, float &ox, float &oy, float &oz) {
    const Quat r=multiply(multiply(q,{x,y,z,0.f}),inverse(q)); ox=r.x; oy=r.y; oz=r.z;
}
}  // namespace

DynamicBoneSolver::DynamicBoneSolver(AnimSkeleton *skeleton) : skeleton_(skeleton) {}
DynamicBoneSolver::~DynamicBoneSolver() = default;

void DynamicBoneSolver::setSkeleton(AnimSkeleton *skeleton) {
    if (skeleton_ == skeleton) return;
    skeleton_ = skeleton; clearChains(); clearColliders();
}

std::vector<int> DynamicBoneSolver::buildChainByIndexes(int rootBone, int endBone) const {
    std::vector<int> result;
    if (!skeleton_ || rootBone<0 || endBone<0 || rootBone>=skeleton_->getBoneCount() || endBone>=skeleton_->getBoneCount()) return result;
    for (int bone=endBone; bone>=0; bone=skeleton_->getParent(bone)) { result.push_back(bone); if (bone==rootBone) break; }
    if (result.empty() || result.back()!=rootBone) return {};
    std::reverse(result.begin(),result.end()); return result;
}

bool DynamicBoneSolver::validateChain(const std::vector<int> &bones) const {
    if (!skeleton_ || bones.size()<2) return false;
    for (size_t i=0;i<bones.size();++i) {
        if (bones[i]<0 || bones[i]>=skeleton_->getBoneCount()) return false;
        if (i>0 && skeleton_->getParent(bones[i])!=bones[i-1]) return false;
    }
    return true;
}

int DynamicBoneSolver::addChain(int rootBone,int endBone,float stiffness,float damping,float inertia,float gravityScale,float radius,int iterations) {
    auto bones=buildChainByIndexes(rootBone,endBone);
    if (!validateChain(bones) || iterations<=0) return -1;
    Chain c; c.bones=std::move(bones); c.stiffness=clamp01(stiffness); c.damping=clamp01(damping);
    c.inertia=clamp01(inertia); c.gravityScale=gravityScale; c.radius=std::max(radius,0.f); c.iterations=iterations;
    chains_.push_back(std::move(c)); return static_cast<int>(chains_.size())-1;
}

int DynamicBoneSolver::addChainByName(const std::string &root,const std::string &end,float stiffness,float damping,float inertia,float gravityScale,float radius,int iterations) {
    if (!skeleton_) return -1;
    return addChain(skeleton_->findBone(root),skeleton_->findBone(end),stiffness,damping,inertia,gravityScale,radius,iterations);
}
int DynamicBoneSolver::getChainCount() const { return static_cast<int>(chains_.size()); }
void DynamicBoneSolver::clearChains() { chains_.clear(); }
void DynamicBoneSolver::setChainEnabled(int index,bool enabled) { if (index>=0 && index<static_cast<int>(chains_.size())) chains_[static_cast<size_t>(index)].enabled=enabled; }
bool DynamicBoneSolver::isChainEnabled(int index) const { return index>=0 && index<static_cast<int>(chains_.size()) && chains_[static_cast<size_t>(index)].enabled; }
void DynamicBoneSolver::setChainFreezeAxis(int index,int axis) { if (index>=0 && index<static_cast<int>(chains_.size())) chains_[static_cast<size_t>(index)].freezeAxis=axis>=1 && axis<=3?axis:0; }
void DynamicBoneSolver::setChainSelfCollision(int index,bool enabled) { if(index>=0&&index<static_cast<int>(chains_.size()))chains_[static_cast<size_t>(index)].selfCollision=enabled; }
void DynamicBoneSolver::setChainParticleParameters(int index,int particle,float stiffness,float damping,float inertia,float gravityScale,float radius) {
    if (index<0 || index>=static_cast<int>(chains_.size())) return;
    auto &chain=chains_[static_cast<size_t>(index)];
    const size_t count=chain.bones.size()+(chain.endMode==0?0U:1U);
    if (particle<0 || static_cast<size_t>(particle)>=count) return;
    if (chain.particleParameters.size()!=count) chain.particleParameters.assign(count,{chain.stiffness,chain.damping,chain.inertia,chain.gravityScale,chain.radius});
    chain.particleParameters[static_cast<size_t>(particle)]={clamp01(stiffness),clamp01(damping),clamp01(inertia),gravityScale,std::max(radius,0.f)};
}
void DynamicBoneSolver::setChainEndLength(int index,float value) { if (index>=0 && index<static_cast<int>(chains_.size())) { auto &chain=chains_[static_cast<size_t>(index)]; chain.endLength=std::max(value,0.f); chain.endMode=chain.endLength>0.f?1:0; chain.particleParameters.clear(); chain.initialized=false; } }
void DynamicBoneSolver::setChainEndOffset(int index,float x,float y,float z) { if (index>=0 && index<static_cast<int>(chains_.size())) { auto &chain=chains_[static_cast<size_t>(index)]; chain.endOffset={x,y,z}; chain.endMode=length(x,y,z)>kEpsilon?2:0; chain.particleParameters.clear(); chain.initialized=false; } }
void DynamicBoneSolver::clearChainEnd(int index) { if (index>=0 && index<static_cast<int>(chains_.size())) { auto &chain=chains_[static_cast<size_t>(index)]; chain.endMode=0; chain.particleParameters.clear(); chain.initialized=false; } }
void DynamicBoneSolver::setGlobalGravity(float x,float y,float z) { globalGravityX_=x; globalGravityY_=y; globalGravityZ_=z; }
void DynamicBoneSolver::setExternalForce(float x,float y,float z) { externalForceX_=x; externalForceY_=y; externalForceZ_=z; }
void DynamicBoneSolver::setWeight(float value) { weight_=clamp01(value); }
void DynamicBoneSolver::setUpdateRate(float value) { updateRate_=std::max(value,0.f); }
void DynamicBoneSolver::setTeleportThreshold(float value) { teleportThreshold_=std::max(value,0.f); }
void DynamicBoneSolver::setObjectMoveResponse(float value) { objectMoveResponse_=clamp01(value); }
void DynamicBoneSolver::setDistanceReference(float x,float y,float z) { distanceReference_={x,y,z}; }
void DynamicBoneSolver::setDistanceLimit(float value) { distanceLimit_=std::max(value,0.f); }
bool DynamicBoneSolver::isChainSleeping(int index) const { return index>=0 && index<static_cast<int>(chains_.size()) && chains_[static_cast<size_t>(index)].sleeping; }
void DynamicBoneSolver::addColliderSphere(float x,float y,float z,float radius) { if (radius>0.f) colliders_.push_back({-1,{},{},{x,y,z},{x,y,z},radius,false,true,false}); }
void DynamicBoneSolver::addBoneColliderSphere(int bone,float x,float y,float z,float radius) { if (skeleton_ && bone>=0 && bone<skeleton_->getBoneCount() && radius>0.f) colliders_.push_back({bone,{x,y,z},{x,y,z},{},{},radius,false,true,false}); }
void DynamicBoneSolver::addColliderCapsule(float sx,float sy,float sz,float ex,float ey,float ez,float radius) { if (radius>0.f) colliders_.push_back({-1,{},{},{sx,sy,sz},{ex,ey,ez},radius,true,true,false}); }
void DynamicBoneSolver::addBoneColliderCapsule(int bone,float sx,float sy,float sz,float ex,float ey,float ez,float radius) { if (skeleton_ && bone>=0 && bone<skeleton_->getBoneCount() && radius>0.f) colliders_.push_back({bone,{sx,sy,sz},{ex,ey,ez},{},{},radius,true,true,false}); }
void DynamicBoneSolver::clearColliders() { colliders_.clear(); }
int DynamicBoneSolver::getColliderCount() const { return static_cast<int>(colliders_.size()); }
void DynamicBoneSolver::removeCollider(int index) { if (index>=0 && index<static_cast<int>(colliders_.size())) colliders_.erase(colliders_.begin()+index); }
void DynamicBoneSolver::setColliderEnabled(int index,bool enabled) { if (index>=0 && index<static_cast<int>(colliders_.size())) colliders_[static_cast<size_t>(index)].enabled=enabled; }
void DynamicBoneSolver::setColliderInside(int index,bool inside) { if (index>=0 && index<static_cast<int>(colliders_.size())) colliders_[static_cast<size_t>(index)].inside=inside; }
void DynamicBoneSolver::setColliderRadius(int index,float radius) { if (index>=0 && index<static_cast<int>(colliders_.size()) && radius>0.f) colliders_[static_cast<size_t>(index)].radius=radius; }
void DynamicBoneSolver::reset() { for (auto &chain:chains_) chain.initialized=false; }

void DynamicBoneSolver::captureTargets(Chain &chain,const AnimPose &pose) {
    chain.target.resize(chain.bones.size());
    for (size_t i=0;i<chain.bones.size();++i) { const auto &w=pose.world(chain.bones[i]); chain.target[i]={w.px,w.py,w.pz}; }
    if (chain.endMode==1) {
        const Vec3 a=chain.target[chain.target.size()-2],b=chain.target.back();
        const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,d=length(dx,dy,dz);
        if (d>kEpsilon) chain.target.push_back({b.x+dx*chain.endLength/d,b.y+dy*chain.endLength/d,b.z+dz*chain.endLength/d});
    } else if (chain.endMode==2) {
        const auto &w=pose.world(chain.bones.back()); Vec3 end;
        rotate({w.qx,w.qy,w.qz,w.qw},chain.endOffset.x*w.sx,chain.endOffset.y*w.sy,chain.endOffset.z*w.sz,end.x,end.y,end.z);
        chain.target.push_back({w.px+end.x,w.py+end.y,w.pz+end.z});
    }
    if (chain.particleParameters.size()!=chain.target.size()) chain.particleParameters.assign(chain.target.size(),{chain.stiffness,chain.damping,chain.inertia,chain.gravityScale,chain.radius});
}
void DynamicBoneSolver::initializeChain(Chain &chain) {
    chain.current=chain.target;
    chain.previous=chain.target;
    chain.restLength.resize(chain.target.size()-1);
    for (size_t i=1;i<chain.target.size();++i) chain.restLength[i-1]=length(chain.target[i].x-chain.target[i-1].x,chain.target[i].y-chain.target[i-1].y,chain.target[i].z-chain.target[i-1].z);
    chain.initialized=true;
}
bool DynamicBoneSolver::resolveCollider(const Collider &c,float particleRadius,Vec3 &p) const {
    if (!c.enabled) return false;
    Vec3 center=c.center;
    if (c.capsule) {
        const float sx=c.end.x-c.center.x,sy=c.end.y-c.center.y,sz=c.end.z-c.center.z;
        const float segmentLengthSquared=sx*sx+sy*sy+sz*sz;
        if (segmentLengthSquared>kEpsilon) {
            const float t=std::clamp(((p.x-c.center.x)*sx+(p.y-c.center.y)*sy+(p.z-c.center.z)*sz)/segmentLengthSquared,0.f,1.f);
            center={c.center.x+sx*t,c.center.y+sy*t,c.center.z+sz*t};
        }
    }
    const float dx=p.x-center.x,dy=p.y-center.y,dz=p.z-center.z,d=length(dx,dy,dz);
    const float limit=c.inside?std::max(c.radius-particleRadius,0.f):c.radius+particleRadius;
    if ((!c.inside && d>=limit) || (c.inside && d<=limit)) return false;
    if (d<=kEpsilon) { p.y=center.y+limit; return true; }
    const float s=limit/d; p={center.x+dx*s,center.y+dy*s,center.z+dz*s}; return true;
}
void DynamicBoneSolver::simulateChain(Chain &chain,float dt) {
    chain.current[0]=chain.target[0]; chain.previous[0]=chain.target[0];
    const float dt2=dt*dt;
    for (size_t i=1;i<chain.current.size();++i) {
        const auto &parameters=chain.particleParameters[i];
        const float retention=(1.f-parameters.damping)*parameters.inertia;
        float forceX=externalForceX_,forceY=externalForceY_,forceZ=externalForceZ_;
        if (forceField_) { float x=0.f,y=0.f,z=0.f; forceField_->sampleForce(chain.current[i].x,chain.current[i].y,chain.current[i].z,simulationTime_,x,y,z); forceX+=x; forceY+=y; forceZ+=z; }
        const Vec3 old=chain.current[i];
        chain.current[i].x+=(chain.current[i].x-chain.previous[i].x)*retention+(globalGravityX_+forceX)*parameters.gravityScale*dt2;
        chain.current[i].y+=(chain.current[i].y-chain.previous[i].y)*retention+(globalGravityY_+forceY)*parameters.gravityScale*dt2;
        chain.current[i].z+=(chain.current[i].z-chain.previous[i].z)*retention+(globalGravityZ_+forceZ)*parameters.gravityScale*dt2;
        chain.current[i].x+=(chain.target[i].x-chain.current[i].x)*parameters.stiffness;
        chain.current[i].y+=(chain.target[i].y-chain.current[i].y)*parameters.stiffness;
        chain.current[i].z+=(chain.target[i].z-chain.current[i].z)*parameters.stiffness;
        chain.previous[i]=old;
    }
    for (int pass=0;pass<chain.iterations;++pass) {
        chain.current[0]=chain.target[0];
        if(chain.selfCollision)for(size_t i=1;i<chain.current.size();++i)for(size_t j=i+2;j<chain.current.size();++j){++lastUpdateStats_.selfCollisionTests;Vec3& a=chain.current[i];Vec3& b=chain.current[j];const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,d=length(dx,dy,dz);const float minimum=chain.particleParameters[i].radius+chain.particleParameters[j].radius;if(d<minimum&&minimum>0.f){const float nx=d>kEpsilon?dx/d:1.f,ny=d>kEpsilon?dy/d:0.f,nz=d>kEpsilon?dz/d:0.f,correction=(minimum-d)*0.5f;a.x-=nx*correction;a.y-=ny*correction;a.z-=nz*correction;b.x+=nx*correction;b.y+=ny*correction;b.z+=nz*correction;}}
        for (size_t i=1;i<chain.current.size();++i) {
            Vec3 &a=chain.current[i-1],&b=chain.current[i];
            const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,d=length(dx,dy,dz);
            if (d>kEpsilon) { const float s=chain.restLength[i-1]/d; b={a.x+dx*s,a.y+dy*s,a.z+dz*s}; }
            for (const auto &collider:colliders_) { ++lastUpdateStats_.colliderTests; resolveCollider(collider,chain.particleParameters[i].radius,b); }
            constrainSegment(chain,i);
        }
    }
}
void DynamicBoneSolver::constrainSegment(Chain &chain,size_t i) const {
    Vec3 &a=chain.current[i-1],&b=chain.current[i];
    if (chain.freezeAxis==0) {
        const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,d=length(dx,dy,dz);
        if (d>kEpsilon) { const float s=chain.restLength[i-1]/d; b={a.x+dx*s,a.y+dy*s,a.z+dz*s}; }
        return;
    }
    float *coordinates[3]={&b.x,&b.y,&b.z};
    const float targetCoordinates[3]={chain.target[i].x,chain.target[i].y,chain.target[i].z};
    const float parentCoordinates[3]={a.x,a.y,a.z};
    const int frozen=chain.freezeAxis-1;
    *coordinates[frozen]=targetCoordinates[frozen];
    const int first=(frozen+1)%3,second=(frozen+2)%3;
    float d1=*coordinates[first]-parentCoordinates[first],d2=*coordinates[second]-parentCoordinates[second];
    const float frozenDelta=targetCoordinates[frozen]-parentCoordinates[frozen];
    const float freeLength=std::sqrt(std::max(chain.restLength[i-1]*chain.restLength[i-1]-frozenDelta*frozenDelta,0.f));
    float freeDistance=length(d1,d2,0.f);
    if (freeDistance<=kEpsilon) { d1=targetCoordinates[first]-parentCoordinates[first]; d2=targetCoordinates[second]-parentCoordinates[second]; freeDistance=length(d1,d2,0.f); }
    if (freeDistance>kEpsilon) { *coordinates[first]=parentCoordinates[first]+d1*freeLength/freeDistance; *coordinates[second]=parentCoordinates[second]+d2*freeLength/freeDistance; }
}
void DynamicBoneSolver::updateColliderCenters(const AnimPose &pose) {
    for (auto &c:colliders_) {
        if (c.bone<0) continue;
        const auto &w=pose.world(c.bone);
        rotate({w.qx,w.qy,w.qz,w.qw},c.offset.x*w.sx,c.offset.y*w.sy,c.offset.z*w.sz,c.center.x,c.center.y,c.center.z);
        c.center.x+=w.px; c.center.y+=w.py; c.center.z+=w.pz;
        rotate({w.qx,w.qy,w.qz,w.qw},c.endOffset.x*w.sx,c.endOffset.y*w.sy,c.endOffset.z*w.sz,c.end.x,c.end.y,c.end.z);
        c.end.x+=w.px; c.end.y+=w.py; c.end.z+=w.pz;
    }
}
void DynamicBoneSolver::writeChainRotations(const Chain &chain,AnimPose &pose) const {
    const size_t rotationCount=std::min(chain.bones.size(),chain.current.size()-1);
    for (size_t i=0;i<rotationCount;++i) {
        const int bone=chain.bones[i]; pose.computeWorld(skeleton_);
        const auto &w=pose.world(bone);
        float animatedX,animatedY,animatedZ;
        if (i+1<chain.bones.size()) { const auto &cw=pose.world(chain.bones[i+1]); animatedX=cw.px-w.px; animatedY=cw.py-w.py; animatedZ=cw.pz-w.pz; }
        else { animatedX=chain.target[i+1].x-chain.target[i].x; animatedY=chain.target[i+1].y-chain.target[i].y; animatedZ=chain.target[i+1].z-chain.target[i].z; }
        const Quat delta=fromTo(animatedX,animatedY,animatedZ,chain.current[i+1].x-chain.current[i].x,chain.current[i+1].y-chain.current[i].y,chain.current[i+1].z-chain.current[i].z);
        const Quat desiredWorld=multiply(delta,{w.qx,w.qy,w.qz,w.qw});
        Quat desiredLocal=desiredWorld;
        const int parent=skeleton_->getParent(bone);
        if (parent>=0) { const auto &pw=pose.world(parent); desiredLocal=multiply(inverse({pw.qx,pw.qy,pw.qz,pw.qw}),desiredWorld); }
        const auto &local=pose.local(bone); const Quat result=blend({local.qx,local.qy,local.qz,local.qw},desiredLocal,weight_);
        pose.setLocalRotation(bone,result.x,result.y,result.z,result.w);
    }
}
void DynamicBoneSolver::update(AnimPose *pose,float dt) {
    lastUpdateStats_={};
    if (!skeleton_ || !pose || !std::isfinite(dt) || dt<=0.f || weight_<=0.f || skeleton_->getBoneCount()!=pose->getBoneCount()) return;
    pose->computeWorld(skeleton_); updateColliderCenters(*pose);
    const float preferred=updateRate_>0.f?1.f/updateRate_:dt;
    const int steps=std::clamp(static_cast<int>(std::ceil(dt/preferred)),1,8);
    const float stepDt=std::min(dt/static_cast<float>(steps),preferred);
    for (auto &chain:chains_) {
        if (!chain.enabled || chain.bones.size()<2) continue;
        captureTargets(chain,*pose);
        const float rootDistance=length(chain.target[0].x-distanceReference_.x,chain.target[0].y-distanceReference_.y,chain.target[0].z-distanceReference_.z);
        if (distanceLimit_>0.f && rootDistance>distanceLimit_) { chain.sleeping=true; chain.initialized=false; ++lastUpdateStats_.sleepingChains; continue; }
        chain.sleeping=false;
        ++lastUpdateStats_.activeChains;lastUpdateStats_.particles+=static_cast<int>(chain.target.size());lastUpdateStats_.substeps+=steps;
        if (chain.initialized) {
            const float moveX=chain.target[0].x-chain.current[0].x,moveY=chain.target[0].y-chain.current[0].y,moveZ=chain.target[0].z-chain.current[0].z;
            if (teleportThreshold_>0.f && length(moveX,moveY,moveZ)>teleportThreshold_) chain.initialized=false;
            else if (objectMoveResponse_>0.f) for (size_t i=0;i<chain.current.size();++i) {
                chain.current[i].x+=moveX*objectMoveResponse_; chain.current[i].y+=moveY*objectMoveResponse_; chain.current[i].z+=moveZ*objectMoveResponse_;
                chain.previous[i].x+=moveX*objectMoveResponse_; chain.previous[i].y+=moveY*objectMoveResponse_; chain.previous[i].z+=moveZ*objectMoveResponse_;
            }
        }
        if (!chain.initialized) initializeChain(chain);
        for (int step=0;step<steps;++step) simulateChain(chain,stepDt);
        writeChainRotations(chain,*pose);
    }
    pose->computeWorld(skeleton_);
    simulationTime_+=dt;
}
DynamicBoneDebugSnapshot DynamicBoneSolver::debugSnapshot() const {
    DynamicBoneDebugSnapshot result;
    for(size_t chainIndex=0;chainIndex<chains_.size();++chainIndex){const auto& chain=chains_[chainIndex];for(size_t particle=0;particle<chain.current.size();++particle){const auto& point=chain.current[particle];const float radius=particle<chain.particleParameters.size()?chain.particleParameters[particle].radius:chain.radius;result.particles.push_back({static_cast<int>(chainIndex),static_cast<int>(particle),point.x,point.y,point.z,radius,chain.sleeping});}}
    for(const auto& collider:colliders_)result.colliders.push_back({collider.center.x,collider.center.y,collider.center.z,collider.end.x,collider.end.y,collider.end.z,collider.radius,collider.capsule,collider.inside,collider.enabled});
    return result;
}

}  // namespace eve::animation
