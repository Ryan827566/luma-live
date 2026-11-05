#pragma once

#include "AiTask.hpp"
#include "IOrchestrator.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace luma::ai::task {

class IAiTaskService {
public:
    virtual ~IAiTaskService() = default;
    virtual std::string Submit(core::AiRequest) = 0;
    virtual bool RunNext() = 0;
    virtual std::shared_ptr<const AiTask> Get(const std::string&) const = 0;
};

class AiTaskService final : public IAiTaskService {
public:
    explicit AiTaskService(std::shared_ptr<orchestrator::IOrchestrator> orchestrator)
        : orchestrator_(std::move(orchestrator)) {}

    std::string Submit(core::AiRequest);
    bool RunNext() override;
    std::shared_ptr<const AiTask> Get(const std::string&) const override;

private:
    std::shared_ptr<orchestrator::IOrchestrator> orchestrator_;
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<AiTask>> queue_;
    std::unordered_map<std::string, std::shared_ptr<AiTask>> tasks_;
    std::uint64_t next_id_{1};
};

} // namespace luma::ai::task
