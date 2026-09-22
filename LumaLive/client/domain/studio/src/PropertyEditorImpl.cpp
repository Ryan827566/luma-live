#include "luma/domain/studio/IPropertyEditor.hpp"
#include "luma/domain/studio/ISceneGraph.hpp"
namespace luma::client::domain::studio {
class PropertyEditorImpl final : public IPropertyEditor { ISceneGraph&g_; public: explicit PropertyEditorImpl(ISceneGraph&g):g_(g){}
 bool SetFloat(const StudioLayerId&i,std::string_view p,double v)override{auto s=g_.GetActiveSceneId();if(!s)return false;auto l=g_.GetLayer(*s,i);if(!l)return false;if(p=="x")l->transform.x=v;else if(p=="y")l->transform.y=v;else if(p=="width"&&v>0)l->transform.width=v;else if(p=="height"&&v>0)l->transform.height=v;else if(p=="rotation")l->transform.rotation=v;else return false;return g_.UpdateLayer(*s,*l);}
 bool SetBoolean(const StudioLayerId&i,std::string_view p,bool v)override{auto s=g_.GetActiveSceneId();if(!s)return false;auto l=g_.GetLayer(*s,i);if(!l)return false;if(p=="visible")l->visible=v;else if(p=="locked")l->locked=v;else return false;return g_.UpdateLayer(*s,*l);}
 bool SetString(const StudioLayerId&,std::string_view,std::string_view)override{return false;}
};
std::unique_ptr<IPropertyEditor>CreatePropertyEditor(ISceneGraph&g){return std::make_unique<PropertyEditorImpl>(g);}
}
