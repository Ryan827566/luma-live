#include "luma/domain/studio/ISceneEditor.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
#include "luma/domain/studio/ISceneCommandHistory.hpp"
#include "luma/domain/studio/ISceneSelectionService.hpp"
#include "luma/domain/studio/ILayerEditor.hpp"
#include <memory>
namespace luma::client::domain::studio {
class TransformCommand final:public ISceneEditCommand{ISceneGraph&g_;StudioSceneId sid_;StudioLayerId id_;Transform before_,after_;public:TransformCommand(ISceneGraph&g,StudioSceneId s,StudioLayerId i,Transform b,Transform a):g_(g),sid_(std::move(s)),id_(std::move(i)),before_(b),after_(a){}bool Execute()override{auto l=g_.GetLayer(sid_,id_);if(!l)return false;l->transform=after_;return g_.UpdateLayer(sid_,*l);}bool Undo()override{auto l=g_.GetLayer(sid_,id_);if(!l)return false;l->transform=before_;return g_.UpdateLayer(sid_,*l);}std::string GetName()const override{return "Transform Layer";}};
class SceneEditorImpl final:public ISceneEditor{ISceneGraph&g_;ISceneCommandHistory&h_;std::unique_ptr<ISceneSelectionService>sel_;std::unique_ptr<ILayerEditor>layers_;SceneEditorState st_;
public:SceneEditorImpl(ISceneGraph&g,ISceneCommandHistory&h):g_(g),h_(h),sel_(CreateSceneSelectionService()),layers_(CreateLayerEditor(g)){if(auto s=g_.GetActiveSceneId())st_.sceneId=*s;}
bool BeginEdit()override{st_.editing=true;return true;}bool EndEdit()override{st_.editing=false;st_.dragging=false;st_.resizing=false;return true;}bool IsEditing()const override{return st_.editing;}
bool SelectLayer(const StudioLayerId&i)override{auto s=g_.GetActiveSceneId();if(!s||!g_.GetLayer(*s,i))return false;st_.sceneId=*s;sel_->Select(i);st_.selectedLayerId=i;return true;}bool ClearSelection()override{sel_->Clear();st_.selectedLayerId.reset();return true;}
bool MoveSelectedLayer(double dx,double dy)override{return transform([&](Transform&t){t.x+=dx;t.y+=dy;});}bool ResizeSelectedLayer(double dw,double dh)override{return transform([&](Transform&t){t.width+=dw;t.height+=dh;});}bool SetSelectedLayerTransform(const Transform&t)override{return transform([&](Transform&a){a=t;});}
bool BringSelectedLayerToFront()override{return st_.selectedLayerId&&layers_->BringToFront(*st_.selectedLayerId);}bool SendSelectedLayerToBack()override{return st_.selectedLayerId&&layers_->SendToBack(*st_.selectedLayerId);}
bool Undo()override{return h_.Undo();}bool Redo()override{return h_.Redo();}SceneEditorState GetState()const override{return st_;}
private:template<class F>bool transform(F f){if(!st_.selectedLayerId||st_.sceneId.empty())return false;auto l=g_.GetLayer(st_.sceneId,*st_.selectedLayerId);if(!l||l->locked)return false;auto before=l->transform,after=before;f(after);if(after.width<=0||after.height<=0)return false;return h_.Execute(std::make_unique<TransformCommand>(g_,st_.sceneId,*st_.selectedLayerId,before,after));}
};
std::unique_ptr<ISceneEditor>CreateSceneEditor(ISceneGraph&g,ISceneCommandHistory&h){return std::make_unique<SceneEditorImpl>(g,h);}
}
