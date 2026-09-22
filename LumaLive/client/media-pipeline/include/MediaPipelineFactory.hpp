#pragma once

#include "IMediaPipelineService.hpp"
#include <memory>

namespace luma::client::media::pipeline {

std::unique_ptr<IMediaPipelineService> CreateMediaPipelineService();

} // namespace luma::client::media::pipeline
