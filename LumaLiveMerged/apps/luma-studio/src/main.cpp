#include "luma/client/application/studio/StudioApplication.hpp"
#include "luma/client/ui/scene_editor/SceneEditorView.hpp"
#ifdef _WIN32
#include <windows.h>
int WINAPI wWinMain(HINSTANCE h,HINSTANCE, PWSTR,int n){luma::client::application::studio::StudioApplication app; luma::client::ui::scene_editor::SceneEditorView view(app); if(!view.Create(h,n))return 1; MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return static_cast<int>(m.wParam);}
#else
int main(){return 0;}
#endif
