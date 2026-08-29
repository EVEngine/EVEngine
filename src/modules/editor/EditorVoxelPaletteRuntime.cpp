#include "editor/EditorVoxelPaletteTarget.h"
#include "voxel/CubeTypeRegistry.h"
#include <utility>
namespace eve::editor{namespace{template<class T>EditorResult<T>fail(EditorStatus s,const char*r,std::string m){return EditorResult<T>::error(s,RuleId(r),std::move(m));}}
VoxelPaletteRuntime::VoxelPaletteRuntime()=default;VoxelPaletteRuntime::~VoxelPaletteRuntime()=default;
EditorResult<std::vector<VoxelPalettePublishedEntry>>VoxelPaletteRuntime::publish(const VoxelPaletteTarget&document){const auto diagnostics=document.validate();for(const auto&d:diagnostics)if(d.severity==DiagnosticSeverity::Error){EditorResult<std::vector<VoxelPalettePublishedEntry>>r;r.status=EditorStatus::Rejected;r.diagnostics=diagnostics;return r;}auto candidate=std::make_unique<voxel::CubeTypeRegistry>();std::vector<VoxelPalettePublishedEntry>published;for(const auto&entry:document.entries()){const int base=candidate->add(entry.type);if(base==0)return fail<std::vector<VoxelPalettePublishedEntry>>(EditorStatus::Failed,"editor.voxel-palette.publish","Voxel registry rejected a validated cube type");published.push_back({entry.id,entry.type.name,base,entry.type.directional?4:1});}registry_=std::move(candidate);revision_=document.revision();auto result=EditorResult<std::vector<VoxelPalettePublishedEntry>>::applied(std::move(published));result.diagnostics=diagnostics;return result;}
} // namespace eve::editor
