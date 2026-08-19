#include "thread/JobSystem.h"

#include "thread/JobSystemThreadPool.h"

namespace eve {
namespace thread {

/**
 * Backend switch point: the engine's scheduler is selected here. The default
 * implementation schedules on top of the existing ThreadPool worker pool. A
 * TBB backend only needs to implement the JobSystem/Job/TaskGroup interfaces
 * and be returned from this factory.
 */
JobSystem *createJobSystem(int workerCount) {
    return new JobSystemThreadPool(workerCount);
}

}  // namespace thread
}  // namespace eve
