#pragma once
#include "StudioPage.hpp"
#include <memory>
#include <vector>

namespace luma::client::ui::studio {

class StudioPageRouter {
public:
    StudioPageRouter();
    StudioPageRouter(const StudioPageRouter&) = delete;
    StudioPageRouter& operator=(const StudioPageRouter&) = delete;

    void navigate(StudioPage page) noexcept;
    StudioPage current() const noexcept { return current_; }
    const IStudioPage& page() const noexcept;
    const std::vector<StudioPage>& navigation() const noexcept { return navigation_; }
    static const wchar_t* label(StudioPage page) noexcept;

private:
    StudioPage current_{StudioPage::Main};
    std::vector<StudioPage> navigation_;
    std::vector<std::unique_ptr<IStudioPage>> pages_;
};

}
