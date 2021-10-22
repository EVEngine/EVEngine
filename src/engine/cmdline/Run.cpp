#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{

// create a new project
int Cmdline::Run(std::string path) {
    ssq::VM vm(2048, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    ssq::Script script = vm.compileSource(load_content);
    vm.run(script);
    return 0;
}



} // namespace eve
