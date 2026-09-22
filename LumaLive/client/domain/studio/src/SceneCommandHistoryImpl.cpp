#include "luma/domain/studio/ISceneCommandHistory.hpp"
#include <vector>
namespace luma::client::domain::studio {
class SceneCommandHistoryImpl final:public ISceneCommandHistory{std::vector<std::unique_ptr<ISceneEditCommand>> done_,redo_;public:
 bool Execute(std::unique_ptr<ISceneEditCommand> c)override{if(!c||!c->Execute())return false;done_.push_back(std::move(c));redo_.clear();return true;}
 bool Undo()override{if(done_.empty())return false;auto c=std::move(done_.back());done_.pop_back();if(!c->Undo()){done_.push_back(std::move(c));return false;}redo_.push_back(std::move(c));return true;}
 bool Redo()override{if(redo_.empty())return false;auto c=std::move(redo_.back());redo_.pop_back();if(!c->Redo()){redo_.push_back(std::move(c));return false;}done_.push_back(std::move(c));return true;}
 bool CanUndo()const override{return !done_.empty();} bool CanRedo()const override{return !redo_.empty();} void Clear()override{done_.clear();redo_.clear();}
};
std::unique_ptr<ISceneCommandHistory>CreateSceneCommandHistory(){return std::make_unique<SceneCommandHistoryImpl>();}
}
