#pragma once
#include <windows.h>
#include <memory>
#include <string>

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
    void FocusEntry();
    // UI-thread snapshot access only; never reads the worker's live service or tokens.
    bool IsAuthenticated() const;
    std::wstring DisplayName() const;
    std::wstring Email() const;
    void Close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
