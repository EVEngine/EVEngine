// Appended to the engine's generated ECS.hpp. The upstream standalone header
// remains header-only; engine/SDK consumers link these operations from the
// same foundation that already owns the process-wide default Table.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::map<std::type_index, ecs::IComponentManager*>;
extern template class __declspec(dllimport) std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#else
extern template class std::map<std::type_index, ecs::IComponentManager*>;
extern template class std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#endif

// Handle values keep their original Table/type/id/generation projection and
// borrowed lifetimes. Only Windows DLL non-template container code is shared.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::vector<ecs::EntityHandle>;
#endif
