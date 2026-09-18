#pragma once
#include <memory>
#include "SceneModel.hpp"
#include <vector>
#include <optional>
namespace luma::client::domain::studio {
class ISceneSelectionService {
public:
    virtual ~ISceneSelectionService() = default;
    virtual bool Select(StudioLayerId) = 0;
    virtual bool SelectMultiple(const std::vector<StudioLayerId>&) = 0;
    virtual bool Deselect(const StudioLayerId&) = 0;
    virtual void Clear() = 0;
    virtual bool IsSelected(const StudioLayerId&) const = 0;
    virtual std::vector<StudioLayerId> GetSelectedLayers() const = 0;
    virtual std::optional<StudioLayerId> GetPrimarySelection() const = 0;
};
std::unique_ptr<ISceneSelectionService> CreateSceneSelectionService();
}
