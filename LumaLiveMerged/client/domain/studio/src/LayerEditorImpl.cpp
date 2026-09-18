#include "luma/domain/studio/ILayerEditor.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
#include <algorithm>
namespace luma::client::domain::studio {
class LayerEditorImpl final : public ILayerEditor { ISceneGraph& g_; bool update(const StudioSceneId&s,Layer l){return g_.UpdateLayer(s,l);} public: explicit LayerEditorImpl(ISceneGraph&g):g_(g){}
 bool SetVisible(const StudioLayerId&i,bool v)override{auto s=g_.GetActiveSceneId();if(!s)return false;auto l=g_.GetLayer(*s,i);if(!l)return false;l->visible=v;return update(*s,*l);} bool SetLocked(const StudioLayerId&i,bool v)override{auto s=g_.GetActiveSceneId();if(!s)return false;auto l=g_.GetLayer(*s,i);if(!l)return false;l->locked=v;return update(*s,*l);}
 bool BringToFront(const StudioLayerId&i)override{return reorder(i,1'000'000);} bool SendToBack(const StudioLayerId&i)override{return reorder(i,-1'000'000);} bool MoveUp(const StudioLayerId&i)override{return reorder(i,1);} bool MoveDown(const StudioLayerId&i)override{return reorder(i,-1);}
 private: bool reorder(const StudioLayerId&i,int delta){auto s=g_.GetActiveSceneId();if(!s)return false;auto sc=g_.GetScene(*s);if(!sc)return false;auto it=std::find_if(sc->layers.begin(),sc->layers.end(),[&](const Layer&l){return l.id==i;});if(it==sc->layers.end()||it->locked)return false;std::sort(sc->layers.begin(),sc->layers.end(),[](const Layer&a,const Layer&b){return a.zOrder<b.zOrder;});auto pos=std::find_if(sc->layers.begin(),sc->layers.end(),[&](const Layer&l){return l.id==i;});if(delta>100000){auto item=*pos;sc->layers.erase(pos);sc->layers.push_back(item);}else if(delta<-100000){auto item=*pos;sc->layers.erase(pos);sc->layers.insert(sc->layers.begin(),item);}else if(delta>0&&pos+1!=sc->layers.end()){std::iter_swap(pos,pos+1);}else if(delta<0&&pos!=sc->layers.begin()){std::iter_swap(pos,pos-1);}else{return true;}for(size_t n=0;n<sc->layers.size();++n){sc->layers[n].zOrder=static_cast<int>(n);if(!g_.UpdateLayer(*s,sc->layers[n]))return false;}return true;}
};
std::unique_ptr<ILayerEditor> CreateLayerEditor(ISceneGraph&g){return std::make_unique<LayerEditorImpl>(g);}
}
