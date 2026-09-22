#include "IAiProviderService.hpp"

namespace luma::ai::providers {

class AiProviderService final : public IAiProviderService {
public:
    OperationResult Start() override { running_=true; return {true, "started"}; }
    OperationResult Stop() override { running_=false; return {true, "stopped"}; }
    bool IsRunning() const override { return running_; }
    OperationResult Execute(std::string_view operation) override {
        if (operation.empty()) return {false, "operation is empty"};
        lastOperation_=std::string(operation);
        return {true, lastOperation_};
    }
private:
    bool running_{false};
    std::string lastOperation_;
};

}
