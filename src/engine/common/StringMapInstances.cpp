// Keep static and non-Windows builds free of unused explicit instantiations.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
#include "common/StringMapInstances.h"

template class __declspec(dllexport) std::unordered_map<std::string, std::string>;
#endif
