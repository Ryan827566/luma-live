#pragma once
#include "luma/domain/studio/ISceneEditor.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
#include "luma/domain/studio/ISceneCommandHistory.hpp"
#include <memory>
namespace luma::client::application::studio {
class StudioApplication final {
    std::unique_ptr<client::domain::studio::ISceneGraph> graph_;
    std::unique_ptr<client::domain::studio::ISceneCommandHistory> history_;
    std::unique_ptr<client::domain::studio::ISceneEditor> editor_;
public:
    StudioApplication();
    client::domain::studio::ISceneGraph& SceneGraph() noexcept { return *graph_; }
    client::domain::studio::ISceneEditor& SceneEditor() noexcept { return *editor_; }
    void Undo();
    void Redo();
};
}
