#pragma once
#include <memory>
#include "IDeviceCaptureService.hpp"

namespace luma::client::media {
std::unique_ptr<IDeviceCaptureService> CreateDeviceCaptureService();
}
