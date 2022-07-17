#pragma once

#include "common/Data.h"
#include "common/Module.h"

namespace thread
{

class Task : public Module {
public:
    // this will create a loaded event
    void IO(std::string path); 

    // this will create a resource created event
    void Resource(Data* data);

    // this will run a piece of script, a script finished event will be created
    void Script(std::string script);

    void Compute();
    void ComputeShader();


protected:
    // threadPool
};

} // namespace thread
