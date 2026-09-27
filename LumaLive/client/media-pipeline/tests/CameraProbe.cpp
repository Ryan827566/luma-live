#include "DeviceCaptureFactory.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <string>
#include <objbase.h>
int main(int argc,char** argv){
 const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(FAILED(com)){std::cerr<<"COM initialization failed\n";return 1;}
 int code=0;
 {auto service=luma::client::media::CreateDeviceCaptureService();
 auto result=service->Start();
 if(!result.IsOk()){std::cerr<<result.Message()<<'\n';code=1;}
 else {
 auto cameras=service->EnumerateDevices(luma::client::media::CaptureDeviceType::Camera);
 std::cout<<"Camera count: "<<cameras.devices.size()<<std::endl;
 for(size_t i=0;i<cameras.devices.size();++i)std::cout<<i<<": "<<cameras.devices[i].name<<std::endl;
 if(argc>1&&std::string(argv[1])=="--capture"){
 if(cameras.devices.empty()){std::cerr<<"SKIP: no camera exposed to this process\n";code=77;}
 else {std::atomic<int> frames{0};luma::client::media::CameraCaptureConfig cfg;cfg.device_id=cameras.devices.front().id;
 result=service->StartCamera(cfg,[&](auto frame){if(frame.IsValid())++frames;});
 if(!result.IsOk()){std::cerr<<result.Message()<<'\n';code=1;}
 else {auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(frames<3&&service->IsCameraCapturing()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));service->StopCamera();std::cout<<"Captured frames: "<<frames<<'\n';if(frames<3)code=1;}
 }}
 service->Stop();}}
 CoUninitialize();return code;
}
