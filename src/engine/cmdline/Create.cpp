#include "cmdline.h"
#include <filesystem>

using namespace std::filesystem;

namespace eve
{
    


// create a new project
int cmdCreate(std::string path, std::string name) {
    create_directory(path+"/"+name);
    return 0;
}

} // namespace eve
