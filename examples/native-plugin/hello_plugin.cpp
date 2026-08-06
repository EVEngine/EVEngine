#include "common/Export.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

// Minimal sample plugin: registers a Squirrel-visible module "HelloPlugin".
namespace eve::hello {

class HelloPlugin : public Module {
public:
    Module_REG(HelloPlugin);
    HelloPlugin() = default;
    ~HelloPlugin() override = default;

    std::string greet() const { return "hello from native plugin"; }
};

Module_IMPL(HelloPlugin, new HelloPlugin());

void HelloPlugin::expose(ssq::Table& table) {
    auto cls = table.addClass(name, HelloPlugin::create, false);
    expose(cls);
}

void HelloPlugin::expose(ssq::Class& cls) {
    cls.addFunc("getName", &HelloPlugin::getName);
    cls.addFunc("greet", &HelloPlugin::greet);
}

}  // namespace eve::hello

EVE_PLUGIN_EXPORT int eve_plugin_init(void) {
    // Module_IMPL already registered HelloPlugin via static ModuleRegister.
    return 0;
}
