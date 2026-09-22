#pragma once
#include "CaptureTypes.hpp"
#include "contracts/errors/Error.hpp"

namespace luma::client::media {

class IDeviceCaptureService {
public:
    virtual ~IDeviceCaptureService() = default;
    virtual shared::contracts::Result Start() = 0;
    virtual shared::contracts::Result Stop() = 0;
    virtual bool IsRunning() const noexcept = 0;
    virtual CaptureDeviceList EnumerateDevices(CaptureDeviceType type) const = 0;

    // Starts a real Windows capture stream and delivers frames on a worker thread.
    // The callbacks are invoked only while the corresponding capture is active.
    virtual shared::contracts::Result StartCamera(const CameraCaptureConfig& config, VideoFrameCallback callback) = 0;
    virtual shared::contracts::Result StartMicrophone(const AudioCaptureConfig& config, AudioFrameCallback callback) = 0;
    virtual shared::contracts::Result StopCamera() = 0;
    virtual shared::contracts::Result StopMicrophone() = 0;
    virtual bool IsCameraCapturing() const noexcept = 0;
    virtual bool IsMicrophoneCapturing() const noexcept = 0;
};

}
