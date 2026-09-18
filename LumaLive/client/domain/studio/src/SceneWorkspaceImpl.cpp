#include "luma/domain/studio/ISceneWorkspace.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
namespace luma::client::domain::studio {
class SceneWorkspaceImpl final : public ISceneWorkspace { ISceneGraph& g_; public: explicit SceneWorkspaceImpl(ISceneGraph&g):g_(g){}
 std::vector<Scene> ListScenes()const override{return g_.GetScenes();}
 bool CreateScene(const StudioSceneId&id,const std::string&name)override{return g_.AddScene(Scene{id,name,{}});}
 bool DeleteScene(const StudioSceneId&id)override{return g_.RemoveScene(id);}
 bool SwitchScene(const StudioSceneId&id)override{return g_.SetActiveScene(id);}
 bool AddLayer(const Layer&l)override{auto s=g_.GetActiveSceneId();return s&&g_.AddLayer(*s,l);}
 bool DeleteLayer(const StudioLayerId&id)override{auto s=g_.GetActiveSceneId();return s&&g_.RemoveLayer(*s,id);}
};
std::unique_ptr<ISceneWorkspace>CreateSceneWorkspace(ISceneGraph&g){return std::make_unique<SceneWorkspaceImpl>(g);}
}
