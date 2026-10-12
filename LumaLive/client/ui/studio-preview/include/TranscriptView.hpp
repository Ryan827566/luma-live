#pragma once
#include <windows.h>
#include <algorithm>

namespace luma::client::ui::preview {
inline int TranscriptVisibleLines(HWND edit) {
    auto dc = GetDC(edit);
    if (!dc) return 1;
    auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    auto old = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (old) SelectObject(dc, old);
    ReleaseDC(edit, dc);
    RECT format{};
    SendMessageW(edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    return std::max(1, int(format.bottom - format.top) / std::max(1, int(metrics.tmHeight)));
}
inline void UpdateTranscriptScrollbar(HWND edit) {
    const auto lines = int(SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    ShowScrollBar(edit, SB_VERT, lines > TranscriptVisibleLines(edit));
}
inline void ReplaceTranscriptText(HWND edit, const wchar_t* text) {
    DWORD start = 0, end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    const int first = int(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int lines = int(SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    // Wheel/scrollbar navigation leaves the caret at EOF. It must also be in view
    // before incoming captions can follow the tail, otherwise reading jumps.
    const bool follow = start == end && end == DWORD(GetWindowTextLengthW(edit)) &&
                        first + TranscriptVisibleLines(edit) >= lines;
    SetWindowTextW(edit, text);
    UpdateTranscriptScrollbar(edit);
    if (follow) {
        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        // EM_SCROLLCARET is a no-op while the workspace is hidden. Line scrolling
        // also preserves following when users switch between Calls/Meetings/AI.
        SendMessageW(edit, EM_LINESCROLL, 0, SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    } else {
        SendMessageW(edit, EM_SETSEL, start, end);
        // Restoring selection may scroll the control; restore by a delta from
        // its actual position, not by adding the previous first line twice.
        const int current = int(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
        SendMessageW(edit, EM_LINESCROLL, 0, first - current);
    }
}
}
