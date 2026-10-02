#pragma once

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace luma::client::ui::preview {

// Bounds and font use the caller's coordinate system (logical or device pixels).
// Reserve participant labels before passing video bounds to this helper.
inline void DrawCaptionOverlay(HDC dc, RECT bounds, const std::wstring& original,
                               const std::wstring& translated, HFONT font) {
    if (!dc || (original.empty() && translated.empty())) return;
    const int saved = SaveDC(dc);
    if (!saved) return;
    SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW metrics{};
    if (!GetTextMetricsW(dc, &metrics)) { RestoreDC(dc, saved); return; }
    const int line = (std::max)(1, int(metrics.tmHeight + metrics.tmExternalLeading));
    const int padding = (std::max)(4, line / 3);
    const int margin = (std::max)(4, line / 2);
    RECT area{bounds.left + margin, bounds.top + margin,
              bounds.right - margin, bounds.bottom - margin};
    const int contentWidth = int(area.right - area.left) - 2 * padding;
    const int available = int(area.bottom - area.top) / 2 - 2 * padding;
    const int groups = int(!original.empty()) + int(!translated.empty());
    const int gap = groups == 2 ? (std::max)(2, line / 5) : 0;
    const int lineBudget = (available - gap) / line;
    if (contentWidth < 4 * line || lineBudget < groups) { RestoreDC(dc, saved); return; }

    // Bound GDI layout work; ellipsis also marks text shortened before wrapping.
    auto bounded = [](const std::wstring& text) {
        if (text.size() <= 512) return text;
        size_t length = 511;
        if (text[length - 1] >= 0xd800 && text[length - 1] <= 0xdbff) --length;
        return text.substr(0, length) + L"\u2026";
    };
    struct CaptionLine {std::wstring text;bool ellipsize;};
    auto wrap = [&](std::wstring text, int limit) {
        std::vector<CaptionLine> result;
        // Normalize CRLF without turning a single line break into two breaks.
        for (size_t i=0;i<text.size();++i) {
            if(text[i]==L'\r') {
                if(i+1<text.size()&&text[i+1]==L'\n')text.erase(i,1);
                else text[i]=L'\n';
            }
            if(text[i]==L'\t')text[i]=L' ';
        }
        while (!text.empty() && int(result.size()) < limit) {
            if (int(result.size()) == limit - 1) {
                // One final physical line owns all remaining text and its ellipsis.
                std::replace(text.begin(), text.end(), L'\n', L' ');
                result.push_back({std::move(text), true});
                break;
            }
            const auto newline = text.find(L'\n');
            const size_t segment = newline == std::wstring::npos ? text.size() : newline;
            int fit=0;SIZE measured{};
            if(segment) GetTextExtentExPointW(dc,text.c_str(),int(segment),contentWidth,&fit,nullptr,&measured);
            size_t count=(std::min)(segment,size_t((std::max)(0,fit)));
            if (count < segment) {
                // Prefer complete words; CJK and unbroken words wrap at glyph boundaries.
                if(count && text[count-1]>=0xd800 && text[count-1]<=0xdbff)--count;
                const auto space=count?text.find_last_of(L" ",count-1):std::wstring::npos;
                if(space!=std::wstring::npos && space>0)count=space;
                if(!count)count=(segment>=2 && text[0]>=0xd800 && text[0]<=0xdbff)?2:1;
            }
            size_t consumed=count;
            if(count==segment && newline!=std::wstring::npos)++consumed;
            auto row=text.substr(0,count);
            while(!row.empty()&&row.back()==L' ')row.pop_back();
            result.push_back({std::move(row),false});
            text.erase(0,consumed);
            while(!text.empty()&&text.front()==L' ')text.erase(0,1);
        }
        return result;
    };
    const auto source = wrap(bounded(original),(std::min)(2,lineBudget-int(!translated.empty())));
    const auto translation = wrap(bounded(translated),(std::min)(2,lineBudget-int(source.size())));
    const int sourceLines = int(source.size()), translatedLines = int(translation.size());
    const int height = (sourceLines + translatedLines) * line + gap + 2 * padding;
    area.top = area.bottom - height;
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    auto background = CreateSolidBrush(RGB(6, 10, 16));
    FillRect(dc, &area, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    RECT text{area.left + padding, area.top + padding, area.right - padding, 0};
    auto draw = [&](const std::vector<CaptionLine>& rows, COLORREF color) {
        SetTextColor(dc,color);
        for(const auto& row:rows) {
            text.bottom=text.top+line;
            DrawTextW(dc,row.text.c_str(),int(row.text.size()),&text,
                      DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|(row.ellipsize?DT_END_ELLIPSIS:0));
            text.top=text.bottom;
        }
    };
    draw(source,RGB(248,250,252));
    if(sourceLines && translatedLines)text.top+=gap;
    draw(translation,RGB(170,218,255));
    RestoreDC(dc, saved);
}

} // namespace luma::client::ui::preview
