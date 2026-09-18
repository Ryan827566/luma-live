#include "luma/domain/studio/ISceneGraph.hpp"
#include "luma/domain/studio/ISceneCommandHistory.hpp"
#include "luma/domain/studio/ISceneEditor.hpp"
#include <cassert>
int main(){using namespace luma::client::domain::studio;auto g=CreateSceneGraph();Scene s{"scene-1","Main",{}};assert(g->AddScene(s));assert(g->AddLayer("scene-1",Layer{"layer-1","source-1","Camera",{10,20,640,360,0},true,false,0}));auto h=CreateSceneCommandHistory();auto e=CreateSceneEditor(*g,*h);assert(e->BeginEdit());assert(e->SelectLayer("layer-1"));assert(e->MoveSelectedLayer(5,7));auto l=g->GetLayer("scene-1","layer-1");assert(l && l->transform.x==15 && l->transform.y==27);assert(e->Undo());l=g->GetLayer("scene-1","layer-1");assert(l && l->transform.x==10 && l->transform.y==20);assert(e->Redo());return 0;}
