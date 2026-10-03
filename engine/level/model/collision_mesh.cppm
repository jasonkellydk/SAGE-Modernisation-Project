export module engine.level.model.collision_mesh;
import std;
export import engine.level.collision.collision_scene;
export import Engine.Core.Math.FixedAffineTransform3;

export namespace engine::level {
// Immutable model-space geometry in independent triangle/metadata columns.
// Content adapters may provide sampled poses; simulation uses fixed values.
struct ModelCollision3 {
    std::vector<Engine::Math::FixedVector3> first,second,third;
    std::vector<std::uint32_t> categories,surfaces;
};
struct ModelCollisionTrack3 {std::vector<ModelCollision3> frames;};
inline void ValidateCollisionColumns(const ModelCollision3& model) {
    const auto size=model.first.size();
    if(model.second.size()!=size || model.third.size()!=size || model.categories.size()!=size || model.surfaces.size()!=size)
        throw std::invalid_argument("model collision columns differ in length");
}
inline void AppendModelCollision(CollisionScene3& scene,const ModelCollision3& model,
    const Engine::Math::FixedAffineTransform3& transform,std::uint64_t subject,std::uint32_t category_mask=0xffffffffu) {
    ValidateCollisionColumns(model);
    for(std::size_t i=0;i<model.first.size();++i) if(const auto categories=model.categories[i]&category_mask;categories)
        scene.AddTriangle(transform.Point(model.first[i]),transform.Point(model.second[i]),transform.Point(model.third[i]),subject,categories,model.surfaces[i]);
}
inline ModelCollision3 SampleCollisionTrack(const ModelCollisionTrack3& track,Engine::Math::Fixed frame) {
    using namespace Engine::Math;
    if(track.frames.empty()) throw std::invalid_argument("empty collision track");
    frame=std::clamp(frame,Fixed{},Fixed::FromInt(track.frames.size()-1));
    const auto index=std::size_t(frame.Raw()/Fixed::OneRaw);const auto& first=track.frames[index];ValidateCollisionColumns(first);
    if(index+1==track.frames.size() || frame.Raw()%Fixed::OneRaw==0) return first;
    const auto& second=track.frames[index+1];ValidateCollisionColumns(second);
    if(first.first.size()!=second.first.size() || first.categories!=second.categories || first.surfaces!=second.surfaces)
        throw std::invalid_argument("collision track topology changes between samples");
    const auto fraction=Fixed::FromRaw(frame.Raw()%Fixed::OneRaw);auto result=first;
    for(std::size_t i=0;i<first.first.size();++i) {
        result.first[i]=first.first[i]+(second.first[i]-first.first[i])*fraction;
        result.second[i]=first.second[i]+(second.second[i]-first.second[i])*fraction;
        result.third[i]=first.third[i]+(second.third[i]-first.third[i])*fraction;
    }
    return result;
}
}
