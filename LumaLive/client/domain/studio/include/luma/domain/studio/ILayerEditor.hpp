#pragma once
#include <memory>
#include "SceneModel.hpp"
namespace luma::client::domain::studio {
class ISceneGraph;
class ILayerEditor {
public:
    virtual ~ILayerEditor() = default;
    virtual bool SetVisible(const StudioLayerId&, bool) = 0;
    virtual bool SetLocked(const StudioLayerId&, bool) = 0;
    virtual bool BringToFront(const StudioLayerId&) = 0;
    virtual bool SendToBack(const StudioLayerId&) = 0;
    virtual bool MoveUp(const StudioLayerId&) = 0;
    virtual bool MoveDown(const StudioLayerId&) = 0;
};
std::unique_ptr<ILayerEditor> CreateLayerEditor(ISceneGraph&);
}
