#pragma once

#include <string>
#include <functional>
#include <memory>

#include "common/Module.h"
namespace CLI
{
    class App;
    class Formatter;
} // namespace CLI


namespace eve::cmd {

class Cmdline;
class Register;
struct Handler {
    virtual ~Handler() {}
    virtual void setup(CLI::App&, std::shared_ptr<CLI::Formatter>) = 0;
    virtual int parse(CLI::App&, Cmdline&) = 0;
};

class Cmdline : public Module {

public:
    Module_REG(Cmdline);

    int Run(std::string path, std::string root, bool debug = false, int dapPort = 0,
            int mcpPort = 0);

    // build the project, need to download tools and source code
    int Build(std::string path, std::string output, std::string platform);

    // package game into a single executable file / apk
    int Package(std::string path, std::string output, std::string sdk);

    // run test if it has
    int Test(std::string path);

    // zip the current folder
    int Zip(std::string path);

    // start dev server for hot reload
    int DevServer(std::string path);

    // get third-party source code
    int Get(std::string url);

    // clean the project and remove internal objects
    int Clean(std::string path);

    // show documentation for the module/function/type
    int Doc(std::string name);

    // create a new project
    int Create(std::string path, std::string name);

    // this will run passed argv by parsing it
    int runArgs(unsigned argc, char** argv);

    // this will be used in physfs filesystem
    std::string getArgv(unsigned i) { return argv[i]; }
    unsigned getArgc() { return argc; }

    static std::string get_remaining(CLI::App* sub, std::string default_path = ".");

    friend Register;
protected:
    Cmdline();

    unsigned argc;
    char** argv;

    static std::vector<std::function<Handler*()>>& handers();
    static void registerCmd(std::function<Handler*()> handler);
};

#define CMD_REG(name) \
    static eve::cmd::Handler* name##_create() { return new name(); } \
    Register name##_cmd_reg(name##_create)

struct Register {
    Register(std::function<Handler*()> handler) {
        Cmdline::registerCmd(handler);
    }
};


}  // namespace eve
