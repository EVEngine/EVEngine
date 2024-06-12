#pragma once

#include "Task.h"

namespace eve::thread
{

class Thread
{
public:
    virtual ~Thread();

    virtual void addTask(Task *task);
    virtual void removeTask(Task *task);
    virtual void clearTasks();
    
};


class ThreadPool: public Thread
{
public:
    virtual ~ThreadPool();

    virtual void addTask(Task *task);
    virtual void removeTask(Task *task);
    virtual void clearTasks();

protected:
    // threadPools
    std::vector<Thread*> threadPools;    
};

}
