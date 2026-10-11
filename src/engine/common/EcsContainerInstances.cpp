#include <ECS.hpp>

// No Table or component state lives here: this translation unit only owns
// the common map code. Allocation/destruction still follows each Table's
// existing ownership, staging, generation and inheritance contracts.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
template class EVE_ECS_DEFAULT_TABLE_API std::map<std::type_index, ecs::IComponentManager*>;
template class EVE_ECS_DEFAULT_TABLE_API std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#else
template class std::map<std::type_index, ecs::IComponentManager*>;
template class std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#endif
