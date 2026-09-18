#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace luma::client::domain::studio {
using StudioSceneId = std::string;
using StudioLayerId = std::string;
using StudioSourceId = std::string;

enum class SourceType : std::uint8_t { Camera, Screen, Media, Image, Text, Browser, Unknown };

struct Transform final {
    double x{0.0}; double y{0.0}; double width{0.0}; double height{0.0}; double rotation{0.0};
};

struct Source final {
    StudioSourceId id;
    std::string name;
    SourceType type{SourceType::Unknown};
};

struct Layer final {
    StudioLayerId id;
    StudioSourceId sourceId;
    std::string name;
    Transform transform{};
    bool visible{true};
    bool locked{false};
    int zOrder{0};
};

struct Scene final {
    StudioSceneId id;
    std::string name;
    std::vector<Layer> layers;
};
}
