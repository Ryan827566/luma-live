#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <atomic>
#include <iostream>
#include <memory>

namespace {
struct State {
    std::atomic<HRESULT> error{S_OK};
    std::atomic<bool> ready{false}, video{false}, audio{false}, closing{false};
};
class Events final : public IMFPMediaPlayerCallback {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<State> state_;
public:
    explicit Events(std::shared_ptr<State> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(IMFPMediaPlayerCallback)) return E_NOINTERFACE;
        *out = static_cast<IMFPMediaPlayerCallback*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        if (state_->closing) return;
        if (FAILED(event->hrEvent)) { state_->error = event->hrEvent; return; }
        if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
            auto item = MFP_GET_MEDIAITEM_CREATED_EVENT(event)->pMediaItem;
            BOOL present = FALSE, selected = FALSE;
            HRESULT hr = item->HasVideo(&present, &selected);
            if (FAILED(hr)) { state_->error = hr; return; }
            state_->video = present && selected;
            hr = item->HasAudio(&present, &selected);
            if (FAILED(hr)) { state_->error = hr; return; }
            state_->audio = present && selected;
            state_->error = event->pMediaPlayer->SetMediaItem(item);
        }
        if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
            state_->error = event->pMediaPlayer->Play(); state_->ready = true;
        }
    }
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: luma_file_playback_tests <video-with-audio>\n"; return 2; }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { std::cerr << "COM initialization failed\n"; return 2; }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { CoUninitialize(); std::cerr << "MF startup failed\n"; return 2; }
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"LumaPlaybackSmoke";
    RegisterClassW(&wc);
    const auto window = CreateWindowExW(0, wc.lpszClassName, L"Playback smoke", WS_OVERLAPPEDWINDOW, 0, 0, 640, 400, nullptr, nullptr, wc.hInstance, nullptr);
    auto state = std::make_shared<State>();
    Microsoft::WRL::ComPtr<IMFPMediaPlayer> player;
    auto events = new Events(state);
    hr = window ? MFPCreateMediaPlayer(nullptr, FALSE, 0, events, window, &player) : HRESULT_FROM_WIN32(GetLastError());
    events->Release();
    if (SUCCEEDED(hr)) hr = player->CreateMediaItemFromURL(argv[1], FALSE, 0, nullptr);
    LONGLONG position = 0;
    const auto deadline = GetTickCount64() + 12000;
    while (SUCCEEDED(hr) && SUCCEEDED(state->error.load()) && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (state->ready) {
            PROPVARIANT value{};
            const auto positionResult = player->GetPosition(MFP_POSITIONTYPE_100NS, &value);
            if (SUCCEEDED(positionResult) && value.vt == VT_I8) position = value.hVal.QuadPart;
            PropVariantClear(&value);
            player->UpdateVideo();
            if (position >= 5000000) break;
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    const auto eventError = state->error.load();
    const bool passed = SUCCEEDED(hr) && SUCCEEDED(eventError) && state->video && state->audio && position >= 5000000;
    std::cout << "video=" << state->video << " audio=" << state->audio << " position100ns=" << position
              << " startup_hr=0x" << std::hex << static_cast<unsigned long>(hr) << " event_hr=0x" << static_cast<unsigned long>(eventError)
              << (passed ? " PASS\n" : " FAIL (both selected tracks and advancing playback are required)\n");
    state->closing = true;
    if (player) { player->Shutdown(); player.Reset(); }
    if (window) DestroyWindow(window);
    MFShutdown(); CoUninitialize();
    return passed ? 0 : 1;
}
