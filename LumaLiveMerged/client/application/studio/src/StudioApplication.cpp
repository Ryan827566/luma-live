#include "luma/client/application/studio/StudioApplication.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
#include "luma/domain/studio/ISceneCommandHistory.hpp"
#include "luma/domain/studio/ISceneEditor.hpp"
namespace luma::client::application::studio {
using namespace client::domain::studio;
StudioApplication::StudioApplication():graph_(CreateSceneGraph()),history_(CreateSceneCommandHistory()),editor_(CreateSceneEditor(*graph_,*history_)){
    Scene scene{"scene-default","Main Scene",{}};
    graph_->AddScene(scene);
    graph_->AddLayer(scene.id,Layer{"camera-layer","camera","Camera",{40,40,640,360,0},true,false,0});
    graph_->AddLayer(scene.id,Layer{"screen-layer","screen","Screen",{720,40,480,270,0},true,false,1});
}
void StudioApplication::Undo(){editor_->Undo();}
void StudioApplication::Redo(){editor_->Redo();}
}
