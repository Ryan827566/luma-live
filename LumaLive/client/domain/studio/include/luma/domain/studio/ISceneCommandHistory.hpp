#pragma once
#include "ISceneEditCommand.hpp"
#include <memory>
namespace luma::client::domain::studio {
class ISceneCommandHistory {
public:
    virtual ~ISceneCommandHistory() = default;
    virtual bool Execute(std::unique_ptr<ISceneEditCommand>) = 0;
    virtual bool Undo() = 0;
    virtual bool Redo() = 0;
    virtual bool CanUndo() const = 0;
    virtual bool CanRedo() const = 0;
    virtual void Clear() = 0;
};
std::unique_ptr<ISceneCommandHistory> CreateSceneCommandHistory();
}
