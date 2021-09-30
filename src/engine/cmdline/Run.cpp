#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"
#include "window/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{

static void expose(ssq::VM& vm) {
    auto table = vm.addTable("eve");
    window::Window::expose(table);
}

// create a new project
int cmdRun(std::string path) {
    ssq::VM vm(2048, ssq::Libs::ALL);
    expose(vm);
    ssq::Script script = vm.compileSource(load_content);
    vm.run(script);
    return 0;
}



} // namespace eve
