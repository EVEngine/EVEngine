#include "common/Model.h"
#include "ScriptTest.h"

#include "sqclass.h"
#include "sqpcheader.h"
#include "sqtable.h"
#include "sqvm.h"

extern const char* model_content;
extern const char* model2_content;

class ModelScriptTest : public ::testing::Test {
public:
    ModelScriptTest() : vm(2048, ssq::Libs::ALL) {}

protected:
    std::map<std::string, ssq::Object> models;
    std::vector<ssq::Object>           instances;
    std::vector<ssq::Object>           updates;

    void SetUp() override {
        expose(vm);
        ssq::Script s1 = vm.compileSource(model_content);
        ssq::Script s2 = vm.compileSource(model2_content);
        vm.run(s1);
        vm.callFunc(vm.findFunc("update"), vm);
        printf("-----------------------------\n");
        vm.run(s2);
        vm.callFunc(vm.findFunc("update"), vm);
        printf("-----------------------------\n");
    }

    void update() {
        for (int i = 0; i < instances.size(); ++i) {
        }
    }

    void expose(ssq::VM& vm) {
        ModuleManager::expose(vm);
        auto eve = vm.find("eve").toTable();

        static bool first = true;

        auto isClick = eve.addFunc("isClick", [&]() {
            if (first) {
                first = false;
                return true;
            } else
                return false;
        });

        auto createModel = eve.addFunc("model", [](ssq::Object cls) {
            printf("eve.model(class)\n");
            auto o = cls.getRaw();
            EXPECT_EQ(o._type, OT_CLASS);
            auto pclass = o._unVal.pClass;
            auto t      = pclass->_members;
            for (SQInteger i = 0; i < t->_numofnodes; i++) {
                if (sq_type(t->_nodes[i].key) != OT_NULL) {
                    printf("%s ", _string(t->_nodes[i].key)->_val);
                    auto p = t->_nodes[i].val;
                    if (sq_type(p) == OT_INTEGER) {
                        if (_isfield(p)) {
                            printf("field %d\n", _member_idx(p));
                        }
                        if (_ismethod(p)) {
                            printf("method %d\n", _member_idx(p));
                        }
                    }
                }
            }
        });
        eve.addFunc("reg", [&](ssq::Object obj) { instances.push_back(obj); });
        eve.addFunc("set", [&](std::string s, ssq::Object obj) { models[s] = obj; });
        eve.addFunc("get", [&](std::string s) { return models[s]; });
    }

    const char* script;
    ssq::VM     vm;
};


TEST_F(ModelScriptTest, basic) { EXPECT_TRUE(vm.callFunc(vm.findFunc("basic"), vm).toBool()); }
