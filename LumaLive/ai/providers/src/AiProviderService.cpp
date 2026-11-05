#include "IAiProviderService.hpp"
namespace luma::ai::providers {
class AiProviderService final:public IAiProviderService{bool running_{};std::string last_;
public:OperationResult Start()override{running_=true;return{true,"started"};}OperationResult Stop()override{running_=false;return{true,"stopped"};}bool IsRunning()const override{return running_;}OperationResult Execute(std::string_view op)override{if(!running_)return{false,"service is stopped"};if(op.empty())return{false,"operation is empty"};last_=std::string(op);return{true,last_};}};
}
