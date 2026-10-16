#include "crowd/thread/CrowdThread.h"
#include <exception>
#include <vector>
#include "crowd/CrowdWorkScheduler.h"
#include "thread/JobSystem.h"

namespace eve::crowd_thread {
namespace {
Result<void> failed(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, message, "crowd.scheduler"));
}
class JobScheduler final : public eve::crowd::CrowdWorkScheduler {
public:
    explicit JobScheduler(std::weak_ptr<eve::thread::JobSystem> jobs) : jobs_(std::move(jobs)) {}
    Result<void> execute(int partitions, const std::function<void(int)>& body) override try {
        if (partitions < 0 || !body) return failed("Invalid Crowd partition dispatch");
        auto owner = jobs_.lock();
        if (!owner || !owner->isRunning()) return failed("Crowd JobSystem provider expired or stopped");
        if (partitions == 0) return Result<void>::success();
        // JobSystem's join does not propagate child failure: record exceptions in partition-owned slots.
        std::vector<std::exception_ptr>   errors(std::size_t(partitions), nullptr);
        std::unique_ptr<eve::thread::Job> job(owner->parallelFor(
            0, partitions,
            [&](int first, int last) {
                for (int index = first; index < last; ++index) {
                    try {
                        body(index);
                    } catch (...) {
                        errors[std::size_t(index)] = std::current_exception();
                    }
                }
            },
            1));
        // This freshly submitted join cannot wait on itself, the documented wait exception condition.
        job->wait();
        if (job->hasFailed()) return failed("Crowd scheduler join failed");
        for (const auto& error : errors)
            if (error) return failed("Crowd partition body failed after join");
        return Result<void>::success();
    } catch (const std::exception& error) {
        return failed(error.what());
    } catch (...) {
        return failed("Crowd scheduler dispatch failed");
    }

private:
    std::weak_ptr<eve::thread::JobSystem> jobs_;
};
}  // namespace
Result<std::shared_ptr<eve::crowd::CrowdWorkScheduler>> makeJobSystemCrowdScheduler(
    std::weak_ptr<eve::thread::JobSystem> jobs) try {
    return Result<std::shared_ptr<eve::crowd::CrowdWorkScheduler>>::success(
        std::make_shared<JobScheduler>(std::move(jobs)));
} catch (const std::exception& error) {
    return Result<std::shared_ptr<eve::crowd::CrowdWorkScheduler>>::failure(
        Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowd.scheduler"));
}
}  // namespace eve::crowd_thread
