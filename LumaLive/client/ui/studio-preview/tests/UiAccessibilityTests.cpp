#include "UiAccessibility.hpp"
#include <oleacc.h>
#include <commctrl.h>
#include <string>
#include <iostream>
using namespace luma::client::ui::preview;
int wmain(int argc,wchar_t** argv){
 if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
 if(argc==3){auto target=reinterpret_cast<HWND>(_wcstoui64(argv[1],nullptr,10));IAccessible* accessible=nullptr;int code=5;if(SUCCEEDED(AccessibleObjectFromWindow(target,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&accessible)))){VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name=nullptr;const auto hr=accessible->get_accName(self,&name);code=SUCCEEDED(hr)&&name&&std::wstring(name)==argv[2]?0:6;SysFreeString(name);accessible->Release();}CoUninitialize();return code;}
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
 auto parent=CreateWindowW(L"STATIC",L"",WS_OVERLAPPEDWINDOW,0,0,400,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 auto edit=CreateWindowW(L"EDIT",L"unchanged-value",WS_CHILD,0,0,200,30,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
 auto check=[&](const wchar_t* expected){IAccessible* accessible=nullptr;if(FAILED(AccessibleObjectFromWindow(edit,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&accessible))))return false;VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name=nullptr;const auto hr=accessible->get_accName(self,&name);const bool ok=SUCCEEDED(hr)&&name&&std::wstring(name)==expected;SysFreeString(name);accessible->Release();return ok;};
 int result=0;
 if(!SetAccessibleName(edit,L"Translation language")||!check(L"Translation language"))result=2;
 if(!SetAccessibleName(edit,L"Updated input name")||!check(L"Updated input name"))result=3;
 wchar_t value[64]{};GetWindowTextW(edit,value,64);if(std::wstring(value)!=L"unchanged-value")result=4;
 DestroyWindow(parent);CoUninitialize();
 if(!result)std::cout<<"PASS: native accessible name, replacement, preserved input value and cleanup\n";
 return result;
}
