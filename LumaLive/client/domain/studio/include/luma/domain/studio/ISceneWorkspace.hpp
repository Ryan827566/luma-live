#pragma once
#include "SceneModel.hpp"
#include <memory>
#include <vector>
namespace luma::client::domain::studio {
class ISceneGraph;
class ISceneWorkspace {
public:
    virtual ~ISceneWorkspace() = default;
    virtual std::vector<Scene> ListScenes() const = 0;
    virtual bool CreateScene(const StudioSceneId&, const std::string&) = 0;
    virtual bool DeleteScene(const StudioSceneId&) = 0;
    virtual bool SwitchScene(const StudioSceneId&) = 0;
    virtual bool AddLayer(const Layer&) = 0;
    virtual bool DeleteLayer(const StudioLayerId&) = 0;
};
std::unique_ptr<ISceneWorkspace> CreateSceneWorkspace(ISceneGraph&);
}
