#include "housegen_editing/HouseGenTarget.h"

#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include <set>
#include <utility>

namespace eve::housegen_editing {
namespace {
template<class T> EditorResult<T> fail(EditorStatus status,const char* rule,std::string message){return eve::editing::failed<T>(status,RuleId(rule),std::move(message));}
}

HouseGenPreviewRuntime::HouseGenPreviewRuntime() = default;
HouseGenPreviewRuntime::~HouseGenPreviewRuntime() = default;

EditorResult<void> HouseGenPreviewRuntime::publish(const HouseGenDocumentTarget& document) {
    const auto diagnostics = document.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    auto library = std::make_unique<housegen::HouseComponentLibrary>();
    std::set<std::string> categories;
    for (const auto& component : document.components()) {
        auto registered = library->registerComponent(component.component);
        if (!registered.ok()) return fail<void>(EditorStatus::Rejected,"editor.housegen.runtime-component","House runtime rejected an authored component");
        categories.insert(component.component.category);
    }
    for (const char* required : {"foundation","floor","wall","door","roof"})
        if (!categories.contains(required))
            return fail<void>(EditorStatus::Rejected,"editor.housegen.runtime-category","House kit is missing required category: "+std::string(required));
    auto layout = std::make_unique<housegen::HouseLayout>();
    housegen::HouseGenerator generator(*library);
    auto generated = generator.generate(document.request(), *layout);
    if (!generated.ok()) return fail<void>(EditorStatus::Failed,"editor.housegen.generate","House generator rejected the candidate kit or request");
    library_=std::move(library); layout_=std::move(layout); revision_=document.revision();
    return eve::editing::applied<void>(diagnostics);
}

EditorResult<EditorGizmoSnapshot> HouseGenPreviewRuntime::gizmo(Revision expectedRevision) const {
    if (expectedRevision!=revision_) return fail<EditorGizmoSnapshot>(EditorStatus::Conflict,"editor.housegen.stale","House preview generation is stale");
    if (!layout_||!library_) return fail<EditorGizmoSnapshot>(EditorStatus::NotFound,"editor.housegen.preview","House preview has no generated layout");
    EditorGizmoSnapshot snapshot;snapshot.status=EditorStatus::Applied;snapshot.target="housegen-preview";snapshot.targetRevision=revision_;
    if (layout_->instances.size()>200000) return fail<EditorGizmoSnapshot>(EditorStatus::Rejected,"editor.housegen.preview-budget","House layout exceeds 200000 preview instances");
    for (std::size_t index=0;index<layout_->instances.size();++index){const auto& instance=layout_->instances[index];const auto component=library_->find(instance.componentId);if(!component)continue;const auto& c=component->get();const bool quarter=((instance.rotationDeg/90)&1)!=0;EditorGizmoPrimitive p;p.id="component:"+std::to_string(index);p.kind="box";p.position={(instance.x+(quarter?c.depth:c.width)*.5)*layout_->moduleSize,(instance.z*c.height+.5*c.height)*layout_->floorHeight,(instance.y+(quarter?c.width:c.depth)*.5)*layout_->moduleSize};p.size={(quarter?c.depth:c.width)*layout_->moduleSize,c.height*layout_->floorHeight,(quarter?c.width:c.depth)*layout_->moduleSize};if(c.category=="door")p.color={.2,1,.4,.8};else if(c.category=="roof")p.color={1,.5,.2,.65};else if(c.category=="foundation")p.color={.5,.5,.5,.6};else p.color={.2,.7,1,.5};snapshot.primitives.push_back(std::move(p));}
    for(std::size_t index=0;index<layout_->rooms.size();++index){const auto& room=layout_->rooms[index];EditorGizmoPrimitive p;p.id="room:"+std::to_string(index);p.kind="area";p.position={(room.x+room.width*.5)*layout_->moduleSize,.02,(room.y+room.depth*.5)*layout_->moduleSize};p.size={room.width*layout_->moduleSize,.01,room.depth*layout_->moduleSize};p.color={.8,.2,1,.25};snapshot.primitives.push_back(std::move(p));}
    return eve::editing::applied<EditorGizmoSnapshot>(std::move(snapshot));
}

} // namespace eve::housegen_editing
