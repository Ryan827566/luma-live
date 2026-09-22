#pragma once
#include <memory>
#include <string>
namespace luma::client::domain::studio {
class ISceneEditCommand {
public:
    virtual ~ISceneEditCommand() = default;
    virtual bool Execute() = 0;
    virtual bool Undo() = 0;
    virtual bool Redo() { return Execute(); }
    virtual std::string GetName() const = 0;
};
}
