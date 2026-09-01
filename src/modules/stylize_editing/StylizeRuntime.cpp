#include "stylize_editing/StylizeTarget.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleRecipe.h"
#include "stylize/StylePass.h"

#include <utility>

namespace eve::stylize_editing {
namespace {
template<class T>EditorResult<T>fail(EditorStatus s,const char*r,std::string m){return EditorResult<T>::error(s,RuleId(r),std::move(m));}
}

StylizeRecipeRuntime::StylizeRecipeRuntime() = default;
StylizeRecipeRuntime::~StylizeRecipeRuntime() = default;

EditorResult<void>StylizeRecipeRuntime::publish(const StylizeRecipeTarget&document,graphics::Graphics*graphics){
 if(!graphics)return fail<void>(EditorStatus::Rejected,"editor.stylize.graphics","Stylize publication requires Graphics");
 const auto diagnostics=document.validate();for(const auto&d:diagnostics)if(d.severity==DiagnosticSeverity::Error){EditorResult<void>r;r.status=EditorStatus::Rejected;r.diagnostics=diagnostics;return r;}
 std::vector<std::unique_ptr<stylize::StyleInstance>> instances;auto recipe=std::make_unique<stylize::StyleRecipe>();
 try{for(const auto&pass:document.passes()){if(!pass.enabled)continue;auto instance=std::make_unique<stylize::StyleInstance>(pass.style);instance->setPriority(pass.priority);for(const auto&[name,value]:pass.overrides)instance->setFloat(name,static_cast<float>(value));recipe->add(instance.get());instances.push_back(std::move(instance));}if(!instances.empty())recipe->compile(graphics);}catch(const std::exception&exception){return fail<void>(EditorStatus::Failed,"editor.stylize.compile",exception.what());}
 instances_=std::move(instances);recipe_=std::move(recipe);graphics_=graphics;revision_=document.revision();EditorResult<void>result=EditorResult<void>::applied();result.diagnostics=diagnostics;return result;
}
EditorResult<void>StylizeRecipeRuntime::apply(graphics::Graphics*graphics,graphics::Texture*source,graphics::Canvas*destination,Revision expectedRevision)const{
 if(expectedRevision!=revision_)return fail<void>(EditorStatus::Conflict,"editor.stylize.stale","Stylize runtime generation is stale");
 if(!graphics||!source||!destination||graphics!=graphics_||revision_==0)return fail<void>(EditorStatus::Rejected,"editor.stylize.runtime","Stylize runtime requires its compiled Graphics, source and destination");
 try{if(instances_.empty()){auto*previous=graphics->getCanvas();graphics->setCanvas(destination);graphics->drawTexturedRect(source,0,0,static_cast<float>(destination->getWidth()),static_cast<float>(destination->getHeight()),graphics::Color(1,1,1,1));graphics->setCanvas(previous);}else recipe_->apply(graphics,source,destination);}catch(const std::exception&exception){return fail<void>(EditorStatus::Failed,"editor.stylize.apply",exception.what());}return EditorResult<void>::applied();
}

} // namespace eve::stylize_editing
