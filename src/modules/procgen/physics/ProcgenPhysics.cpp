#include "procgen/physics/ProcgenPhysics.h"

#include "common/SquirrelBinding.h"
#include "physics/World3D.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/physics/GtsTerrainColliderRuntime.h"

#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::procgen_physics {
Module_IMPL(ProcgenPhysics,new ProcgenPhysics());
GtsTerrainColliderRuntime* ProcgenPhysics::newGtsTerrainColliderRuntime(){return new GtsTerrainColliderRuntime();}
void ProcgenPhysics::expose(ssq::Table& table){
    auto module=table.addClass(name,ProcgenPhysics::create,false); expose(module);
    auto runtime=table.addClass<GtsTerrainColliderRuntime>("GtsTerrainColliderRuntime",
        std::function<GtsTerrainColliderRuntime*()>([](){return new GtsTerrainColliderRuntime();}),true);
    runtime.addFunc("replace",[vm=table.getHandle()](GtsTerrainColliderRuntime* self,const procgen::GtsTerrainLodSet* lods,
        physics::World3D* world,int level,float x,float y,float z){
        auto result=(!self||!lods||!world)
            ? Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"GTS collider replace requires runtime, LOD set and world","procgen.physics.gtsTerrainCollider"))
            : self->replace(*lods,*world,level,x,y,z);
        return eve::script::projectResult(vm,std::move(result),[](std::uint64_t value){return Value(static_cast<std::int64_t>(value));});});
    runtime.addFunc("clear",[vm=table.getHandle()](GtsTerrainColliderRuntime* self){
        auto result=self?self->clear():Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"GTS collider runtime is null","procgen.physics.gtsTerrainCollider"));
        return eve::script::projectResult(vm,std::move(result),[](int value){return Value(static_cast<std::int64_t>(value));});});
    runtime.addFunc("getBody",&GtsTerrainColliderRuntime::getBody);
    runtime.addFunc("getTileCount",&GtsTerrainColliderRuntime::getTileCount);
    runtime.addFunc("getRevision",&GtsTerrainColliderRuntime::getRevision);
}
void ProcgenPhysics::expose(ssq::Class& cls){cls.addFunc("newGtsTerrainColliderRuntime",&ProcgenPhysics::newGtsTerrainColliderRuntime);}
}