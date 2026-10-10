#include "TranscriptView.hpp"
#include <iostream>
#include <string>
#include <stdexcept>
using namespace luma::client::ui::preview;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    auto parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 600, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 400, 180, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent || !edit) return 1;
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    auto first = [&] { return int(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0)); };
    try {
        std::wstring text;
        for (int i = 0; i < 100; ++i) text += L"Transcript line " + std::to_wstring(i) + L"\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > 0, "Initial long transcript did not follow tail");
        require(GetWindowLongW(edit, GWL_STYLE) & WS_VSCROLL, "Overflow scrollbar hidden");
        const int bottom = first();
        SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA * 4), 0);
        const int reading = first();
        require(reading < bottom, "Wheel did not leave tail");
        text += L"New incoming caption\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() == reading, "Incoming caption interrupted wheel reading");
        // Selected text must survive a refresh, even with a nonzero scroll position.
        SendMessageW(edit, EM_SETSEL, 20, 40);
        SendMessageW(edit, EM_LINESCROLL, 0, 20 - first());
        const int selectedTop = first();
        text += L"Another caption\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        DWORD start = 0, end = 0;
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        require(start == 20 && end == 40, "Selection lost during refresh");
        require(first() == selectedTop, "Selection restore moved reading position");
        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        const int resumed = first();
        text += L"Follow tail again\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > resumed, "Returning to tail did not resume following");
        ShowWindow(parent, SW_HIDE);
        for (int i = 0; i < 20; ++i) text += L"Caption while workspace is hidden\r\n";
        ReplaceTranscriptText(edit, text.c_str());
        require(first() > resumed + 15, "Hidden workspace stopped following incoming captions");
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        require(first() > resumed + 15, "Revealing workspace lost the latest captions");
        ReplaceTranscriptText(edit, L"Short transcript");
        require(first() == 0, "Short transcript retained invalid viewport");
        require(!(GetWindowLongW(edit, GWL_STYLE) & WS_VSCROLL), "Short transcript retained scrollbar");
        std::cout << "PASS: native transcript wheel reading, selection, tail resumption and shrink\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n"; DestroyWindow(parent); return 1;
    }
    DestroyWindow(parent); return 0;
}
