#include "editor/EditorSurfaceFluidPreview.h"

#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceWetnessField.h"

#include <algorithm>
#include <cmath>

namespace eve::editor {
namespace {
void error(SurfaceFluidPreviewSnapshot& result, const char* rule, std::string message) {
    result.diagnostics.push_back({RuleId(rule), DiagnosticSeverity::Error, std::move(message)});
    result.status = EditorStatus::Rejected;
}
bool finite(const std::array<double, 3>& value) {
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}
std::array<double, 3> values(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}
}

SurfaceFluidPreviewSnapshot SurfaceFluidPreviewService::build(
    const SurfaceFluidTarget& target, const SurfaceFluidPreviewRequest& request) const {
    SurfaceFluidPreviewSnapshot result; result.documentRevision = target.revision();
    if (request.documentRevision != target.revision()) {
        error(result, "editor.surface-fluid.preview-stale", "Surface fluid preview request is stale");
        result.status = EditorStatus::Conflict; return result;
    }
    if (!std::isfinite(request.seconds) || !std::isfinite(request.fixedStep) ||
        request.seconds < 0.0 || request.seconds > 600.0 || request.fixedStep <= 0.0 ||
        request.positions.empty() || request.positions.size() > request.maximumVertices ||
        request.indices.empty() || request.indices.size() % 3 != 0 ||
        (!request.uvs.empty() && request.uvs.size() != request.positions.size()) ||
        request.seeds.size() > request.maximumDroplets || request.maximumSteps == 0 ||
        request.seconds / request.fixedStep > static_cast<double>(request.maximumSteps)) {
        error(result, "editor.surface-fluid.preview-request", "Surface fluid preview request or budget is invalid");
        return result;
    }
    std::vector<glm::vec3> positions; positions.reserve(request.positions.size());
    for (const auto& position : request.positions) {
        if (!finite(position)) { error(result,"editor.surface-fluid.preview-position","Surface positions must be finite"); return result; }
        positions.emplace_back(position[0],position[1],position[2]);
    }
    for (std::uint32_t index : request.indices)
        if (index >= positions.size()) { error(result,"editor.surface-fluid.preview-index","Surface index is out of range"); return result; }
    std::vector<glm::vec2> uvs; uvs.reserve(request.uvs.size());
    for (const auto& uv : request.uvs) {
        if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) { error(result,"editor.surface-fluid.preview-uv","Surface UVs must be finite"); return result; }
        uvs.emplace_back(uv[0],uv[1]);
    }
    fluids::FluidSurfaceBinding binding;
    if (!binding.build(positions,request.indices,uvs)) { error(result,"editor.surface-fluid.preview-topology","Surface topology is invalid"); return result; }
    fluids::SurfaceWetnessField wetness;
    if (!wetness.build(binding)) { error(result,"editor.surface-fluid.preview-wetness","Could not initialize surface wetness"); return result; }
    fluids::SurfaceDropletSimulation simulation(&binding,{},&wetness);
    fluids::SurfaceFluidRenderParams renderParams; fluids::SurfaceWetnessParams wetnessParams;
    auto applied=SurfaceFluidRuntimeApplier().apply(target,&simulation,&renderParams,&wetnessParams);
    if(!applied.accepted()){result.status=applied.status;result.diagnostics=std::move(applied.diagnostics);return result;}
    for(const auto& seed:request.seeds){
        if(!finite(seed.barycentric)||!finite(seed.velocity)||!std::isfinite(seed.volume)||seed.volume<=0.0||
           seed.triangle>=static_cast<std::uint32_t>(binding.triangleCount())||
           std::any_of(seed.barycentric.begin(),seed.barycentric.end(),[](double v){return v<0.0;})||
           std::abs(seed.barycentric[0]+seed.barycentric[1]+seed.barycentric[2]-1.0)>1e-5){
            error(result,"editor.surface-fluid.preview-seed","Surface droplet seed is invalid");return result;
        }
        if(!simulation.addDroplet({seed.triangle,{seed.barycentric[0],seed.barycentric[1],seed.barycentric[2]}},
                                  static_cast<float>(seed.volume),{seed.velocity[0],seed.velocity[1],seed.velocity[2]})){
            error(result,"editor.surface-fluid.preview-seed-add","Surface droplet seed could not be added");return result;
        }
    }
    const std::size_t fullSteps=static_cast<std::size_t>(std::floor(request.seconds/request.fixedStep));
    const double remainder=request.seconds-static_cast<double>(fullSteps)*request.fixedStep;
    for(std::size_t step=0;step<fullSteps+(remainder>1e-12?1u:0u);++step){
        const float dt=static_cast<float>(step<fullSteps?request.fixedStep:remainder);
        simulation.step(dt); wetness.step(dt,wetnessParams);
        if(simulation.droplets().size()+simulation.airborneDroplets().size()>request.maximumDroplets){
            error(result,"editor.surface-fluid.preview-droplet-budget","Surface fluid preview exceeds its droplet budget");return result;
        }
    }
    fluids::SurfaceFluidRenderData renderData; renderData.update(binding,simulation,&wetness,renderParams);
    result.droplets.reserve(renderData.droplets().size());
    for(const auto& droplet:renderData.droplets())result.droplets.push_back({droplet.id,values(droplet.position),
        values(droplet.normal),values(droplet.majorAxis),values(droplet.minorAxis),droplet.capHeight,droplet.wetness});
    result.vertexWetness.reserve(wetness.values().size());
    for(float value:wetness.values())result.vertexWetness.push_back(value);
    result.simulatedSeconds=request.seconds;result.status=EditorStatus::Applied;return result;
}

}  // namespace eve::editor
