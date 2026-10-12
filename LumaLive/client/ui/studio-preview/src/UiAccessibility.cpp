#include "UiAccessibility.hpp"
#include <initguid.h>
#include <oleacc.h>
#include <commctrl.h>
namespace luma::client::ui::preview {
namespace {
constexpr UINT_PTR NameSubclass=0x4c554d42;
LRESULT CALLBACK NameProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
 if(m==WM_NCDESTROY){auto service=reinterpret_cast<IAccPropServices*>(data);const MSAAPROPID property=PROPID_ACC_NAME;service->ClearHwndProps(h,DWORD(OBJID_CLIENT),CHILDID_SELF,&property,1);service->Release();RemoveWindowSubclass(h,NameProc,id);}
 return DefSubclassProc(h,m,w,l);
}
}
bool SetAccessibleName(HWND control,const wchar_t* name){
 if(!control||!name)return false;
 DWORD_PTR existing{};
 if(GetWindowSubclass(control,NameProc,NameSubclass,&existing))return SUCCEEDED(reinterpret_cast<IAccPropServices*>(existing)->SetHwndPropStr(control,DWORD(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,name));
 IAccPropServices* service=nullptr;
 if(FAILED(CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service))))return false;
 if(FAILED(service->SetHwndPropStr(control,DWORD(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,name))){service->Release();return false;}
 if(!SetWindowSubclass(control,NameProc,NameSubclass,reinterpret_cast<DWORD_PTR>(service))){const MSAAPROPID property=PROPID_ACC_NAME;service->ClearHwndProps(control,DWORD(OBJID_CLIENT),CHILDID_SELF,&property,1);service->Release();return false;}
 return true;
}
}
