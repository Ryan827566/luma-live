#include "CaptionOverlay.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace luma::client::ui::preview;
int main(int argc,char** argv){
 HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-360;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 void* memory=nullptr;HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);auto old=SelectObject(dc,bmp);HFONT font=CreateFontW(-20,0,0,0,400,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Microsoft YaHei UI");auto pixels=static_cast<DWORD*>(memory);
 auto clear=[&]{std::fill(pixels,pixels+640*360,0x00334251);};
 auto inside=[&](RECT box,bool expected){GdiFlush();bool changed=false;for(int y=0;y<360;++y)for(int x=0;x<640;++x)if(pixels[y*640+x]!=0x00334251){changed=true;if(x<box.left||x>=box.right||y<box.top||y>=box.bottom)throw std::runtime_error("caption painted outside bounds");}if(changed!=expected)throw std::runtime_error("unexpected caption visibility");};
 int result=0;try{
 clear();RECT box{20,24,620,310};DrawCaptionOverlay(dc,box,L"",L"",font);inside(box,false);
 DrawCaptionOverlay(dc,box,L"\u4e0b\u5468\u53d1\u5e03\u65b0\u7248\u672c\uff0c\u8bf7\u786e\u8ba4\u89c6\u9891\u4f1a\u8bae\u548c\u5b57\u5e55\u6d4b\u8bd5\u5b8c\u6210\u3002",L"We release next week. Please confirm the meeting and caption tests are complete.",font);inside(box,true);
 if(argc>1){BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+640*360*4;std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));out.write(reinterpret_cast<char*>(pixels),640*360*4);}
 int firstRow=360,lastRow=-1;for(int y=0;y<360;++y)for(int x=0;x<640;++x)if((pixels[y*640+x]&0xffffff)==0x00aadaff){firstRow=(std::min)(firstRow,y);lastRow=(std::max)(lastRow,y);}if(lastRow-firstRow<24)throw std::runtime_error("translation failed to wrap to two lines");

 clear();RECT tiny{30,30,70,45};DrawCaptionOverlay(dc,tiny,std::wstring(10000,L'x'),L"",font);inside(tiny,false);
 std::cout<<"PASS: empty, bilingual, clipping and tiny caption bounds\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 SelectObject(dc,old);DeleteObject(font);DeleteObject(bmp);DeleteDC(dc);return result;
}
