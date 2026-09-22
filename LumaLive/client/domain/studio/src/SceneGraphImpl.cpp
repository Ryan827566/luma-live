#include "luma/domain/studio/ISceneGraph.hpp"
#include <algorithm>
#include <unordered_map>
namespace luma::client::domain::studio {
class SceneGraphImpl final : public ISceneGraph {
    std::vector<Scene> scenes_;
    std::optional<StudioSceneId> active_;
public:
    std::optional<Scene> GetScene(const StudioSceneId& id) const override { for (const auto& s:scenes_) if(s.id==id) return s; return std::nullopt; }
    std::vector<Scene> GetScenes() const override { return scenes_; }
    bool AddScene(Scene s) override { if(s.id.empty() || GetScene(s.id)) return false; scenes_.push_back(std::move(s)); if(!active_) active_=scenes_.back().id; return true; }
    bool RemoveScene(const StudioSceneId& id) override { auto it=std::remove_if(scenes_.begin(),scenes_.end(),[&](const Scene&s){return s.id==id;}); if(it==scenes_.end()) return false; scenes_.erase(it,scenes_.end()); if(active_ && *active_==id) active_=scenes_.empty()?std::nullopt:std::optional<StudioSceneId>(scenes_.front().id); return true; }
    bool SetActiveScene(const StudioSceneId& id) override { if(!GetScene(id)) return false; active_=id; return true; }
    std::optional<StudioSceneId> GetActiveSceneId() const override { return active_; }
    std::optional<Layer> GetLayer(const StudioSceneId& sid,const StudioLayerId& lid) const override { auto s=GetScene(sid); if(!s) return std::nullopt; for(const auto& l:s->layers) if(l.id==lid) return l; return std::nullopt; }
    bool AddLayer(const StudioSceneId& sid,Layer layer) override { for(auto& s:scenes_) if(s.id==sid){ if(layer.id.empty()) return false; for(const auto& l:s.layers) if(l.id==layer.id) return false; if(layer.zOrder==0) layer.zOrder=static_cast<int>(s.layers.size()); s.layers.push_back(std::move(layer)); return true;} return false; }
    bool RemoveLayer(const StudioSceneId& sid,const StudioLayerId& lid) override { for(auto& s:scenes_) if(s.id==sid){ auto it=std::remove_if(s.layers.begin(),s.layers.end(),[&](const Layer&l){return l.id==lid;}); if(it==s.layers.end()) return false; s.layers.erase(it,s.layers.end()); return true;} return false; }
    bool UpdateLayer(const StudioSceneId& sid,const Layer& layer) override { for(auto& s:scenes_) if(s.id==sid){ for(auto& l:s.layers) if(l.id==layer.id){ l=layer; return true; } } return false; }
};
std::unique_ptr<ISceneGraph> CreateSceneGraph(){return std::make_unique<SceneGraphImpl>();}
}
