#include "ScreenCaptureService.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <future>

namespace luma::client::media {
bool ScreenCaptureService::Start(std::function<void(pipeline::VideoFrame)> callback) {
    if (running_ || !callback) return false;
    Stop();
    { std::lock_guard lock(errorMutex_); error_.clear(); }
    std::promise<bool> startup; auto result = startup.get_future();
    running_ = true;
    worker_ = std::thread([this, callback=std::move(callback), startup=std::move(startup)]() mutable {
        HDC screen=GetDC(nullptr), dc=screen?CreateCompatibleDC(screen):nullptr;
        const int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
        const double scale=std::min({1.0,1280.0/std::max(1,sw),720.0/std::max(1,sh)});
        const int w=std::max(2,int(sw*scale)&~1), h=std::max(2,int(sh*scale)&~1);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
        HGDIOBJ previous=bitmap?SelectObject(dc,bitmap):nullptr;
        const bool initialized=screen&&dc&&bitmap&&pixels&&sw>0&&sh>0;
        if (!initialized) { std::lock_guard lock(errorMutex_); error_="Unable to initialize display capture"; running_=false; }
        startup.set_value(initialized);
        auto next=std::chrono::steady_clock::now();
        while (running_ && initialized) {
            SetStretchBltMode(dc,COLORONCOLOR);
            if (!StretchBlt(dc,0,0,w,h,screen,0,0,sw,sh,SRCCOPY|CAPTUREBLT)) {
                std::lock_guard lock(errorMutex_); error_="Display capture failed; desktop may be unavailable"; running_=false; break;
            }
            GdiFlush();
            pipeline::VideoFrame frame; frame.width=w; frame.height=h; frame.format=pipeline::PixelFormat::I420;
            frame.timestamp_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const size_t ySize=size_t(w)*h; frame.data.resize(ySize+ySize/2);
            const auto* rgb=static_cast<const unsigned char*>(pixels);
            auto clamp=[](int v){return static_cast<unsigned char>(std::clamp(v,0,255));};
            for(int y=0;y<h;y+=2)for(int x=0;x<w;x+=2) {
                int sumR=0,sumG=0,sumB=0;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                    const size_t at=size_t(y+dy)*w+x+dx; const auto* p=rgb+at*4;
                    const int b=p[0],g=p[1],r=p[2];sumR+=r;sumG+=g;sumB+=b;
                    frame.data[at]=clamp(((66*r+129*g+25*b+128)>>8)+16);
                }
                const int r=sumR/4,g=sumG/4,b=sumB/4;const size_t uv=size_t(y/2)*(w/2)+x/2;
                frame.data[ySize+uv]=clamp(((-38*r-74*g+112*b+128)>>8)+128);
                frame.data[ySize+ySize/4+uv]=clamp(((112*r-94*g-18*b+128)>>8)+128);
            }
            try { callback(std::move(frame)); } catch (...) { std::lock_guard lock(errorMutex_);error_="Display consumer failed";running_=false; }
            next+=std::chrono::milliseconds(67);
            if(next<std::chrono::steady_clock::now())next=std::chrono::steady_clock::now();
            std::this_thread::sleep_until(next);
        }
        if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);
    });
    return result.get();
}
void ScreenCaptureService::Stop() { running_=false;if(worker_.joinable())worker_.join(); }
}
