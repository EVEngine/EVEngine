#pragma once

#include <string>

#include "common/Module.h"

namespace eve {

class Cmdline : public Module {
public:
    Module_REG(Cmdline);

    int Run(std::string path);

    // build the project, need to download tools and source code
    int Build(std::string path, std::string output, std::string platform);

    // package game into a single executable file / apk
    int Package(std::string path, std::string output);

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

    // this will be used in physfs filesystem
    void setArg0(std::string arg0) { this->arg0 = arg0; }
    std::string getArg0() { return arg0; }

protected:
    Cmdline();
    std::string arg0;
};

}  // namespace eve
