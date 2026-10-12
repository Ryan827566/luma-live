#pragma once
#include <windows.h>
#include <memory>

namespace luma::client::ui::preview {
// The account page shares the workspace lifetime; switching pages never reconnects.
class AccountPanel {
public:
    AccountPanel();
    ~AccountPanel();
    void Open(HWND owner);
    void Hide();
    void Place(RECT bounds, UINT dpi);
    bool CancelPendingAction();
    void Close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
