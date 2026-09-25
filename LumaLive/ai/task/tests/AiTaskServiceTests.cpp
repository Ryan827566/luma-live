#include "IAiTaskService.hpp"
#include <cassert>
#include <memory>

namespace {
class NullOrchestrator final : public luma::ai::orchestrator::IOrchestrator {
public:
    luma::ai::core::AiResponse Execute(luma::ai::core::AiRequest request) override {
        luma::ai::core::AiResponse response;
        response.request_id = std::move(request.request_id);
        response.success = true;
        response.text = "ok";
        return response;
    }
};
}

int main() {
    using namespace luma::ai;
    auto orchestrator = std::make_shared<NullOrchestrator>();
    task::AiTaskService service(orchestrator);

    core::AiRequest request;
    request.input = "hello";
    const std::string id = service.Submit(request);

    const auto queued = service.Get(id);
    assert(queued);
    assert(queued->status == task::Status::Queued);
    assert(queued->request.request_id == id);

    assert(service.RunNext());

    const auto completed = service.Get(id);
    assert(completed);
    assert(completed->status == task::Status::Succeeded);
    assert(completed->response.success);
    assert(completed->response.text == "ok");
    assert(!service.RunNext());

    assert(!service.Get("missing"));
    return 0;
}
