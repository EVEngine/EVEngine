#include "spritestack_editing/SpriteStackTarget.h"

#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "model3d/ModelData.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace eve::spritestack_editing {
namespace {
template<class T>EditorResult<T>fail(EditorStatus status,const char*rule,std::string message){return EditorResult<T>::error(status,RuleId(rule),std::move(message));}
std::uint64_t checksum(const image::ImageData& image){const auto*bytes=static_cast<const unsigned char*>(image.getData());std::uint64_t hash=1469598103934665603ull;for(std::size_t i=0;i<image.getSize();++i){hash^=bytes[i];hash*=1099511628211ull;}return hash;}
SpriteStackLayerArtifact artifact(const image::ImageData& image,int index){std::size_t covered=0;for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x)if(image.getPixel(x,y).a>0.f)++covered;const std::size_t pixels=static_cast<std::size_t>(image.getWidth())*image.getHeight();return{index,image.getWidth(),image.getHeight(),checksum(image),pixels?static_cast<double>(covered)/pixels:0.0};}
}

SpriteStackBakeRuntime::SpriteStackBakeRuntime()=default;
SpriteStackBakeRuntime::~SpriteStackBakeRuntime()=default;

EditorResult<std::vector<SpriteStackLayerArtifact>>SpriteStackBakeRuntime::bake(const SpriteStackDocumentTarget&document,const ISpriteStackModelResolver*resolver){const auto diagnostics=document.validate();for(const auto&d:diagnostics)if(d.severity==DiagnosticSeverity::Error){EditorResult<std::vector<SpriteStackLayerArtifact>>result;result.status=EditorStatus::Rejected;result.diagnostics=diagnostics;return result;}std::vector<image::ImageData*>raw;try{if(document.value().sourceKind=="primitive")raw=spritestack::slicePrimitiveToLayers(document.value().source,document.value().bake);else{if(!resolver)return fail<std::vector<SpriteStackLayerArtifact>>(EditorStatus::Rejected,"editor.spritestack.resolver","Model SpriteStack bake requires an asset resolver");auto resolved=resolver->resolveModel(document.value().source);if(!resolved.value||!*resolved.value){EditorResult<std::vector<SpriteStackLayerArtifact>>failed;failed.status=resolved.status;failed.diagnostics=std::move(resolved.diagnostics);return failed;}raw=spritestack::sliceModelToLayers(*resolved.value,document.value().bake);}}catch(const std::exception&exception){for(auto*layer:raw)delete layer;return fail<std::vector<SpriteStackLayerArtifact>>(EditorStatus::Failed,"editor.spritestack.bake",exception.what());}if(raw.size()!=static_cast<std::size_t>(document.value().bake.layerCount)||std::any_of(raw.begin(),raw.end(),[](auto*p){return p==nullptr;})){for(auto*layer:raw)delete layer;return fail<std::vector<SpriteStackLayerArtifact>>(EditorStatus::Failed,"editor.spritestack.layer-count","SpriteStack baker returned an incomplete generation");}std::vector<std::unique_ptr<image::ImageData>>candidate;std::vector<SpriteStackLayerArtifact>artifacts;candidate.reserve(raw.size());artifacts.reserve(raw.size());for(std::size_t i=0;i<raw.size();++i){candidate.emplace_back(raw[i]);artifacts.push_back(artifact(*raw[i],static_cast<int>(i)));}layers_=std::move(candidate);stack_.reset();value_=document.value();revision_=document.revision();auto result=EditorResult<std::vector<SpriteStackLayerArtifact>>::applied(std::move(artifacts));result.diagnostics=diagnostics;return result;}

EditorResult<spritestack::SpriteStack2D*>SpriteStackBakeRuntime::publish(graphics::Graphics*graphics,Revision expectedRevision){if(expectedRevision!=revision_)return fail<spritestack::SpriteStack2D*>(EditorStatus::Conflict,"editor.spritestack.stale","SpriteStack bake generation is stale");if(!graphics||layers_.empty())return fail<spritestack::SpriteStack2D*>(EditorStatus::Rejected,"editor.spritestack.publish","SpriteStack publication requires Graphics and baked layers");auto candidate=std::make_unique<spritestack::SpriteStack2D>();try{candidate->setLayerCount(static_cast<int>(layers_.size()));for(std::size_t i=0;i<layers_.size();++i)candidate->setLayerImage(graphics,layers_[i].get(),static_cast<int>(i));candidate->setThickness(value_.layerOffset);candidate->setSize(value_.displayWidth,value_.displayHeight);candidate->setShadowEnabled(value_.shadow);candidate->setShadowOpacity(value_.shadowOpacity);candidate->setOutline(value_.outlineWidth);}catch(const std::exception&exception){return fail<spritestack::SpriteStack2D*>(EditorStatus::Failed,"editor.spritestack.publish",exception.what());}stack_=std::move(candidate);return EditorResult<spritestack::SpriteStack2D*>::applied(stack_.get());}

} // namespace eve::spritestack_editing
