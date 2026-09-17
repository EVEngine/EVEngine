#include "procgen/PcgSpawnProgress.h"
#include <algorithm>
#include <cmath>
namespace eve::procgen {
namespace { template<class T> Result<T> invalid(const char* m) { return Result<T>::failure(Diagnostic::error(
    DiagnosticCode::InvalidArgument,m,{}, {},"procgen.pcgSpawnProgress")); } }
Result<PcgSpawnProgressStatus> PcgSpawnProgress::updateRule(std::string name,int total,int done,int localTotal,int localDone) {
    if(name.empty()||total<=0||done<0||done>total||localTotal<=0||localDone<0||localDone>localTotal)
        return invalid<PcgSpawnProgressStatus>("valid rule counts and a nonempty spawner name are required");
    title_="Spawning"; subtitle_=std::move(name)+" "+std::to_string(localDone)+" of "+std::to_string(localTotal)+" Rules";
    totalRuleCount_=total; totalRulesCompleted_=done; spawnerRuleCount_=localTotal; spawnerRulesCompleted_=localDone;
    ruleProgress_=0; status_=done==total?PcgSpawnProgressStatus::Completed:PcgSpawnProgressStatus::Active;
    return Result<PcgSpawnProgressStatus>::success(status_);
}
Result<PcgSpawnProgressStatus> PcgSpawnProgress::updateRuleFraction(double value) {
    if(status_!=PcgSpawnProgressStatus::Active||!std::isfinite(value)||value<0||value>1)
        return invalid<PcgSpawnProgressStatus>("active progress and a finite fraction in [0,1] are required");
    ruleProgress_=value; return Result<PcgSpawnProgressStatus>::success(status_);
}
Result<PcgSpawnProgressStatus> PcgSpawnProgress::requestCancel() {
    if(status_!=PcgSpawnProgressStatus::Active) return invalid<PcgSpawnProgressStatus>("only active spawning can be cancelled");
    status_=PcgSpawnProgressStatus::CancelRequested; return Result<PcgSpawnProgressStatus>::success(status_);
}
PcgSpawnProgressStatus PcgSpawnProgress::clear() noexcept { title_.clear(); subtitle_.clear(); totalRuleCount_=totalRulesCompleted_=spawnerRuleCount_=spawnerRulesCompleted_=0; ruleProgress_=0; return status_=PcgSpawnProgressStatus::Hidden; }
double PcgSpawnProgress::progress() const noexcept { return totalRuleCount_<=0?0:std::clamp((double(totalRulesCompleted_)+ruleProgress_)/double(totalRuleCount_),0.0,1.0); }
} // namespace eve::procgen
