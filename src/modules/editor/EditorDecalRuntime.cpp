#include "editor/EditorDecalTarget.h"

#include "decal/DecalManager.h"

#include <cmath>

namespace eve::editor {
namespace {
template<class T>EditorResult<T>fail(EditorStatus s,const char*r,std::string m){return EditorResult<T>::error(s,RuleId(r),std::move(m));}
double number(const DecalDocumentTarget&d,const char*p){return *d.value(p)->getIf<double>();}
const EditorValue::Array&array(const DecalDocumentTarget&d,const char*p){return *d.value(p)->getIf<EditorValue::Array>();}
graphics::Texture* resolve(const DecalDocumentTarget&d,const IDecalRuntimeAssetResolver*assets,const char*path,EditorResult<void>&failure){const auto&name=*d.value(path)->getIf<std::string>();if(name.empty())return nullptr;if(!assets){failure=fail<void>(EditorStatus::Rejected,"editor.decal.assets","Decal texture resolver is required");return nullptr;}auto result=assets->texture(name);if(!result.value){failure.status=result.status;failure.diagnostics=std::move(result.diagnostics);return nullptr;}return *result.value;}
}

EditorResult<void>DecalRuntimeBinding::publish(const DecalDocumentTarget&document){
 if(!manager_)return fail<void>(EditorStatus::Rejected,"editor.decal.manager","Live DecalManager is required");
 const auto diagnostics=document.validate();if(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto&d){return d.severity==DiagnosticSeverity::Error;})){EditorResult<void>r;r.status=EditorStatus::Rejected;r.diagnostics=diagnostics;return r;}
 EditorResult<void>assetFailure=EditorResult<void>::applied();auto*albedo=resolve(document,assets_,"texture.albedo",assetFailure);if(!assetFailure.accepted())return assetFailure;auto*normal=resolve(document,assets_,"texture.normal",assetFailure);if(!assetFailure.accepted())return assetFailure;auto*params=resolve(document,assets_,"texture.params",assetFailure);if(!assetFailure.accepted())return assetFailure;
 decal::DecalInstance candidate;const auto&p=array(document,"transform.position"),&n=array(document,"transform.normal"),&uv=array(document,"texture.uvRect");
 candidate.x=static_cast<float>(*p[0].getIf<double>());candidate.y=static_cast<float>(*p[1].getIf<double>());candidate.z=static_cast<float>(*p[2].getIf<double>());
 candidate.nx=static_cast<float>(*n[0].getIf<double>());candidate.ny=static_cast<float>(*n[1].getIf<double>());candidate.nz=static_cast<float>(*n[2].getIf<double>());
 candidate.yaw=static_cast<float>(number(document,"transform.yaw"));candidate.size=static_cast<float>(number(document,"projection.size"));candidate.depth=static_cast<float>(number(document,"projection.depth"));candidate.kind=*document.value("decal.kind")->getIf<std::string>();
 candidate.albedo=albedo;candidate.normal=normal;candidate.params=params;for(int i=0;i<4;++i)candidate.uvRect[i]=static_cast<float>(*uv[i].getIf<double>());
 candidate.normalStrength=static_cast<float>(number(document,"channel.normal"));candidate.roughnessStrength=static_cast<float>(number(document,"channel.roughness"));candidate.metalStrength=static_cast<float>(number(document,"channel.metal"));candidate.emissiveStrength=static_cast<float>(number(document,"channel.emissive"));candidate.blendMode=*document.value("blend.mode")->getIf<std::string>()=="add"?1:0;
 candidate.lifetime=static_cast<float>(number(document,"lifetime.seconds"));candidate.fadeIn=static_cast<float>(number(document,"lifetime.fadeIn"));candidate.fadeOut=static_cast<float>(number(document,"lifetime.fadeOut"));
 const int next=manager_->replace(runtimeId_,std::move(candidate));if(next==0)return fail<void>(EditorStatus::Conflict,"editor.decal.replace","Decal runtime generation is stale or invalid");runtimeId_=next;EditorResult<void>r=EditorResult<void>::applied();r.diagnostics=diagnostics;return r;
}
EditorResult<void>DecalRuntimeBinding::clear(){if(runtimeId_==0)return EditorResult<void>::applied();if(!manager_||!manager_->remove(runtimeId_))return fail<void>(EditorStatus::NotFound,"editor.decal.runtime-stale","Published decal generation no longer exists");runtimeId_=0;return EditorResult<void>::applied();}

EditorResult<void>DecalPublishingTarget::applyDomainOperation(const DomainOperation&operation){if(staging_)return document_.applyDomainOperation(operation);auto candidate=cloneDomainState();auto applied=candidate->applyDomainOperation(operation);if(!applied.accepted())return applied;return commitDomainState(std::move(candidate));}
std::unique_ptr<IDomainOperationTarget>DecalPublishingTarget::cloneDomainState()const{auto candidate=std::make_unique<DecalPublishingTarget>(*this);candidate->staging_=true;return candidate;}
EditorResult<void>DecalPublishingTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget>candidate){auto*typed=dynamic_cast<DecalPublishingTarget*>(candidate.get());if(!typed||typed->targetId()!=targetId()||typed->sink_!=sink_||!typed->staging_)return fail<void>(EditorStatus::Conflict,"editor.decal.publish-candidate","Decal publishing candidate mismatch");if(!sink_)return fail<void>(EditorStatus::Rejected,"editor.decal.publish-sink","Decal publishing target requires a runtime sink");auto published=sink_->publish(typed->document_);if(!published.accepted())return published;document_=typed->document_;return EditorResult<void>::applied();}

} // namespace eve::editor
