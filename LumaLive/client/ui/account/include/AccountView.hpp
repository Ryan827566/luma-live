#pragma once
#ifdef _WIN32
#include <windows.h>
#endif

namespace luma::client::ui::account {
#ifdef _WIN32
int RunAccountView(HINSTANCE instance, int show_command);
#else
int RunAccountView(void*, int);
#endif
}
