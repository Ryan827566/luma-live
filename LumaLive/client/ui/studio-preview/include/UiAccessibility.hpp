#pragma once
#include <windows.h>
namespace luma::client::ui::preview {
// Name an input without replacing its value or its native accessibility provider.
bool SetAccessibleName(HWND control,const wchar_t* name);
}
