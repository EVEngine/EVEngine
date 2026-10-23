// No unused instances or header-generated exports in static/non-Windows builds.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
#include <vector>
#include "editing/EditingIds.h"
#include "editing/EditingSelection.h"

template class __declspec(dllexport) eve::UuidIdAdapter<eve::editing::RuleIdTag>;
template class __declspec(dllexport) eve::UuidIdAdapter<eve::editing::TargetIdTag>;
template class __declspec(dllexport) eve::UuidIdAdapter<eve::editing::StableIdTag>;
template class __declspec(dllexport) eve::UuidIdAdapter<eve::editing::ObjectIdTag>;
template class __declspec(dllexport) eve::UuidIdAdapter<eve::editing::CapabilityIdTag>;
template class __declspec(dllexport) std::vector<eve::editing::CapabilityId>;
template class __declspec(dllexport) std::vector<eve::editing::SelectionItem>;
#endif
