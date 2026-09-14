#include "procgen/physics/GtsTerrainColliderRuntime.h"

#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "procgen/GtsTerrainLod.h"

#include <cmath>
#include <limits>
#include <vector>

namespace eve::procgen_physics {
namespace {
struct ColliderLink { physics::PhysicsWorldHandle world; physics::PhysicsBodyHandle body; };
physics::Body3D* resolve(const ColliderLink& link) {
    auto* world=physics::World3D::findWorld(link.world); return world?world->findBody(link.body):nullptr;
}
int destroy(std::vector<ColliderLink>& links) {
    int count=0; for(const auto& link:links) if(auto* world=physics::World3D::findWorld(link.world))
        if(auto* body=world->findBody(link.body)){world->destroyBody(body);++count;} links.clear(); return count;
}
template<class T> Result<T> invalid(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,std::move(message),
                                                 "procgen.physics.gtsTerrainCollider"));
}
}
struct GtsTerrainColliderRuntime::Impl { std::vector<ColliderLink> tiles; std::uint64_t revision=0; };
GtsTerrainColliderRuntime::GtsTerrainColliderRuntime():impl_(std::make_unique<Impl>()){}
GtsTerrainColliderRuntime::~GtsTerrainColliderRuntime(){destroy(impl_->tiles);}
Result<std::uint64_t> GtsTerrainColliderRuntime::replace(const procgen::GtsTerrainLodSet& lods,
 physics::World3D& world,int level,float ox,float oy,float oz) {
    if(!world.isValid()||level<0||level>=lods.getLevelCount()||!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz))
        return invalid<std::uint64_t>("GTS collider world, level or origin is invalid");
    std::vector<ColliderLink> candidate(static_cast<std::size_t>(lods.getTileCount()));
    try {
        for(int tileIndex=0;tileIndex<lods.getTileCount();++tileIndex) {
            const auto* tile=lods.tileAt(tileIndex); if(!tile||level>=static_cast<int>(tile->levels.size())) { destroy(candidate); return invalid<std::uint64_t>("GTS collider tile level is missing"); }
            const auto& mesh=tile->levels[static_cast<std::size_t>(level)]; if(mesh.empty()) continue;
            auto* body=world.newBody("static",ox+tile->offsetX,oy,oz+tile->offsetZ);
            if(!body){destroy(candidate);return invalid<std::uint64_t>("GTS collider body creation failed");}
            ColliderLink link{world.runtimeHandle(),body->runtimeHandle()}; candidate[static_cast<std::size_t>(tileIndex)]=link;
            std::vector<std::int32_t> indices; indices.reserve(mesh.indices().size());
            for(auto index:mesh.indices()){if(index>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) throw eve::Exception("mesh index exceeds physics range");indices.push_back(static_cast<std::int32_t>(index));}
            try { if(!body->newTriangleMeshShape(mesh.positions(),indices)) throw eve::Exception("shape creation returned null"); }
            catch(...) { world.destroyBody(body); candidate[static_cast<std::size_t>(tileIndex)]={}; throw; }
        }
    } catch(const std::exception& error) {
        destroy(candidate); return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::Failed,error.what(),
            "procgen.physics.gtsTerrainCollider"));
    }
    destroy(impl_->tiles); impl_->tiles=std::move(candidate); ++impl_->revision; if(!impl_->revision)++impl_->revision;
    return Result<std::uint64_t>::success(impl_->revision);
}
Result<int> GtsTerrainColliderRuntime::clear(){int count=destroy(impl_->tiles);++impl_->revision;if(!impl_->revision)++impl_->revision;return Result<int>::success(count);}
physics::Body3D* GtsTerrainColliderRuntime::getBody(int index)const{return index>=0&&index<static_cast<int>(impl_->tiles.size())?resolve(impl_->tiles[static_cast<std::size_t>(index)]):nullptr;}
int GtsTerrainColliderRuntime::getTileCount()const{return static_cast<int>(impl_->tiles.size());}
std::uint64_t GtsTerrainColliderRuntime::getRevision()const{return impl_->revision;}
}