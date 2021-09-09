#pragma once

#include <string>

namespace eve
{

int cmdRun(std::string path);

// build the project, need to download tools and source code
int cmdBuild(std::string path, std::string output, std::string platform);

// package game into a single executable file / apk 
int cmdPackage(std::string path, std::string output);

// run test if it has
int cmdTest(std::string path);

// zip the current folder
int cmdZip(std::string path);

// start dev server for hot reload
int cmdDevServer(std::string path);

// get third-party source code 
int cmdGet(std::string url);

// clean the project and remove internal objects
int cmdClean(std::string path);

// show documentation for the module/function/type
int cmdDoc(std::string name);

// create a new project
int cmdCreate(std::string path, std::string name);

} // namespace eve
