#include <gtest/gtest.h>
#include <iostream>

#include "window/Window.h"
#include <simplesquirrel/simplesquirrel.hpp>

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
    auto table = vm.addTable("eve");
    window::Window::expose(table);
  }

  const char* script;
  ssq::VM vm;
};
