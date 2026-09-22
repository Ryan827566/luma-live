#include "luma/domain/studio/ISceneSelectionService.hpp"
#include <algorithm>
namespace luma::client::domain::studio {
class SceneSelectionServiceImpl final : public ISceneSelectionService { std::vector<StudioLayerId> ids_; public:
 bool Select(StudioLayerId id) override { if(id.empty()) return false; ids_.clear(); ids_.push_back(std::move(id)); return true; }
 bool SelectMultiple(const std::vector<StudioLayerId>& ids) override { ids_=ids; std::sort(ids_.begin(),ids_.end()); ids_.erase(std::unique(ids_.begin(),ids_.end()),ids_.end()); return !ids_.empty(); }
 bool Deselect(const StudioLayerId& id) override { auto it=std::remove(ids_.begin(),ids_.end(),id); if(it==ids_.end()) return false; ids_.erase(it,ids_.end()); return true; }
 void Clear() override { ids_.clear(); }
 bool IsSelected(const StudioLayerId& id) const override { return std::find(ids_.begin(),ids_.end(),id)!=ids_.end(); }
 std::vector<StudioLayerId> GetSelectedLayers() const override { return ids_; }
 std::optional<StudioLayerId> GetPrimarySelection() const override { return ids_.empty()?std::nullopt:std::optional<StudioLayerId>(ids_.front()); }
};
std::unique_ptr<ISceneSelectionService> CreateSceneSelectionService(){return std::make_unique<SceneSelectionServiceImpl>();}
}
