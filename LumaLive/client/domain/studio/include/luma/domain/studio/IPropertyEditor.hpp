#pragma once
#include <memory>
#include "SceneModel.hpp"
#include <string_view>
namespace luma::client::domain::studio {
class ISceneGraph;
class IPropertyEditor {
public:
    virtual ~IPropertyEditor() = default;
    virtual bool SetFloat(const StudioLayerId&, std::string_view, double) = 0;
    virtual bool SetBoolean(const StudioLayerId&, std::string_view, bool) = 0;
    virtual bool SetString(const StudioLayerId&, std::string_view, std::string_view) = 0;
};
std::unique_ptr<IPropertyEditor> CreatePropertyEditor(ISceneGraph&);
}
