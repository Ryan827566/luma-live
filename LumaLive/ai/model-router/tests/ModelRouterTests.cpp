#include "IModelRouter.hpp"
#include <cassert>
int main(){auto r=std::make_shared<luma::ai::providers::ProviderRegistry>();luma::ai::core::ProviderConfig c;c.name="router-test";c.capabilities={luma::ai::core::Capability::Chat};r->Register(std::make_shared<luma::ai::providers::DeterministicProvider>(c));luma::ai::model_router::ModelRouter router(r);assert(router.Route(luma::ai::core::Capability::Chat));assert(!router.Route(luma::ai::core::Capability::Vision));return 0;}
