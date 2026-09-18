#include "luma/domain/studio/ITransformEditor.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
namespace luma::client::domain::studio {
class TransformEditorImpl final : public ITransformEditor { ISceneGraph& graph_; public: explicit TransformEditorImpl(ISceneGraph&g):graph_(g){}
 bool SetPosition(const StudioLayerId&id,double x,double y) override { auto sid=graph_.GetActiveSceneId(); if(!sid)return false; auto l=graph_.GetLayer(*sid,id); if(!l)return false; l->transform.x=x;l->transform.y=y;return graph_.UpdateLayer(*sid,*l); }
 bool SetSize(const StudioLayerId&id,double w,double h) override { auto sid=graph_.GetActiveSceneId(); if(!sid)return false; auto l=graph_.GetLayer(*sid,id); if(!l)return false; if(w<=0||h<=0)return false; l->transform.width=w;l->transform.height=h;return graph_.UpdateLayer(*sid,*l); }
 bool SetRotation(const StudioLayerId&id,double r) override { auto sid=graph_.GetActiveSceneId(); if(!sid)return false; auto l=graph_.GetLayer(*sid,id); if(!l)return false; l->transform.rotation=r;return graph_.UpdateLayer(*sid,*l); }
 bool SetTransform(const StudioLayerId&id,const Transform&t) override { auto sid=graph_.GetActiveSceneId(); if(!sid||t.width<=0||t.height<=0)return false; auto l=graph_.GetLayer(*sid,id); if(!l)return false;l->transform=t;return graph_.UpdateLayer(*sid,*l); }
 std::optional<Transform> GetTransform(const StudioLayerId&id) const override {auto sid=graph_.GetActiveSceneId();if(!sid)return std::nullopt;auto l=graph_.GetLayer(*sid,id);return l?std::optional<Transform>(l->transform):std::nullopt;}
};
std::unique_ptr<ITransformEditor> CreateTransformEditor(ISceneGraph&g){return std::make_unique<TransformEditorImpl>(g);}
}
