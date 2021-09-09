#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <filesystem>

using namespace std::filesystem;

namespace eve
{

class Foo {
public:
    Foo(const std::string& msg):msg(msg) {
    }

    const std::string& getMsg() const {
        return msg;
    }

    void setMsg(const std::string& msg) {
        this->msg = msg;
    }

    static void expose(ssq::VM& vm) {
        ssq::Class cls = vm.addClass("Foo", ssq::Class::Ctor<Foo(std::string)>());
        cls.addFunc("getMsg", &Foo::getMsg);
        cls.addFunc("setMsg", &Foo::setMsg);
    }
private:
    std::string msg;
};

class Foo2 : public Foo {
public:
    Foo2() : Foo("default") {

    }

    void bar2() {
        std::cerr << "bar2 called: " << getMsg() << std::endl;
    };

    static void expose(ssq::VM& vm) {
        ssq::Class cls = vm.addClass("Foo2", ssq::Class::Ctor<Foo2()>());
        cls.addFunc("getMsg", &Foo::getMsg);
        cls.addFunc("setMsg", &Foo::setMsg);
        cls.addFunc("bar2", &Foo2::bar2);
    }
};

// create a new project
int cmdRun(std::string path) {
    ssq::VM vm(2048, ssq::Libs::ALL);
    Module::expose(vm);
    ssq::Script script = vm.compileSource(load_content);
    vm.run(script);
    return 0;
}



} // namespace eve
