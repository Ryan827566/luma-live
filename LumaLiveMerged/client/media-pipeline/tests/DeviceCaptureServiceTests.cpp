#include "DeviceCaptureFactory.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace luma::client::media;
    auto service = CreateDeviceCaptureService();
    assert(service);
    assert(!service->IsRunning());
    auto before = service->EnumerateDevices(CaptureDeviceType::Camera);
    assert(before.devices.empty());
    assert(service->Start().IsOk());
    assert(service->IsRunning());
    auto cameras = service->EnumerateDevices(CaptureDeviceType::Camera);
    auto microphones = service->EnumerateDevices(CaptureDeviceType::Microphone);
    for (const auto& d : cameras.devices) assert(d.type == CaptureDeviceType::Camera && !d.name.empty() && !d.id.empty());
    for (const auto& d : microphones.devices) assert(d.type == CaptureDeviceType::Microphone && !d.name.empty() && !d.id.empty());
    assert(!service->Start().IsOk());
    assert(service->Stop().IsOk());
    assert(!service->IsRunning());
    assert(!service->Stop().IsOk());
    std::cout << "DeviceCaptureServiceTests: PASS\n";
}
