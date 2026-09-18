#include "IAdapterRegistry.hpp"
#include <algorithm>
namespace luma::adapters { class AdapterRegistry final: public IAdapterRegistry { std::vector<std::string> names_; public: bool Register(std::string n) override { if(Contains(n)) return false; names_.push_back(std::move(n)); return true;} bool Contains(std::string_view n) const override { return std::find(names_.begin(),names_.end(),n)!=names_.end(); } std::vector<std::string> Names() const override{return names_;} }; }
