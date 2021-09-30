#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{

std::unordered_map<std::string, Module*> Module::registered_modules;

} // namespace eve
