#pragma once
#include "UiLocale.hpp"
#include <utility>

namespace luma::client::ui::preview {
inline std::wstring LocalizedCallMessage(const std::string& value) {
 const auto wide = [](const std::string& text) {
  const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(count, L'\0');
  if (count) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
  return result;
 };
 if (!ChineseUi()) return wide(value);
 // Only these prefixes carry user-provided identities; preserve their suffixes exactly.
 const std::pair<const char*, const wchar_t*> identities[] = {
  {"Connected to ", L"\u5df2\u8fde\u63a5\uff1a"}, {"Connecting to ", L"\u6b63\u5728\u8fde\u63a5\uff1a"},
  {"Calling ", L"\u6b63\u5728\u547c\u53eb\uff1a"}, {"Incoming call from ", L"\u6536\u5230\u6765\u7535\uff1a"}
 };
 for (const auto& item : identities) if (value.rfind(item.first, 0) == 0)
  return std::wstring(UiLabel(item.second, item.second)) + wide(value.substr(std::char_traits<char>::length(item.first)));
 const std::pair<const char*, const wchar_t*> messages[] = {
  {"Offline", L"\u5c1a\u672a\u8fde\u63a5"},
  {"Joining room", L"\u6b63\u5728\u52a0\u5165\u623f\u95f4"},
  {"Ready; choose a participant to call", L"\u5df2\u5c31\u7eea\uff0c\u8bf7\u9009\u62e9\u901a\u8bdd\u5bf9\u8c61"},
  {"Server, room and participant ID are required", L"\u8bf7\u586b\u5199\u670d\u52a1\u5668\u3001\u623f\u95f4\u53f7\u548c\u663e\u793a\u540d\u79f0"},
  {"Signaling connection failed", L"\u65e0\u6cd5\u8fde\u63a5\u670d\u52a1\u5668\uff0c\u8bf7\u68c0\u67e5\u8fde\u63a5\u8bbe\u7f6e\u540e\u91cd\u8bd5"},
  {"Unable to join room", L"\u65e0\u6cd5\u52a0\u5165\u623f\u95f4\uff0c\u8bf7\u68c0\u67e5\u8fde\u63a5\u8bbe\u7f6e\u540e\u91cd\u8bd5"},
  {"Room registration timed out", L"\u52a0\u5165\u623f\u95f4\u8d85\u65f6\uff0c\u8bf7\u91cd\u65b0\u52a0\u5165"},
  {"Signaling disconnected; rejoin to reconnect", L"\u670d\u52a1\u5668\u8fde\u63a5\u65ad\u5f00\uff0c\u8bf7\u91cd\u65b0\u52a0\u5165\u623f\u95f4"},
  {"Signaling send failed", L"\u65e0\u6cd5\u53d1\u9001\u901a\u8bdd\u8bf7\u6c42\uff0c\u8bf7\u91cd\u65b0\u52a0\u5165\u623f\u95f4\u540e\u91cd\u8bd5"},
  {"Requesting connection recovery", L"\u6b63\u5728\u8bf7\u6c42\u6062\u590d\u8fde\u63a5"},
  {"Reconnecting media", L"\u6b63\u5728\u91cd\u65b0\u8fde\u63a5\u97f3\u89c6\u9891"},
  {"Connection interrupted; attempting recovery", L"\u8fde\u63a5\u4e2d\u65ad\uff0c\u6b63\u5728\u5c1d\u8bd5\u6062\u590d"},
  {"Connection failed; reconnect or end the call", L"\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u91cd\u65b0\u8fde\u63a5\u6216\u7ed3\u675f\u901a\u8bdd"},
  {"Unable to request reconnect", L"\u65e0\u6cd5\u8bf7\u6c42\u91cd\u65b0\u8fde\u63a5\uff0c\u8bf7\u91cd\u65b0\u52a0\u5165\u623f\u95f4\u540e\u91cd\u8bd5"},
  {"Unable to restart connection", L"\u65e0\u6cd5\u6062\u590d\u8fde\u63a5\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Unable to send invitation", L"\u65e0\u6cd5\u53d1\u8d77\u901a\u8bdd\uff0c\u8bf7\u68c0\u67e5\u8fde\u63a5\u540e\u91cd\u8bd5"},
  {"Unable to accept invitation", L"\u65e0\u6cd5\u63a5\u542c\u6765\u7535\uff0c\u8bf7\u68c0\u67e5\u8fde\u63a5\u540e\u91cd\u8bd5"},
  {"Media initialization failed", L"\u97f3\u89c6\u9891\u521d\u59cb\u5316\u5931\u8d25\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Unable to create offer", L"\u65e0\u6cd5\u5efa\u7acb\u97f3\u89c6\u9891\u8fde\u63a5\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Unable to create answer", L"\u65e0\u6cd5\u5efa\u7acb\u97f3\u89c6\u9891\u8fde\u63a5\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Invalid remote offer", L"\u5bf9\u65b9\u7684\u8fde\u63a5\u4fe1\u606f\u65e0\u6548\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Invalid remote answer", L"\u5bf9\u65b9\u7684\u8fde\u63a5\u4fe1\u606f\u65e0\u6548\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Invalid remote network candidate", L"\u5bf9\u65b9\u7684\u7f51\u7edc\u8fde\u63a5\u4fe1\u606f\u65e0\u6548\uff0c\u8bf7\u91cd\u65b0\u8fde\u63a5"},
  {"Too many remote network candidates", L"\u5bf9\u65b9\u7684\u7f51\u7edc\u8fde\u63a5\u4fe1\u606f\u5f02\u5e38\uff0c\u8bf7\u7ed3\u675f\u901a\u8bdd\u540e\u91cd\u8bd5"},
  {"Call rejected", L"\u5df2\u62d2\u7edd\u6765\u7535"},
  {"Call rejected by remote participant", L"\u5bf9\u65b9\u62d2\u7edd\u4e86\u901a\u8bdd"},
  {"Remote participant is busy", L"\u5bf9\u65b9\u6b63\u5fd9\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5"},
  {"Caller cancelled", L"\u5bf9\u65b9\u5df2\u53d6\u6d88\u547c\u53eb"},
  {"Call cancelled", L"\u547c\u53eb\u5df2\u53d6\u6d88"},
  {"Call ended", L"\u901a\u8bdd\u5df2\u7ed3\u675f"},
  {"Remote participant ended the call", L"\u5bf9\u65b9\u5df2\u7ed3\u675f\u901a\u8bdd"},
  {"Remote participant left the room", L"\u5bf9\u65b9\u5df2\u79bb\u5f00\u623f\u95f4"},
  {"Call timed out", L"\u547c\u53eb\u8d85\u65f6\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5"}
 };
 for (const auto& item : messages) if (value == item.first) return UiLabel(item.second, item.second);
 if (value.rfind("Media connection failed: ", 0) == 0)
  return UiLabel(L"\u97f3\u89c6\u9891\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u91cd\u65b0\u8fde\u63a5\u6216\u7ed3\u675f\u901a\u8bdd", L"Media connection failed. Reconnect or end the call.");
 return UiLabel(L"\u8fde\u63a5\u672a\u5b8c\u6210\uff0c\u8bf7\u68c0\u67e5\u623f\u95f4\u72b6\u6001\u6216\u91cd\u65b0\u52a0\u5165", L"Check the room status or rejoin");
}
} // namespace luma::client::ui::preview

