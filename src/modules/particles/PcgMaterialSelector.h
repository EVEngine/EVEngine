#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include <cstdint>
#include <vector>
namespace eve::graphics { class Texture; }
namespace eve::particles {
class ParticleEmitter;
/** @brief Pcg-compatible particle texture palette that reserves candidate zero. */
class EVENGINE_API_DOMAINS PcgMaterialSelector {
public:
    /** @brief Add a borrowed texture. Graphics retains ownership and it must outlive use. */
    [[nodiscard]] Result<void> add(graphics::Texture* texture);
    /** @brief Clear all borrowed candidates. */
    void clear(){materials_.clear();}
    /** @brief Return candidate count including reserved index zero. */
    int count()const{return static_cast<int>(materials_.size());}
    /** @brief Deterministically select [1,count) and atomically apply it to a borrowed emitter. */
    [[nodiscard]] Result<int> selectAndApply(ParticleEmitter* emitter,std::uint32_t seed)const;
private:
    std::vector<graphics::Texture*> materials_;
};
}
