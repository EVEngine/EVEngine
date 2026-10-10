#include "procgen/Procgen.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"

#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "common/SquirrelBinding.h"
#include "procgen/ProcgenScriptObjects.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::procgen {

eve::Result<ProcgenParamsHandleRef> Procgen::newParamsHandle() {
    Procgen* module = Procgen::create();
    return module->params_.emplace(std::make_unique<Params>());
}

eve::script::Borrowed<Params> Procgen::resolve(ProcgenParamsHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    if (!module) return {};
    return module->params_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenParamsHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded", "params", {}, "procgen.squirrel"));
    return module->params_.erase(reference);
}

bool Procgen::isStale(ProcgenParamsHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = liveProcgen();
    return !module || module->params_.isStale(reference);
}

eve::Result<ProcgenOutputHandleRef> Procgen::newOutputHandle() {
    Procgen* module = Procgen::create();
    return ownProcgenObject(module->ownership_->outputs, std::make_unique<OutputSpec>());
}

eve::script::Borrowed<OutputSpec> Procgen::resolveOutput(ProcgenOutputHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    return module ? module->ownership_->outputs.resolve(reference) : eve::script::Borrowed<OutputSpec>();
}

eve::Result<void> Procgen::releaseOutput(ProcgenOutputHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded", "output", {}, "procgen.squirrel"));
    return module->ownership_->outputs.erase(reference);
}

bool Procgen::isOutputStale(ProcgenOutputHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->outputs.isStale(reference);
}

eve::Result<ProcgenGridHandleRef> Procgen::newGridHandle(int width, int height) {
    if (width <= 0 || height <= 0)
        return eve::Result<ProcgenGridHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "procedural grid dimensions must be positive",
                                   "grid", {}, "procgen.squirrel"));
    auto grid = std::make_unique<Grid2D>();
    grid->resize(width, height);
    Procgen* module = Procgen::create();
    return module->grids_.emplace(std::move(grid));
}

eve::script::Borrowed<Grid2D> Procgen::resolve(ProcgenGridHandleRef reference) noexcept {
    Procgen* module = liveProcgen();
    if (!module) return {};
    return module->grids_.resolve(reference);
}

eve::Result<void> Procgen::release(ProcgenGridHandleRef reference) {
    Procgen* module = liveProcgen();
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Procgen module is no longer loaded", "grid", {}, "procgen.squirrel"));
    return module->grids_.erase(reference);
}

bool Procgen::isStale(ProcgenGridHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Procgen* module = liveProcgen();
    return !module || module->grids_.isStale(reference);
}

eve::Result<ProcgenPointSetHandleRef> Procgen::newPointSetHandle() {
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>());
}

eve::script::Borrowed<PointSet> Procgen::resolvePointSet(ProcgenPointSetHandleRef reference) noexcept {
    return ownership_->points.resolve(reference);
}

eve::Result<void> Procgen::releasePointSet(ProcgenPointSetHandleRef reference) {
    return ownership_->points.erase(reference);
}

bool Procgen::isPointSetStale(ProcgenPointSetHandleRef reference) const noexcept {
    return reference.isValid() && ownership_->points.isStale(reference);
}


}  // namespace eve::procgen
