#pragma once

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cassert>
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>

#include "window/Window.h"
#include "filesystem/Filesystem.h"
#include "mouse/Mouse.h"

#define UnitSciptTest(name, content) \
    class name : public ScriptTest { \
    public: \
        name() : ScriptTest(content) {} \
    }


class ScriptTest {
public:
  ScriptTest(const char* script)
    : script(script), vm(2048, ssq::Libs::ALL) {
    expose(vm);
    ssq::Script s = vm.compileSource(script);
    vm.run(s);
  }

protected:
    static void expose(ssq::VM& vm) { eve::ModuleManager::expose(vm); }

    const char* script;
    ssq::VM     vm;
};
