#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "window/Window.h"

namespace eve
{

void Module::expose(ssq::VM& vm) {
    auto table = vm.addTable("eve");

    auto evc = vm.addEnum("evc");
    auto mt = vm.newTable();
    evc.addSlot("ModuleType", mt);
    mt.set("event", (uint32_t)ModuleType::event);
    mt.set("filesystem", (uint32_t)ModuleType::filesystem);
    mt.set("graphics", (uint32_t)ModuleType::graphics);
    mt.set("image", (uint32_t)ModuleType::image);
    mt.set("math", (uint32_t)ModuleType::math);
    mt.set("network", (uint32_t)ModuleType::network);
    mt.set("system", (uint32_t)ModuleType::system);
    mt.set("thread", (uint32_t)ModuleType::thread);
    mt.set("window", (uint32_t)ModuleType::window);

    window::Window::expose(table);
}


} // namespace eve
