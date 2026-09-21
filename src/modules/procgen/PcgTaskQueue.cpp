#include "procgen/PcgTaskQueue.h"
#include "common/SquirrelBinding.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
namespace eve::procgen {
namespace { template<class T> Result<T> invalid(const char* message) { return Result<T>::failure(Diagnostic::error(
    DiagnosticCode::InvalidArgument,message,{}, {},"procgen.taskQueue")); } }
Result<std::uint64_t> PcgTaskQueue::add(double wait) {
    if(!std::isfinite(wait)||wait<0) return invalid<std::uint64_t>("task wait time must be finite and non-negative");
    const auto id=nextId_++; tasks_.push_back({id,wait,wait});
    if(status_==PcgTaskQueueStatus::Idle) status_=PcgTaskQueueStatus::Waiting;
    return Result<std::uint64_t>::success(id);
}
Result<PcgTaskQueueStatus> PcgTaskQueue::tick(double dt) {
    if(!std::isfinite(dt)||dt<0) return invalid<PcgTaskQueueStatus>("delta time must be finite and non-negative");
    if(status_==PcgTaskQueueStatus::Ready) return invalid<PcgTaskQueueStatus>("ready task must be resolved before ticking");
    if(tasks_.empty()) { status_=PcgTaskQueueStatus::Idle; return Result<PcgTaskQueueStatus>::success(status_); }
    cursor_%=tasks_.size(); auto& task=tasks_[cursor_]; task.remaining-=dt;
    if(task.remaining<=1e-12) { readyId_=task.id; status_=PcgTaskQueueStatus::Ready; }
    else status_=PcgTaskQueueStatus::Waiting;
    return Result<PcgTaskQueueStatus>::success(status_);
}
Result<PcgTaskQueueStatus> PcgTaskQueue::resolveReady(bool finished) {
    if(status_!=PcgTaskQueueStatus::Ready||tasks_.empty()) return invalid<PcgTaskQueueStatus>("no task is ready");
    if(finished) tasks_.erase(tasks_.begin()+static_cast<std::ptrdiff_t>(cursor_));
    else { tasks_[cursor_].remaining=tasks_[cursor_].wait; ++cursor_; }
    readyId_=0;
    if(tasks_.empty()) { cursor_=0; status_=PcgTaskQueueStatus::Idle; }
    else { cursor_%=tasks_.size(); status_=PcgTaskQueueStatus::Waiting; }
    return Result<PcgTaskQueueStatus>::success(status_);
}
PcgTaskQueueStatus PcgTaskQueue::cancelAll() noexcept { tasks_.clear(); cursor_=0; readyId_=0; return status_=PcgTaskQueueStatus::Idle; }
void exposePcgTaskQueueBindings(ssq::Table& table) {
    auto cls=table.addClass("PcgTaskQueue",ssq::Class::Ctor<PcgTaskQueue()>());
    cls.addFunc("add",[vm=table.getHandle()](PcgTaskQueue* q,float wait){return eve::script::projectResult(vm,q->add(wait),[](std::uint64_t id){return eve::Value(static_cast<int64_t>(id));});});
    cls.addFunc("tick",[vm=table.getHandle()](PcgTaskQueue* q,float dt){return eve::script::projectResult(vm,q->tick(dt),[](PcgTaskQueueStatus s){return eve::Value(static_cast<int>(s));});});
    cls.addFunc("resolveReady",[vm=table.getHandle()](PcgTaskQueue* q,bool done){return eve::script::projectResult(vm,q->resolveReady(done),[](PcgTaskQueueStatus s){return eve::Value(static_cast<int>(s));});});
    cls.addFunc("cancelAll",[](PcgTaskQueue* q){return static_cast<int>(q->cancelAll());});
    cls.addFunc("getReadyTaskId",[](const PcgTaskQueue* q){return static_cast<int64_t>(q->readyTaskId());});
    cls.addFunc("getQueueSize",[](const PcgTaskQueue* q){return q->queueSize();});
    cls.addFunc("getStatus",[](const PcgTaskQueue* q){return static_cast<int>(q->status());});
}
}  // namespace eve::procgen
