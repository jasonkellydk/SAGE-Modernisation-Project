#include <cmath>
import Graphics.Frame.RenderClock;
import Graphics.Frame.RenderSettings;
#include "W3DDevice/GameClient/W3DRenderServices.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
import Engine.Core.Math.RandomStream;
import Engine.Core.Math.Matrix4;
import Engine.Core.Math.Vector3;
import Graphics.Frame.Runtime;
import Graphics.Materials.Ordering;
import Graphics.Scene.DrawParameters;
import Graphics.Scene.OrderedDraws;
import Graphics.Scene.Props.MaterialSubmission;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Views.CameraMatrices;
#include "W3DDevice/GameClient/W3DEmitterParticles.h"
#include "W3DDevice/GameClient/W3DEmitterRenderObject.h"
#include "W3DDevice/GameClient/W3DCamera.h"
#include "W3DDevice/GameClient/W3DRenderContext.h"
#include "W3DDevice/GameClient/W3DSceneClass.h"
#include "W3DDevice/GameClient/W3DTextureHandle.h"

#include "WWLib/RANDOM.h"
namespace {

Random4Class visual_random;

std::array<float, 1> Sample_Scalar(std::array<float, 1> scale)
{
    return {visual_random * scale[0]};
}

std::array<float, 3> Sample_Color(std::array<float, 3> scale)
{
    const Engine::Math::Vector3 value(visual_random * scale[0], visual_random * scale[1], visual_random * scale[2]);
    return {value.x, value.y, value.z};
}

std::array<float, 3> Acceleration(const Assets::EmitterAssetDesc &description)
{
    const Engine::Math::Vector3 value = Engine::Math::Vector3(description.acceleration.x, description.acceleration.y,
        description.acceleration.z) / 1000000.0f;
    return {value.x, value.y, value.z};
}

std::uint32_t Lifetime(const Assets::EmitterAssetDesc &description)
{
    return static_cast<std::uint32_t>(1000 * (description.lifetime > 0 ? description.lifetime : 1.0f));
}

// Seed of the frozen noise sequence. Like the original fresh Random3Class, the
// same sequence restarts for every chunk so frozen lines never change.
constexpr std::uint64_t Frozen_Line_Noise_Seed = 0;

// Non-frozen noise draws from one persistent stream that advances every
// submit, matching the original shared Vector3Randomizer generator.
Engine::Math::RandomStream &Shared_Line_Noise_Stream() noexcept
{
    static Engine::Math::RandomStream stream(0x11e5eed5ull);
    return stream;
}

struct LineRandom final {
    std::optional<Engine::Math::RandomStream> frozen;
    void Reset(std::size_t)
    {
        frozen.emplace(Frozen_Line_Noise_Seed);
    }
    std::array<float, 3> Next(bool freeze)
    {
        auto &stream = freeze ? *frozen : Shared_Line_Noise_Stream();
        const float x = stream.NextFloat(-1.0f, 1.0f);
        const float y = stream.NextFloat(-1.0f, 1.0f);
        const float z = stream.NextFloat(-1.0f, 1.0f);
        return {x, y, z};
    }
};

}

W3DEmitterParticles::W3DEmitterParticles(const Assets::EmitterAssetDesc &description,
    W3DTextureHandle *texture, Graphics::MaterialState shader, W3DEmitterRenderObject *emitter)
    : m_kinematics(Graphics::Emitter_Buffer_Capacity(description), Lifetime(description),
          Acceleration(description), false, Graphics::Get_Render_Clock().Sync_Time()),
      m_visuals(description, m_kinematics.Capacity(), m_kinematics.Lifetime(), Sample_Scalar, Sample_Color),
      m_renderer(description, shader, m_kinematics.Capacity(), Graphics::Get_Render_Clock().Logic_Time_Milliseconds()),
      m_detail(m_kinematics.Capacity(), description.geometry_mode), m_emitter(emitter)
{
    m_texture.Assign_Add_Ref(texture);
    // Trail endpoints are not part of the retained head-position bounds.
    if (m_visuals.Is_Line_Group())
        Set_Force_Visible(1);
}

W3DEmitterParticles::W3DEmitterParticles(const W3DEmitterParticles &source,
    W3DEmitterRenderObject *emitter)
    : W3DRenderObject(source), m_kinematics(source.m_kinematics, Graphics::Get_Render_Clock().Sync_Time()),
      m_visuals(source.m_visuals), m_renderer(source.m_renderer), m_detail(source.m_detail),
      m_texture(source.m_texture), m_emitter(emitter)
{
}

W3DEmitterParticles::~W3DEmitterParticles() = default;
W3DRenderObject *W3DEmitterParticles::Clone() const { return new W3DEmitterParticles(*this, nullptr); }

void W3DEmitterParticles::Notify_Added(W3DScene *scene)
{
    W3DRenderObject::Notify_Added(scene);
    scene->Register(this, W3DScene::ON_FRAME_UPDATE);
}

void W3DEmitterParticles::Notify_Removed(W3DScene *scene)
{
    scene->Unregister(this, W3DScene::ON_FRAME_UPDATE);
    W3DRenderObject::Notify_Removed(scene);
}

void W3DEmitterParticles::On_Frame_Update()
{
    Invalidate_Cached_Bounding_Volumes();
    if (m_emitter)
        m_emitter->Emit(m_kinematics);
    if (Is_Complete())
        Scene->Register(this, W3DScene::RELEASE);
}

void W3DEmitterParticles::Render(W3DRenderContext &info)
{
    const auto layer = Graphics::Get_Render_Settings().Is_Sorting_Enabled() ? 0
        : Graphics::Material_Ordered_Layer(m_renderer.Shader());
    if (layer != 0 && Graphics::Get_Scene_Draw_Queue().Is_Enabled()) {
        Graphics::Get_Scene_Draw_Queue().Enqueue<Extract_Ordered_Draw>(layer, *this);
        return;
    }
    if (m_kinematics.Advance(Graphics::Get_Render_Clock().Sync_Time(), Get_W3D_Render_Services().Frame_Count()))
        m_bounds_dirty = true;
    if (m_detail.Decimation() < m_detail.Count() - 1)
        m_visuals.Evaluate(m_kinematics, Graphics::Get_Render_Clock().Sync_Time(), Get_W3D_Render_Services().Frame_Count());
    Update_Bounds();
    Submit(info);
}

void W3DEmitterParticles::Submit(W3DRenderContext &info)
{
    auto *device = Graphics::Shared_Frame_Device();
    if (!device)
        return;
    Graphics::EmitterDrawInput input;
    input.projection.values = info.Camera.Build_Render_Matrices().projection;
    input.view = Graphics::Get_Camera_Matrices().view;
    input.world.values = Graphics::Import_Affine_Transform(Get_Transform()).matrix;
    // The original called Matrix3D::Get_Orthogonal_Inverse in place on the
    // camera rotation. Because source and destination aliased, the transpose
    // copied the already-overwritten upper triangle back, leaving a symmetric
    // matrix built from the lower triangle. Reproduce that result exactly.
    const auto camera_transform = info.Camera.Get_Transform();
    const auto &camera = camera_transform.elements;
    const float r00 = camera[0], r10 = camera[4], r11 = camera[5];
    const float r20 = camera[8], r21 = camera[9], r22 = camera[10];
    input.line_rotation = {
        r00, r10, r20,
        r10, r11, r21,
        r20, r21, r22};
    input.scene = Graphics::Get_Scene_Draw_Parameters();
    input.rendered_frame = Get_W3D_Render_Services().Frame_Count();
    input.sync_time = Graphics::Get_Render_Clock().Sync_Time();
    input.logic_time = Graphics::Get_Render_Clock().Logic_Time_Milliseconds();
    input.reflection = Get_W3D_Render_Services().Is_Reflection_Render_Pass();
    input.sorting_enabled = Graphics::Get_Render_Settings().Is_Sorting_Enabled();
    if (m_emitter) {
        const Engine::Math::Vector3 position = m_emitter->Get_Position();
        input.source_position = {position.x, position.y, position.z};
        input.source_group = m_emitter->Group();
        input.source_active = !m_emitter->Is_Stopped();
    }
    const auto resolve = [](W3DTextureHandle *source, bool load)
        -> std::optional<Graphics::PropMaterialTexture> {
        if (load && !source->Ensure_Render_Backend_Texture())
            return std::nullopt;
        return Graphics::PropMaterialTexture{source->Peek_Graphics_Texture(), source->Get_Sampling()};
    };
    LineRandom random;
    m_renderer.Submit(*device, Graphics::Get_Prop_Renderer(), Graphics::Get_Prop_Submission(),
        m_kinematics, m_visuals, m_detail.Decimation(), input, m_texture.Peek(), resolve,
        [&](bool freeze) { return random.Next(freeze); },
        [&](std::size_t chunk) { random.Reset(chunk); });
}

void W3DEmitterParticles::Scale(float scale)
{
    m_visuals.Scale(scale);
    m_renderer.Scale(scale);
    m_kinematics.Scale_Acceleration(scale);
}

void W3DEmitterParticles::Update_Bounds()
{
    if (m_kinematics.Advance(Graphics::Get_Render_Clock().Sync_Time(), Get_W3D_Render_Services().Frame_Count()))
        m_bounds_dirty = true;
    if (!m_bounds_dirty)
        return;
    const auto bounds = m_kinematics.Bounds(m_visuals.Max_Size(), Get_W3D_Render_Services().Frame_Count());
    m_bounds = {{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}};
    m_bounds_dirty = false;
}

void W3DEmitterParticles::Get_Local_Bounding_Sphere(Engine::Math::Sphere3 &sphere) const
{
    const_cast<W3DEmitterParticles *>(this)->Update_Bounds();
    sphere = {m_bounds.Center(), m_bounds.Extent().Length()};
}

void W3DEmitterParticles::Get_Local_Bounds(Engine::Math::AxisAlignedBox3 &box) const
{
    const_cast<W3DEmitterParticles *>(this)->Update_Bounds();
    box = m_bounds;
}

void W3DEmitterParticles::Update_Cached_Bounding_Volumes() const
{
    const_cast<W3DEmitterParticles *>(this)->Update_Bounds();
    const Engine::Math::Vector3 center = m_bounds.Center();
    const Engine::Math::Vector3 extent = m_bounds.Extent();
    CachedBoundingSphere = {center, extent.Length()};
    CachedBoundingBox = {center - extent, center + extent};
    Validate_Cached_Bounding_Volumes();
}

void W3DEmitterParticles::Prepare_LOD(W3DCamera &camera)
{
    if (!Is_Not_Hidden_At_All())
        return;
    Engine::Math::Vector2 minimum, maximum;
    camera.Get_View_Plane(minimum, maximum);
    const auto viewport = camera.Get_Viewport();
    const auto sphere = Get_Bounding_Sphere();
    const auto camera_position = camera.Get_Position();
    const Engine::Math::Vector3 center_offset{sphere.center.x - camera_position.x,
        sphere.center.y - camera_position.y, sphere.center.z - camera_position.z};
    const float distance = std::sqrt(center_offset.x * center_offset.x
        + center_offset.y * center_offset.y + center_offset.z * center_offset.z);
    m_detail.Prepare(distance, sphere.radius, m_visuals.Max_Size(),
        viewport.Width() / (maximum.x - minimum.x), viewport.Height() / (maximum.y - minimum.y));
}

int W3DEmitterParticles::Calculate_Cost_Value_Arrays(float area, float *values, float *costs) const
{
    return m_detail.Calculate(area, std::span(values, m_detail.Count() + 1), std::span(costs, m_detail.Count()));
}
