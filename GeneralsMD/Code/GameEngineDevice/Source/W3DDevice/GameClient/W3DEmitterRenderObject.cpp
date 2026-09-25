import Graphics.Frame.RenderClock;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Quaternion;
#include "W3DDevice/GameClient/W3DEmitterRenderObject.h"
#include "W3DDevice/GameClient/W3DEmitterParticles.h"
#include "W3DDevice/GameClient/W3DSceneClass.h"
#include "W3DDevice/GameClient/W3DTextureHandle.h"

#include <cstdint>

import Engine.Core.Math.RandomStream;

namespace
{
// The original Vector3Randomizers all drew from one process-wide random
// generator, so every emitter instance produced a different particle pattern.
// Each clone therefore gets its own stream seeded from a single shared stream
// (the prototypes keep their stable, name-derived seeds).
Engine::Math::RandomStream &Shared_Emitter_Seed_Stream() noexcept
{
    static Engine::Math::RandomStream stream(0x5eed0e317e7ull);
    return stream;
}

Engine::Math::RandomVector3Generator Reseeded_Generator(
    const Engine::Math::RandomVector3Generator &source) noexcept
{
    auto &shared = Shared_Emitter_Seed_Stream();
    const std::uint64_t high = shared.NextUInt32();
    const std::uint64_t seed = (high << 32u) | shared.NextUInt32();
    return {source.Distribution(), source.Dimensions(), seed};
}
}

W3DEmitterRenderObject::W3DEmitterRenderObject(const Assets::EmitterAssetDesc &description,
    W3DTextureHandle *texture, Graphics::MaterialState shader,
    Engine::Math::RandomVector3Generator position, Engine::Math::RandomVector3Generator velocity)
    : m_emission(description), m_position_generator(std::move(position)),
      m_velocity_generator(std::move(velocity))
{
	m_velocity_generator.Scale(0.001f);
    m_particles.Assign_No_Add_Ref(new W3DEmitterParticles(description, texture, shader, this));
}

W3DEmitterRenderObject::W3DEmitterRenderObject(const W3DEmitterRenderObject &source)
    : W3DRenderObject(source), m_emission(source.m_emission),
      m_position_generator(Reseeded_Generator(source.m_position_generator)),
      m_velocity_generator(Reseeded_Generator(source.m_velocity_generator)),
      m_name(source.m_name), m_active(true), m_invisible(source.m_invisible)
{
    m_emission.Prepare_Clone();
    m_particles.Assign_No_Add_Ref(new W3DEmitterParticles(*source.m_particles.Peek(), this));
}

W3DEmitterRenderObject::~W3DEmitterRenderObject()
{
    m_particles->Emitter_Removed();
}

W3DRenderObject *W3DEmitterRenderObject::Clone() const { return new W3DEmitterRenderObject(*this); }

void W3DEmitterRenderObject::Set_Name(const char *name)
{
    if (name)
        m_name = name;
    else
        m_name.reset();
}

Graphics::EmitterTransform W3DEmitterRenderObject::Current_Transform() const
{
    const auto &transform = Get_Transform();
    Engine::Math::AffineTransform3 rotation_matrix;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column)
            rotation_matrix.elements[row * 4 + column] = transform[row][column];
    const Engine::Math::Quaternion rotation = Engine::Math::Quaternion::From_Rotation(rotation_matrix);
    const auto origin = transform.Translation();
    return {{rotation.x, rotation.y, rotation.z, rotation.w}, {origin.x, origin.y, origin.z}};
}

void W3DEmitterRenderObject::Notify_Added(W3DScene *scene)
{
    W3DRenderObject::Notify_Added(scene);
    scene->Register(this, W3DScene::ON_FRAME_UPDATE);
    if (!m_first)
        m_active = true;
    m_in_scene = true;
}

void W3DEmitterRenderObject::Notify_Removed(W3DScene *scene)
{
    scene->Unregister(this, W3DScene::ON_FRAME_UPDATE);
    W3DRenderObject::Notify_Removed(scene);
    m_active = false;
    m_in_scene = false;
}

void W3DEmitterRenderObject::On_Frame_Update()
{
    if (m_active && !m_emission.Complete() && m_first) {
        if (!Is_In_Scene())
            return;
        m_particles->Add(Scene);
        m_emission.Set_Previous_Transform(Current_Transform());
        m_first = false;
    }
    if (m_emission.Complete() && Is_In_Scene())
        Scene->Register(this, W3DScene::RELEASE);
}

void W3DEmitterRenderObject::Start()
{
    // Transform evaluation can reenter visibility updates.
    m_active = true;
    m_emission.Set_Previous_Transform(Current_Transform());
    m_emission.Start();
}

void W3DEmitterRenderObject::Reset()
{
    m_active = true;
    m_emission.Set_Previous_Transform(Current_Transform());
    m_emission.Reset();
}

void W3DEmitterRenderObject::Emit(Graphics::EmitterKinematics &particles)
{
    const auto current = Current_Transform();
    if (!m_active || m_emission.Complete()) {
        m_emission.Set_Previous_Transform(current);
        return;
    }
    const auto sample = [](Engine::Math::RandomVector3Generator &generator) {
        const auto value = generator.Next();
        return std::array{value.x, value.y, value.z};
    };
    m_emission.Emit(particles, Graphics::Get_Render_Clock().Sync_Delta(), Graphics::Get_Render_Clock().Sync_Time(), current,
        [&] { return sample(m_position_generator); },
        [&] { return sample(m_velocity_generator); });
}

void W3DEmitterRenderObject::Update_Visibility()
{
    if (Is_Not_Hidden_At_All() && !m_invisible && !m_active && m_in_scene)
        Start();
    else if ((!Is_Not_Hidden_At_All() || m_invisible) && m_active)
        Stop();
}

void W3DEmitterRenderObject::Scale(float scale)
{
	m_position_generator.Scale(scale);
	m_velocity_generator.Scale(scale);
    m_emission.Scale(scale);
    m_particles->Scale(scale);
}

void W3DEmitterRenderObject::Set_LOD_Bias(float bias) { m_particles->Set_LOD_Bias(bias); }

void W3DEmitterRenderObject::Get_Local_Bounding_Sphere(Engine::Math::Sphere3 &sphere) const
{
    sphere = {{0, 0, 0}, 0.0f};
}

void W3DEmitterRenderObject::Get_Local_Bounds(Engine::Math::AxisAlignedBox3 &box) const
{
    box = {{0, 0, 0}, {0, 0, 0}};
}

void W3DEmitterRenderObject::Update_Cached_Bounding_Volumes() const
{
    const Engine::Math::Vector3 position = Get_Position();
    CachedBoundingSphere = {position, 0.0f};
    CachedBoundingBox = {position, position};
    Validate_Cached_Bounding_Volumes();
}

void W3DEmitterRenderObject::Add_Dependencies_To_List(DynamicVectorClass<StringClass> &files,
    bool textures_only)
{
    if (const auto *texture = m_particles->Peek_Texture())
        files.Add(texture->Get_Full_Path());
    W3DRenderObject::Add_Dependencies_To_List(files, textures_only);
}
