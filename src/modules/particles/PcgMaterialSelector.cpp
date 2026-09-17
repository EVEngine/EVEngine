#include "particles/PcgMaterialSelector.h"
#include "graphics/Texture.h"
#include "particles/ParticleEmitter.h"
namespace eve::particles {
Result<void> PcgMaterialSelector::add(graphics::Texture* texture){
    if(!texture)return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
        "Pcg material texture must not be null","particles.pcgMaterial"));
    materials_.push_back(texture);return Result<void>::success();
}
Result<int> PcgMaterialSelector::selectAndApply(ParticleEmitter* emitter,std::uint32_t seed)const{
    if(!emitter||materials_.size()<2)return Result<int>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument,"Pcg material selection requires an emitter and at least two materials",
        "particles.pcgMaterial"));
    std::uint32_t mixed=seed+0x9e3779b9u;mixed=(mixed^(mixed>>16u))*0x85ebca6bu;
    mixed=(mixed^(mixed>>13u))*0xc2b2ae35u;mixed^=mixed>>16u;
    const int index=1+static_cast<int>(mixed%static_cast<std::uint32_t>(materials_.size()-1));
    emitter->setTexture(materials_[static_cast<std::size_t>(index)]);
    return Result<int>::success(index);
}
}
