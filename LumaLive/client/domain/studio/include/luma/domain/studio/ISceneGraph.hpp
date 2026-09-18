#pragma once
#include "SceneModel.hpp"
#include <memory>
#include <vector>
#include <optional>
namespace luma::client::domain::studio {
class ISceneGraph {
public:
    virtual ~ISceneGraph() = default;
    virtual std::optional<Scene> GetScene(const StudioSceneId&) const = 0;
    virtual std::vector<Scene> GetScenes() const = 0;
    virtual bool AddScene(Scene) = 0;
    virtual bool RemoveScene(const StudioSceneId&) = 0;
    virtual bool SetActiveScene(const StudioSceneId&) = 0;
    virtual std::optional<StudioSceneId> GetActiveSceneId() const = 0;
    virtual std::optional<Layer> GetLayer(const StudioSceneId&, const StudioLayerId&) const = 0;
    virtual bool AddLayer(const StudioSceneId&, Layer) = 0;
    virtual bool RemoveLayer(const StudioSceneId&, const StudioLayerId&) = 0;
    virtual bool UpdateLayer(const StudioSceneId&, const Layer&) = 0;
};
std::unique_ptr<ISceneGraph> CreateSceneGraph();
}
