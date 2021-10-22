#include <gtest/gtest.h>
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

using namespace eve;

class ScriptTest : public ::testing::Test {
public:
  ScriptTest(const char* script)
    : vm(2048, ssq::Libs::ALL), script(script) {}

protected:
  void SetUp() override {
    expose(vm);
    ssq::Script s = vm.compileSource(script);
    vm.run(s);
  }

  // void TearDown() override {}
  static void expose(ssq::VM& vm) {
    ModuleManager::expose(vm);
  }

  const char* script;
  ssq::VM vm;
};
