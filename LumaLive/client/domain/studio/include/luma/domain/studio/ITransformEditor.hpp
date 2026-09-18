#pragma once
#include <memory>
#include "SceneModel.hpp"
#include <optional>
namespace luma::client::domain::studio {
class ISceneGraph;
class ITransformEditor {
public:
    virtual ~ITransformEditor() = default;
    virtual bool SetPosition(const StudioLayerId&, double, double) = 0;
    virtual bool SetSize(const StudioLayerId&, double, double) = 0;
    virtual bool SetRotation(const StudioLayerId&, double) = 0;
    virtual bool SetTransform(const StudioLayerId&, const Transform&) = 0;
    virtual std::optional<Transform> GetTransform(const StudioLayerId&) const = 0;
};
std::unique_ptr<ITransformEditor> CreateTransformEditor(ISceneGraph&);
}
