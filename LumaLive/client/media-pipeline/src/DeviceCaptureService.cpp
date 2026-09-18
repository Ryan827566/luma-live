#include "DeviceCaptureFactory.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <combaseapi.h>
#include <propvarutil.h>
#endif

namespace luma::client::media {

namespace {
#ifdef _WIN32
std::string Utf8FromWide(const WCHAR* value, UINT32 length) {
    if (!value || length == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string result(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), result.data(), n, nullptr, nullptr);
    return result;
}

struct ScopedCom {
    HRESULT hr{E_FAIL};
    ScopedCom() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedCom() { if (SUCCEEDED(hr)) CoUninitialize(); }
    bool Ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

HRESULT FindDevice(const std::string& requested_id, CaptureDeviceType type, IMFActivate** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    ScopedCom com;
    if (!com.Ok()) return com.hr;
    IMFAttributes* attributes = nullptr;
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) return hr;
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        type == CaptureDeviceType::Camera ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes, &activates, &count);
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; ++i) {
            const GUID key = type == CaptureDeviceType::Camera
                ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
                : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID;
            WCHAR* symbolic = nullptr; UINT32 length = 0;
            std::string id;
            if (SUCCEEDED(activates[i]->GetAllocatedString(key, &symbolic, &length))) {
                id = Utf8FromWide(symbolic, length);
                CoTaskMemFree(symbolic);
            }
            bool index_match = false;
            if (requested_id.rfind("mf-device-", 0) == 0) {
                try { index_match = i == std::stoul(requested_id.substr(10)); } catch (...) {}
            }
            if (requested_id.empty() || id == requested_id || index_match) {
                *result = activates[i];
                (*result)->AddRef();
                hr = S_OK;
                break;
            }
        }
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);
    }
    attributes->Release();
    return *result ? S_OK : MF_E_NOT_FOUND;
}

HRESULT ConfigureReader(IMFSourceReader* reader, CaptureDeviceType type, const CameraCaptureConfig& camera, const AudioCaptureConfig& audio) {
    IMFMediaType* media_type = nullptr;
    HRESULT hr = MFCreateMediaType(&media_type);
    if (FAILED(hr)) return hr;
    hr = media_type->SetGUID(MF_MT_MAJOR_TYPE, type == CaptureDeviceType::Camera ? MFMediaType_Video : MFMediaType_Audio);
    if (SUCCEEDED(hr)) {
        if (type == CaptureDeviceType::Camera) {
            hr = media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            if (SUCCEEDED(hr)) hr = MFSetAttributeSize(media_type, MF_MT_FRAME_SIZE, camera.width, camera.height);
            if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(media_type, MF_MT_FRAME_RATE, camera.fps, 1);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        } else {
            hr = media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio.sample_rate);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio.channels);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audio.channels * 2);
            if (SUCCEEDED(hr)) hr = media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audio.sample_rate * audio.channels * 2);
        }
    }
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(type == CaptureDeviceType::Camera ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, media_type);
    media_type->Release();
    return hr;
}
#endif
}

class DeviceCaptureService final : public IDeviceCaptureService {
public:
    ~DeviceCaptureService() override { if (running_) Stop(); }

    shared::contracts::Result Start() override {
        std::lock_guard lock(mutex_);
        if (running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "capture service already running");
#ifdef _WIN32
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, "MFStartup failed");
#endif
        running_ = true;
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result Stop() override {
        std::unique_lock lock(mutex_);
        if (!running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "capture service is not running");
        lock.unlock();
        StopCamera();
        StopMicrophone();
        lock.lock();
#ifdef _WIN32
        MFShutdown();
#endif
        running_ = false;
        return shared::contracts::Result::Ok();
    }

    bool IsRunning() const noexcept override { return running_.load(); }

    CaptureDeviceList EnumerateDevices(CaptureDeviceType type) const override {
        CaptureDeviceList result;
#ifdef _WIN32
        if (!running_) return result;
        ScopedCom com;
        if (!com.Ok()) return result;
        IMFAttributes* attributes = nullptr;
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFCreateAttributes(&attributes, 1))) return result;
        attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, type == CaptureDeviceType::Camera ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
        if (SUCCEEDED(MFEnumDeviceSources(attributes, &activates, &count))) {
            for (UINT32 i = 0; i < count; ++i) {
                const GUID key = type == CaptureDeviceType::Camera ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID;
                WCHAR* name = nullptr; UINT32 nameLen = 0; WCHAR* symbolic = nullptr; UINT32 symbolicLen = 0;
                std::string id;
                if (SUCCEEDED(activates[i]->GetAllocatedString(key, &symbolic, &symbolicLen))) { id = Utf8FromWide(symbolic, symbolicLen); CoTaskMemFree(symbolic); }
                if (id.empty()) id = "mf-device-" + std::to_string(i);
                std::string friendly;
                if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen))) { friendly = Utf8FromWide(name, nameLen); CoTaskMemFree(name); }
                if (friendly.empty()) friendly = type == CaptureDeviceType::Camera ? "Camera" : "Microphone";
                result.devices.push_back({std::move(id), std::move(friendly), type});
                activates[i]->Release();
            }
            CoTaskMemFree(activates);
        }
        attributes->Release();
#else
        (void)type;
#endif
        return result;
    }

    shared::contracts::Result StartCamera(const CameraCaptureConfig& config, VideoFrameCallback callback) override {
        if (!running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "capture service is not running");
        if (!callback) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument, "video callback is empty");
        if (camera_running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "camera capture is already running");
#ifdef _WIN32
        return StartStream(CaptureDeviceType::Camera, config, {}, std::move(callback), {});
#else
        (void)config; return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, "real capture is only available on Windows");
#endif
    }

    shared::contracts::Result StartMicrophone(const AudioCaptureConfig& config, AudioFrameCallback callback) override {
        if (!running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "capture service is not running");
        if (!callback) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument, "audio callback is empty");
        if (microphone_running_) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "microphone capture is already running");
#ifdef _WIN32
        return StartStream(CaptureDeviceType::Microphone, {}, config, {}, std::move(callback));
#else
        (void)config; return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, "real capture is only available on Windows");
#endif
    }

    shared::contracts::Result StopCamera() override { return StopStream(camera_running_, camera_thread_); }
    shared::contracts::Result StopMicrophone() override { return StopStream(microphone_running_, microphone_thread_); }
    bool IsCameraCapturing() const noexcept override { return camera_running_.load(); }
    bool IsMicrophoneCapturing() const noexcept override { return microphone_running_.load(); }

private:
#ifdef _WIN32
    shared::contracts::Result StartStream(CaptureDeviceType type, const CameraCaptureConfig& camera, const AudioCaptureConfig& audio, VideoFrameCallback video_cb, AudioFrameCallback audio_cb) {
        IMFActivate* activate = nullptr;
        HRESULT hr = FindDevice(type == CaptureDeviceType::Camera ? camera.device_id : audio.device_id, type, &activate);
        if (FAILED(hr)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument, "capture device not found");
        IMFMediaSource* source = nullptr;
        hr = activate->ActivateObject(IID_PPV_ARGS(&source));
        activate->Release();
        if (FAILED(hr)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, "failed to activate capture device");
        IMFSourceReader* reader = nullptr;
        hr = MFCreateSourceReaderFromMediaSource(source, nullptr, &reader);
        source->Release();
        if (FAILED(hr)) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal, "failed to create source reader");
        hr = ConfigureReader(reader, type, camera, audio);
        if (FAILED(hr)) { reader->Release(); return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidArgument, "requested capture format is not supported"); }

        std::thread worker([this, reader, type, video_cb = std::move(video_cb), audio_cb = std::move(audio_cb)]() mutable {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            auto& active = type == CaptureDeviceType::Camera ? camera_running_ : microphone_running_;
            active = true;
            while (active && running_) {
                DWORD stream = 0, flags = 0; LONGLONG timestamp = 0; IMFSample* sample = nullptr;
                const HRESULT read_hr = reader->ReadSample(type == CaptureDeviceType::Camera ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream, &flags, &timestamp, &sample);
                if (FAILED(read_hr) || (flags & MF_SOURCE_READERF_ERROR)) break;
                if (!sample) { if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break; continue; }
                IMFMediaBuffer* buffer = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                    BYTE* bytes = nullptr; DWORD max_len = 0, current_len = 0;
                    if (SUCCEEDED(buffer->Lock(&bytes, &max_len, &current_len))) {
                        if (type == CaptureDeviceType::Camera) {
                            pipeline::VideoFrame frame;
                            frame.timestamp_us = static_cast<std::uint64_t>(timestamp / 10); // MF timestamps are 100ns; pipeline uses microseconds.
                            frame.format = pipeline::PixelFormat::NV12;
                            frame.data.assign(bytes, bytes + current_len);
                            IMFMediaType* mt = nullptr;
                            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mt))) {
                                UINT32 w=0,h=0;
                                if (SUCCEEDED(MFGetAttributeSize(mt, MF_MT_FRAME_SIZE, &w, &h))) { frame.width=w; frame.height=h; }
                                mt->Release();
                            }
                            if (frame.IsValid()) video_cb(std::move(frame));
                        } else {
                            pipeline::AudioFrame frame;
                            frame.timestamp_us = static_cast<std::uint64_t>(timestamp / 10);
                            frame.format = pipeline::AudioSampleFormat::S16;
                            frame.data.assign(bytes, bytes + current_len);
                            IMFMediaType* mt = nullptr;
                            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &mt))) {
                                UINT32 rate=0, channels=0, bits=16;
                                if (SUCCEEDED(mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate))) frame.sample_rate=rate;
                                if (SUCCEEDED(mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels))) frame.channels=static_cast<std::uint16_t>(channels);
                                if (SUCCEEDED(mt->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) && bits != 16) frame.format = pipeline::AudioSampleFormat::S16;
                                mt->Release();
                            }
                            if (frame.IsValid()) audio_cb(std::move(frame));
                        }
                        buffer->Unlock();
                    }
                    buffer->Release();
                }
                sample->Release();
            }
            reader->Release();
            active = false;
            CoUninitialize();
        });
        if (type == CaptureDeviceType::Camera) camera_thread_ = std::move(worker); else microphone_thread_ = std::move(worker);
        return shared::contracts::Result::Ok();
    }
#endif

    shared::contracts::Result StopStream(std::atomic_bool& active, std::thread& worker) {
        if (!active) return shared::contracts::Result::Failure(shared::contracts::ErrorCode::InvalidState, "capture stream is not running");
        active = false;
        if (worker.joinable()) worker.join();
        return shared::contracts::Result::Ok();
    }

    std::atomic_bool running_{false};
    std::atomic_bool camera_running_{false};
    std::atomic_bool microphone_running_{false};
    std::mutex mutex_;
    std::thread camera_thread_;
    std::thread microphone_thread_;
};

std::unique_ptr<IDeviceCaptureService> CreateDeviceCaptureService() { return std::make_unique<DeviceCaptureService>(); }

}
