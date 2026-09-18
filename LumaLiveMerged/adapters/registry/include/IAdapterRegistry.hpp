#pragma once
#include <string>
#include <vector>
namespace luma::adapters { class IAdapterRegistry { public: virtual ~IAdapterRegistry()=default; virtual bool Register(std::string name)=0; virtual bool Contains(std::string_view name) const=0; virtual std::vector<std::string> Names() const=0; }; }
