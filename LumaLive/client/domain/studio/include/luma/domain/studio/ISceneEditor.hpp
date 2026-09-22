#pragma once
#include <memory>
#include "SceneModel.hpp"
#include <memory>
namespace luma::client::domain::studio {
class ISceneGraph;
class ISceneCommandHistory;
enum class SceneEditorMode { Select, Move, Resize, Transform };
struct SceneEditorState { bool editing{false}; SceneEditorMode mode{SceneEditorMode::Select}; StudioSceneId sceneId; std::optional<StudioLayerId> selectedLayerId; bool dragging{false}; bool resizing{false}; };
class ISceneEditor {
public:
    virtual ~ISceneEditor() = default;
    virtual bool BeginEdit() = 0;
    virtual bool EndEdit() = 0;
    virtual bool IsEditing() const = 0;
    virtual bool SelectLayer(const StudioLayerId&) = 0;
    virtual bool ClearSelection() = 0;
    virtual bool MoveSelectedLayer(double, double) = 0;
    virtual bool ResizeSelectedLayer(double, double) = 0;
    virtual bool SetSelectedLayerTransform(const Transform&) = 0;
    virtual bool BringSelectedLayerToFront() = 0;
    virtual bool SendSelectedLayerToBack() = 0;
    virtual bool Undo() = 0;
    virtual bool Redo() = 0;
    virtual SceneEditorState GetState() const = 0;
};
std::unique_ptr<ISceneEditor> CreateSceneEditor(ISceneGraph&, ISceneCommandHistory&);
}
