#include "luma/client/application/studio/StudioApplication.hpp"
#include "luma/client/ui/scene_editor/SceneEditorView.hpp"
#ifdef _WIN32
#include <windows.h>
#include "StudioPreview.hpp"
#include <string_view>
int WINAPI wWinMain(HINSTANCE h,HINSTANCE, PWSTR args,int n){
 if(std::wstring_view(args)!=L"--scene-editor")return luma::client::ui::preview::RunStudioPreview(h,n);
 luma::client::application::studio::StudioApplication app; luma::client::ui::scene_editor::SceneEditorView view(app); if(!view.Create(h,n))return 1; MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return static_cast<int>(m.wParam);}
#else
int main(){return 0;}
#endif
