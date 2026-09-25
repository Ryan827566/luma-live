#include "IAiTaskService.hpp"

#include <chrono>
#include <utility>

namespace luma::ai::task {

std::string AiTaskService::Submit(core::AiRequest request) {
    std::lock_guard lock(mutex_);
    auto task = std::make_shared<AiTask>();
    task->id = "ai-task-" + std::to_string(next_id_++);
    task->request = std::move(request);
    task->request.request_id = task->id;
    task->created_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    tasks_[task->id] = task;
    queue_.push_back(task);
    return task->id;
}

bool AiTaskService::RunNext() {
    std::shared_ptr<AiTask> task;
    {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return false;
        task = queue_.front();
        queue_.pop_front();
        task->status = Status::Running;
    }

    core::AiResponse response;
    if (!orchestrator_) {
        response.request_id = task->request.request_id;
        response.error = "orchestrator is unavailable";
    } else {
        response = orchestrator_->Execute(task->request);
    }

    {
        std::lock_guard lock(mutex_);
        task->response = std::move(response);
        task->status = task->response.success ? Status::Succeeded : Status::Failed;
    }
    return true;
}

std::shared_ptr<const AiTask> AiTaskService::Get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) return nullptr;

    // Return an immutable snapshot so callers cannot observe concurrent mutation
    // of the task stored inside the service.
    return std::make_shared<const AiTask>(*it->second);
}

} // namespace luma::ai::task
