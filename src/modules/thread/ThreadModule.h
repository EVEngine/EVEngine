#pragma once

#include "common/Module.h"

namespace eve::thread
{


/**
 * @brief The ThreadModule class provides an interface for creating and managing threads.
 * It has a default threadpool to run tasks
 */
class ThreadModule : public Module, public ThreadPool
{
public:
    Module_REG(ThreadModule);
    virtual ~ThreadModule() {}
    
    virtual Task* newResourceTask(std::string path) = 0;
    virtual Task* newScriptTask(std::string script) = 0;
    
    virtual Thread* newThread();
    virtual ThreadPool* newThreadPool(uint32_t numThreads);
};

} // namespace eve::thread
