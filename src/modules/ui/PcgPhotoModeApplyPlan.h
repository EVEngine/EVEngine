#pragma once
#include "common/PcgPhotoModeApply.h"
#include "ui/PcgPhotoModeValues.h"
#include <vector>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Stable, reversible difference between two complete Pcg photo-mode snapshots. */
class PcgPhotoModeApplyPlan {
public:
 /** @brief Compile all changed fields in source declaration order. */
 [[nodiscard]] Result<void> compile(const PcgPhotoModeValues& before,const PcgPhotoModeValues& after);
 /** @brief Compile two schema-1 JSON snapshots atomically. */
 [[nodiscard]] Result<void> compileJson(const std::string& beforeJson,const std::string& afterJson);
 /** @brief Execute through an explicit sink and roll back prior writes after a failure. */
 [[nodiscard]] Result<void> execute(IPhotoModeApplySink& sink);
 /** @brief Execute through the registered capability; absence is an observable failure. */
 [[nodiscard]] Result<void> executeRegistered();
 /** @brief Return the number of changed assignments. */ uint64_t getCommandCount()const noexcept{return commands_.size();}
 /** @brief Return a command field name. */ [[nodiscard]] Result<std::string> getCommandField(uint64_t index)const;
 /** @brief Return a command domain. */ [[nodiscard]] Result<int> getCommandDomain(uint64_t index)const;
 /** @brief Return how many forward assignments completed in the last successful execution. */ uint64_t getAppliedCount()const noexcept{return appliedCount_;}
 /** @brief Return whether the last failed execution restored every prior assignment. */ bool getRollbackComplete()const noexcept{return rollbackComplete_;}
 /** @brief Return plan revision, incremented only by successful compilation or execution. */ uint64_t getRevision()const noexcept{return revision_;}
private:
 struct Command { std::string field; PhotoModeDomain domain; PhotoModeValue before; PhotoModeValue after; };
 std::vector<Command> commands_; uint64_t appliedCount_=0,revision_=0; bool rollbackComplete_=true;
};
/** @brief Register Pcg photo-mode application-plan bindings. */ void exposePcgPhotoModeApplyPlanBindings(ssq::Table& table);
}
