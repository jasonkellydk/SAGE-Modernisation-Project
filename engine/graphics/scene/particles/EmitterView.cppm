export module Graphics.Scene.Particles.EmitterView;
import std;
import Assets.Particles;
import Assets.Math;
import Graphics.Materials.State;
import Graphics.RHI;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Props.MaterialSubmission;
import Graphics.Scene.Particles.EmitterKinematics;
export import Graphics.Scene.Particles.EmitterRenderer;
export import Graphics.Scene.Particles.EmitterEmission;
export import Graphics.Scene.Particles.EmitterVisualState;
import Engine.Core.Math.RandomVector3Generator;
import Engine.Core.Math.RandomStream;
import Engine.Core.Math.Quaternion;
import Graphics.Scene.AffineTransform;
export namespace Graphics {
inline MaterialState Emitter_Material(const Assets::EmitterAssetDesc& description,bool texture_has_alpha=true) {
    const auto& authored=description.shader;MaterialState material;
    material.Set_Depth_Compare(static_cast<MaterialState::DepthCompareType>(authored.depth_compare));
    material.Set_Depth_Mask(authored.depth_write ? MaterialState::DEPTH_WRITE_ENABLE : MaterialState::DEPTH_WRITE_DISABLE);
    material.Set_Dst_Blend_Func(static_cast<MaterialState::DstBlendFuncType>(authored.destination_blend));
    using B=Assets::EmitterBlendFactor;
    switch(authored.source_blend) {
    case B::Zero:material.Set_Src_Blend_Func(MaterialState::SRCBLEND_ZERO);break;
    case B::SourceAlpha:material.Set_Src_Blend_Func(MaterialState::SRCBLEND_SRC_ALPHA);break;
    case B::OneMinusSourceAlpha:material.Set_Src_Blend_Func(MaterialState::SRCBLEND_ONE_MINUS_SRC_ALPHA);break;
    default:material.Set_Src_Blend_Func(MaterialState::SRCBLEND_ONE);break;
    }
    material.Set_Alpha_Test(authored.alpha_test ? MaterialState::ALPHATEST_ENABLE : MaterialState::ALPHATEST_DISABLE);
    material.Set_Primary_Gradient(static_cast<MaterialState::PriGradientType>(authored.primary_gradient));
    material.Set_Secondary_Gradient(static_cast<MaterialState::SecGradientType>(authored.secondary_gradient));
    material.Set_Texturing(authored.texturing ? MaterialState::TEXTURING_ENABLE : MaterialState::TEXTURING_DISABLE);
    material.Set_Cull_Mode(MaterialState::CULL_MODE_DISABLE);
    if(description.texture_blend_policy==Assets::EmitterTextureBlendPolicy::AdditiveSprite) material=MaterialState::AdditiveSprite();
    else if(description.texture_blend_policy==Assets::EmitterTextureBlendPolicy::AlphaSpriteWhenTextureHasAlpha) material=texture_has_alpha ? MaterialState::AlphaSprite() : MaterialState::AdditiveSprite();
    return material;
}
// Presentation owner composed from the same particle SoA, authored tracks,
// emission and renderer used by the shared W3D regressions. Simulation time
// is injected: a paused session draws its existing particles without aging.
class EmitterView {
public:
    explicit EmitterView(const Assets::EmitterAssetDesc& description,std::uint64_t seed=0,bool texture_has_alpha=true)
        :m_description(description),m_random(seed),m_position(Randomizer(description.creation_volume,seed+1)),m_velocity(Randomizer(description.velocity_random,seed+2,.001f)),
        m_particles(Emitter_Buffer_Capacity(description),Lifetime(description),{description.acceleration.x*.000001f,description.acceleration.y*.000001f,description.acceleration.z*.000001f},false,0),
        m_emission(description),m_visuals(description,m_particles.Capacity(),m_particles.Lifetime(),[this](auto scale) {return Sample(scale);},[this](auto scale) {return Sample(scale);}),
        m_renderer(description,Emitter_Material(description,texture_has_alpha),m_particles.Capacity(),0) {}
    bool Advance(std::uint32_t time,const RenderTransform& world,bool active) {
        Engine::Math::AffineTransform3 affine;std::copy_n(world.matrix.begin(),12,affine.elements.begin());const auto rotation=Engine::Math::Quaternion::From_Rotation(affine);
        EmitterTransform transform{{rotation.x,rotation.y,rotation.z,rotation.w},{world.matrix[3],world.matrix[7],world.matrix[11]}};
        if(!m_initialized) {m_initialized=true;m_time=time;m_emission.Set_Previous_Transform(transform);}
        if(time<m_time) return false;const auto delta=time-m_time;
        if(active && !m_active) {m_emission.Start();m_emission.Set_Previous_Transform(transform);}m_active=active;
        if(delta && active && !m_emission.Complete() && !m_emission.Emit(m_particles,delta,time,transform,[this] {return Vector(m_position.Next());},[this] {return Vector(m_velocity.Next());})) return false;
        m_particles.Advance(time,0);m_visuals.Evaluate(m_particles,time,0);m_time=time;return true;
    }
    std::uint32_t Count() const noexcept {return m_particles.Count();}
    template<class Inspect> void ForEachParticle(Inspect inspect) const {
        const auto positions=m_particles.Positions(0);
        for(const auto range:m_particles.Active_Ranges()) for(auto slot=range.begin;slot<range.end;++slot)
            inspect(positions[slot],m_visuals.Color(slot),m_visuals.Size(slot));
    }
    bool Submit(Device& device,PropRenderer& renderer,PropSubmission& submission,EmitterDrawInput input,RHITextureHandle texture) {
        input.logic_time=m_time;input.rendered_frame=0;input.source_active=m_active;input.source_position=m_emission.Previous_Transform().origin;input.source_group=m_emission.Group();
        const auto resolve=[](const RHITextureHandle* handle,bool)->std::optional<PropMaterialTexture> {return handle && handle->Is_Valid() ? std::optional<PropMaterialTexture>{{*handle,{}}} : std::nullopt;};
        return m_renderer.Submit(device,renderer,submission,m_particles,m_visuals,0,input,texture.Is_Valid() ? &texture : nullptr,resolve,
            [this](bool) {return std::array{m_random.NextFloat(-1,1),m_random.NextFloat(-1,1),m_random.NextFloat(-1,1)};},[this](std::size_t seed) {m_random=Engine::Math::RandomStream(seed);});
    }
private:
    static std::uint32_t Lifetime(const Assets::EmitterAssetDesc& description) {return std::uint32_t((description.lifetime>0 ? description.lifetime : 1.f)*1000.f);}
    static Engine::Math::RandomVector3Generator Randomizer(const Assets::EmitterRandomizerDesc& description,std::uint64_t seed,float scale=1) {
        using K=Assets::EmitterRandomizerKind;using D=Engine::Math::Vector3Distribution;D kind=D::Box;
        switch(description.kind) {case K::SolidSphere:kind=D::SolidSphere;break;case K::HollowSphere:kind=D::SphereSurface;break;case K::SolidCylinder:kind=D::Cylinder;break;default:break;}
        // W3D particle kinematics use milliseconds. Authored creation volumes
        // remain distances; authored random velocities are distances/second.
        return {kind,{description.dimensions.x*scale,description.dimensions.y*scale,description.dimensions.z*scale},seed};
    }
    static std::array<float,3> Vector(Engine::Math::Vector3 value) {return {value.x,value.y,value.z};}
    template<std::size_t N> std::array<float,N> Sample(std::array<float,N> scale) {for(auto& value:scale) value*=float(std::bit_cast<std::int32_t>(m_random.NextUInt32()));return scale;}
    Assets::EmitterAssetDesc m_description;Engine::Math::RandomStream m_random;Engine::Math::RandomVector3Generator m_position,m_velocity;
    EmitterKinematics m_particles;EmitterEmission m_emission;EmitterVisualState m_visuals;EmitterRenderer m_renderer;
    std::uint32_t m_time{};bool m_initialized{},m_active{};
};
}
