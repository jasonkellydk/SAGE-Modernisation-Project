export module engine.level.adapters.model.model_collision;
import std;
import Assets.Models;
import Assets.Math;
export import engine.level.collision.collision_scene;
export import engine.level.model.collision_mesh;
export import Engine.Core.Math.FixedAffineTransform3;

export namespace engine::level {
// Format-neutral, material-independent collision geometry in model space.
// It can be prepared on a loader worker and instanced into a level's BVH.
struct ModelBoxBound {
    std::string name;
    CollisionBox3 bounds;
    Engine::Math::FixedAffineTransform3 transform;
    Engine::Math::FixedVector3 source_center,source_extent;
    std::uint32_t categories{};
    bool aligned{};
};
}
namespace engine::level::model_collision_detail {
using namespace Engine::Math;
inline std::optional<Fixed> Scalar(float value) {
    const auto bits=std::bit_cast<std::uint32_t>(value),exponent=(bits>>23)&0xffu;
    if(exponent>=174) return {}; // Reject nonfinite and out-of-range input; never saturate authored geometry.
    return Fixed::FromBinary32Bits(bits);
}
inline std::optional<FixedVector3> Vector(Assets::Vector3f value) {
    const auto x=Scalar(value.x),y=Scalar(value.y),z=Scalar(value.z);
    if(!x || !y || !z) return {};
    return FixedVector3{*x,*y,*z};
}
inline std::expected<std::vector<FixedAffineTransform3>,std::string> BindTransforms(const Assets::ModelRigDesc& rig) {
    std::string error;if(!Assets::Validate_Model_Rig(rig,error)) return std::unexpected(error);
    std::vector<FixedAffineTransform3> transforms;transforms.reserve(rig.bones.size());
    for(const auto& bone:rig.bones) {
        std::array<Fixed,4> q;
        for(unsigned i=0;i<4;++i) {const auto value=Scalar(bone.rotation[i]);if(!value) return std::unexpected("invalid collision bone rotation");q[i]=*value;}
        const auto t=Vector(bone.translation);if(!t) return std::unexpected("invalid collision bone translation");
        const auto norm=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
        if(norm<=Fixed{}) return std::unexpected("collision bone rotation is below fixed precision");
        const auto s=Fixed::FromInt(2)/norm;const auto x=q[0],y=q[1],z=q[2],w=q[3];
        FixedAffineTransform3 transform{{Fixed::One()-s*(y*y+z*z),s*(x*y-z*w),s*(x*z+y*w),t->x,
            s*(x*y+z*w),Fixed::One()-s*(x*x+z*z),s*(y*z-x*w),t->y,
            s*(x*z-y*w),s*(y*z+x*w),Fixed::One()-s*(x*x+y*y),t->z}};
        if(bone.parent!=Assets::ModelRootParent) transform=transforms.at(bone.parent)*transform;
        transforms.push_back(transform);
    }
    return transforms;
}
inline std::optional<std::uint32_t> PartBone(const Assets::ModelRigDesc& rig,std::string_view name,std::uint32_t lod) {
    if(rig.attachments.empty()) return 0;
    for(const auto& attachment:rig.attachments) {
        const auto dot=attachment.object_name.find('.');const auto short_name=std::string_view(attachment.object_name).substr(dot==std::string::npos ? 0 : dot+1);
        if(attachment.lod==lod && (attachment.object_name==name || short_name==name)) return attachment.bone;
    }
    return {};
}
}
export namespace engine::level {
inline std::expected<std::vector<ModelBoxBound>,std::string> PrepareModelBoxBounds(const Assets::ModelAsset& asset,
    std::optional<std::uint32_t> selected_lod={},std::span<const Engine::Math::FixedAffineTransform3> posed_bones={}) {
    using namespace model_collision_detail;
    const auto& rig=asset.Rig();auto transforms=BindTransforms(rig);if(!transforms) return std::unexpected(transforms.error());
    if(!posed_bones.empty()) {
        if(posed_bones.size()!=rig.bones.size()) return std::unexpected("posed box bone count differs from model rig");
        transforms->assign(posed_bones.begin(),posed_bones.end());
    }
    std::uint32_t lod{};for(const auto& attachment:rig.attachments) lod=std::max(lod,attachment.lod);
    if(selected_lod) lod=*selected_lod;
    std::vector<ModelBoxBound> result;
    for(const auto& box:asset.Collision().boxes) {
        if(box.attached && box.lod!=lod) continue;
        const auto bone=box.attached ? std::optional(box.bone) : PartBone(rig,box.name,lod);if(!bone) continue;
        if(!transforms->empty() && *bone>=transforms->size()) return std::unexpected("model box bone is missing");
        const auto center=Vector(box.center),extent=Vector(box.extent);
        if(!center || !extent || extent->x<Fixed{} || extent->y<Fixed{} || extent->z<Fixed{}) return std::unexpected("invalid model box bounds");
        FixedAffineTransform3 transform;if(!transforms->empty()) transform=transforms->at(*bone);
        const auto& m=transform.elements;
        const FixedVector3 expanded{Abs(m[0])*extent->x+Abs(m[1])*extent->y+Abs(m[2])*extent->z,
            Abs(m[4])*extent->x+Abs(m[5])*extent->y+Abs(m[6])*extent->z,
            Abs(m[8])*extent->x+Abs(m[9])*extent->y+Abs(m[10])*extent->z};
        result.push_back({box.name,{transform.Point(*center),expanded},transform,*center,*extent,box.categories,box.aligned});
    }
    return result;
}
inline std::expected<ModelCollision3,std::string> PrepareModelCollision(const Assets::ModelAsset& asset,
    std::optional<std::uint32_t> selected_lod={},std::span<const Engine::Math::FixedAffineTransform3> posed_bones={}) {
    using namespace model_collision_detail;
    const auto& source=asset.Collision();const auto& rig=asset.Rig();const auto vertices=asset.Vertices();
    if(source.triangles.size()!=source.surfaces.size()) return std::unexpected("collision surface column length differs");
    auto transforms=BindTransforms(rig);if(!transforms) return std::unexpected(transforms.error());
    if(!posed_bones.empty()) {
        if(posed_bones.size()!=rig.bones.size()) return std::unexpected("posed collision bone count differs from model rig");
        transforms->assign(posed_bones.begin(),posed_bones.end());
    }
    // The caller may select a level; otherwise use the highest detail for
    // authoritative collision, independently of presentation's current LOD.
    std::uint32_t lod{};for(const auto& attachment:rig.attachments) lod=std::max(lod,attachment.lod);
    if(selected_lod) lod=*selected_lod;
    ModelCollision3 result;
    for(const auto& part:source.parts) {
        if(part.first_triangle>source.triangles.size() || part.triangle_count>source.triangles.size()-part.first_triangle)
            return std::unexpected("collision part exceeds triangle column");
        if(!part.categories) continue;
        if(part.attached && part.lod!=lod) continue;
        const auto bone=part.attached ? std::optional(part.bone) : PartBone(rig,part.name,lod);if(!bone) continue;
        if(!transforms->empty() && *bone>=transforms->size()) return std::unexpected("collision part bone is missing");
        const auto position=[&](std::uint32_t index)->std::expected<FixedVector3,std::string> {
            if(index>=vertices.size()) return std::unexpected("collision vertex index is missing");
            const auto& vertex=vertices[index];auto point=Vector(vertex.position);
            if(!point) return std::unexpected("collision vertex is not finite or in fixed range");
            if(transforms->empty()) return *point;
            if(!part.skinned) return transforms->at(*bone).Point(*point);
            FixedVector3 skinned;Fixed total;
            for(unsigned influence=0;influence<4;++influence) {
                const auto weight=Scalar(vertex.bone_weights[influence]);
                if(!weight || *weight<Fixed{}) return std::unexpected("invalid collision skin weight");
                if(*weight==Fixed{}) continue;
                if(vertex.bone_indices[influence]>=transforms->size()) return std::unexpected("collision skin bone is missing");
                skinned=skinned+transforms->at(vertex.bone_indices[influence]).Point(*point)* *weight;total+=*weight;
            }
            if(total<=Fixed{}) return std::unexpected("collision skin weights are empty");
            return skinned;
        };
        for(std::size_t i=part.first_triangle;i<part.first_triangle+part.triangle_count;++i) {
            const auto a=position(source.triangles[i][0]),b=position(source.triangles[i][1]),c=position(source.triangles[i][2]);
            if(!a) return std::unexpected(a.error());if(!b) return std::unexpected(b.error());if(!c) return std::unexpected(c.error());
            result.first.push_back(*a);result.second.push_back(*b);result.third.push_back(*c);
            result.categories.push_back(part.categories);result.surfaces.push_back(source.surfaces[i]);
        }
    }
    // Authored collision boxes are physical geometry, even when their render
    // object is hidden. Preserve the exact oriented faces rather than using
    // an expanded world AABB as the narrow-phase shape.
    const auto boxes=PrepareModelBoxBounds(asset,selected_lod,posed_bones);if(!boxes) return std::unexpected(boxes.error());
    constexpr std::array<std::array<unsigned,3>,12> faces{{{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},
        {2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}}};
    for(const auto& box:*boxes) {
        if(!box.categories) continue;
        std::array<FixedVector3,8> points;
        for(unsigned corner=0;corner<8;++corner) points[corner]=box.transform.Point(box.source_center+FixedVector3{
            corner&1 ? box.source_extent.x : -box.source_extent.x,corner&2 ? box.source_extent.y : -box.source_extent.y,corner&4 ? box.source_extent.z : -box.source_extent.z});
        for(const auto& face:faces) {
            result.first.push_back(points[face[0]]);result.second.push_back(points[face[1]]);result.third.push_back(points[face[2]]);
            result.categories.push_back(box.categories);result.surfaces.push_back(0);
        }
    }
    return result;
}
}
