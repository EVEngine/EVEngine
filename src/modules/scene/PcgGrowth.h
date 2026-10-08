#pragma once

#include <cstdint>
#include "common/Export.h"
#include "common/Result.h"
namespace ssq{class Table;} namespace eve::scene{class SceneNodeRef;
/** @brief Caller-owned one-shot Pcg Growth state with explicit time and random seed. */
class EVENGINE_API_PLATFORM PcgGrowth {
public:
 /** @brief Configure scale interval, variance, and duration atomically. */ [[nodiscard]] Result<void> configure(float startScale,float endScale,float variance,float duration);
 /** @brief Choose the final scale from seed and begin at absolute real time. */ [[nodiscard]] Result<void> start(float now,std::uint32_t seed);
 /** @brief Evaluate the linear scale at absolute real time. */ [[nodiscard]] Result<float> advance(float now);
 /** @brief Schedule destruction five seconds from absolute real time. */ [[nodiscard]] Result<void> die(float now);
 /** @brief Return whether delayed destruction is due. */ [[nodiscard]] Result<bool> shouldDestroy(float now)const;
 /** @brief Return selected end scale. */ float getActualEndScale()const noexcept{return actualEnd_;}
 /** @brief Return last evaluated scale. */ float getScale()const noexcept{return scale_;}
 /** @brief Return whether growth reached its end. */ bool getFinished()const noexcept{return finished_;}
private:
 float startScale_=.15f,endScale_=1,variance_=.25f,duration_=5,startTime_=0,actualEnd_=0,scale_=.15f,deathTime_=0;bool started_=false,finished_=false,dying_=false;
};
/** @brief Advance growth on a real scene node; returns true when it removed the node. */
[[nodiscard]] EVENGINE_API_PLATFORM Result<bool> applyPcgGrowth(PcgGrowth& growth, SceneNodeRef& node, float now);
/** @brief Register Pcg Growth bindings. */ void exposePcgGrowthBindings(ssq::Table& table);
}  // namespace eve::scene
