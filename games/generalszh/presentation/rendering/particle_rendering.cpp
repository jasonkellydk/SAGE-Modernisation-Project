module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

module games.generalszh.presentation.rendering.particle_rendering;

import Graphics.Scene.Particles.Renderer;
import games.generalszh.presentation.effects.heat_haze;
import games.generalszh.presentation.effects.particle_drawing;
import Graphics.Scene.Beams;
import Graphics.Frame.SceneRenderers;
import Graphics.Scene.Views.View;
import Assets.Runtime;
import Assets.Cache;
import Assets.Textures;

namespace generalszh::presentation
{
namespace
{
Graphics::ParticleEmitterFlags FlagsFor(const engine::effects::ParticleSystemDefinition &definition)
{
	using Flags = Graphics::ParticleEmitterFlags;
	Flags flags = Flags::Enabled;
	if (!definition.groundAligned)
		flags = flags | Flags::Billboard;
	switch (definition.shader)
	{
	case engine::effects::ParticleShader::Additive: flags = flags | Flags::Additive; break;
	case engine::effects::ParticleShader::AlphaTest: flags = flags | Flags::AlphaTest; break;
	case engine::effects::ParticleShader::Multiply: flags = flags | Flags::Multiply; break;
	default: break;
	}
	return flags;
}
}

struct ParticleRendering::State
{
	struct Group
	{
		Graphics::ParticleEmitterHandle emitter;
		Graphics::MaterialHandle material;
		Graphics::ParticleEmitterFlags flags{};
		std::vector<std::uint32_t> particles; // this frame's
	};
	std::map<std::string, Graphics::MaterialHandle> materials;
	std::vector<std::pair<Graphics::TextureHandle, Graphics::MaterialHandle>> owned;
	std::map<std::tuple<std::string, std::uint32_t>, Group> groups;
	std::vector<float> x, y, z, zero, ones, size, r, g, b, a, angle;
	std::vector<Graphics::MaterialHandle> materialColumn;
	std::vector<Graphics::ParticleEmitterFlags> flagColumn;
	std::vector<Graphics::PipelineHandle> pipelineColumn;
	std::size_t drawn{0};
	std::size_t field{0};                  // the frame's on-screen ground-aligned AREA_EFFECT particles
	std::vector<float> firstSize;          // by system: its first particle's size (a volume system's layer shift)
	// Streaks (Type = STREAK): a ribbon of beam segments through each system's particles, oldest first.
	std::map<std::string, Graphics::MaterialHandle> beamMaterials;
	std::vector<std::pair<Graphics::TextureHandle, Graphics::MaterialHandle>> ownedBeams;
	std::vector<Graphics::BeamHandle> streakBeams;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> streakParticles; // (system, particle)
	// The snow's emitter (made for its texture and point sprite mode, made again when either changes).
	Graphics::ParticleEmitterHandle snowEmitter;
	Graphics::ParticleEmitterFlags snowFlags{};
	std::string snowTexture;

	// The texture's pixels for a renderer, or none.
	template<typename Renderer>
	static Graphics::TextureHandle Upload(Renderer &renderer, const std::string &texture)
	{
		auto *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr || texture.empty())
			return {};
		const auto handle = cache->Request_Texture(texture);
		cache->Wait(handle);
		const auto *image = cache->Try_Get_Texture(handle);
		if (image == nullptr || !image->Has_Pixels())
			return {};
		Graphics::Texture description;
		const auto pixels = image->Pixels();
		description.width = image->Width();
		description.height = image->Height();
		description.depth = 1;
		description.mip_count = 1;
		description.format = Graphics::TextureFormat::RGBA8_UNorm;
		description.usage = Graphics::TextureUsage::Sampled;
		description.row_pitch = image->Row_Pitch();
		description.pixel_data = pixels;
		return renderer.Create_Texture(description, pixels);
	}

	Graphics::MaterialHandle BeamMaterialFor(const std::string &texture)
	{
		auto &beams = Graphics::GetBeamRenderer();
		if (const auto found = beamMaterials.find(texture); found != beamMaterials.end())
			return found->second;
		Graphics::MaterialHandle material = beams.Default_Material();
		if (const Graphics::TextureHandle uploaded = Upload(beams, texture); uploaded.Is_Valid())
		{
			Graphics::Material m;
			m.shader = beams.Beam_Shader();
			m.textures[0] = uploaded;
			m.parameters.values[0] = m.parameters.values[1] = m.parameters.values[2] = m.parameters.values[3] = 1.0f;
			if (const Graphics::MaterialHandle created = beams.Create_Material(m); created.Is_Valid())
			{
				ownedBeams.emplace_back(uploaded, created);
				material = created;
			}
			else
				beams.Destroy_Texture(uploaded);
		}
		beamMaterials.emplace(texture, material);
		return material;
	}

	~State()
	{
		auto &renderer = Graphics::GetParticleRenderer();
		if (!renderer.Is_Initialized())
			return;
		for (auto &[key, group] : groups)
			renderer.Destroy_Emitter(group.emitter);
		if (snowEmitter.Is_Valid())
			renderer.Destroy_Emitter(snowEmitter);
		for (const auto &[texture, material] : owned)
		{
			renderer.Destroy_Material(material);
			renderer.Destroy_Texture(texture);
		}
		auto &beams = Graphics::GetBeamRenderer();
		if (!beams.Is_Initialized())
			return;
		for (const Graphics::BeamHandle beam : streakBeams)
			beams.Destroy(beam);
		for (const auto &[texture, material] : ownedBeams)
		{
			beams.Destroy_Material(material);
			beams.Destroy_Texture(texture);
		}
	}

	// The texture's material (the renderer's default when it cannot load).
	Graphics::MaterialHandle MaterialFor(const std::string &texture)
	{
		auto &renderer = Graphics::GetParticleRenderer();
		if (const auto found = materials.find(texture); found != materials.end())
			return found->second;
		Graphics::MaterialHandle material = renderer.Default_Material();
		if (auto *cache = Assets::Try_Get_Asset_Cache(); cache != nullptr && !texture.empty())
		{
			const auto handle = cache->Request_Texture(texture);
			cache->Wait(handle);
			if (const auto *image = cache->Try_Get_Texture(handle); image != nullptr && image->Has_Pixels())
			{
				Graphics::Texture description;
				const auto pixels = image->Pixels();
				description.width = image->Width();
				description.height = image->Height();
				description.depth = 1;
				description.mip_count = 1;
				description.format = Graphics::TextureFormat::RGBA8_UNorm;
				description.usage = Graphics::TextureUsage::Sampled;
				description.row_pitch = image->Row_Pitch();
				description.pixel_data = pixels;
				const Graphics::TextureHandle uploaded = renderer.Create_Texture(description, pixels);
				if (uploaded.Is_Valid())
				{
					Graphics::Material m;
					m.shader = renderer.Particle_Shader();
					m.textures[0] = uploaded;
					m.parameters.values[0] = m.parameters.values[1] = m.parameters.values[2] = m.parameters.values[3] = 1.0f;
					const Graphics::MaterialHandle created = renderer.Create_Material(m);
					if (created.Is_Valid())
					{
						owned.emplace_back(uploaded, created);
						material = created;
					}
					else
						renderer.Destroy_Texture(uploaded);
				}
			}
		}
		materials.emplace(texture, material);
		return material;
	}
};

ParticleRendering::ParticleRendering() : m_state(std::make_unique<State>()) {}
ParticleRendering::~ParticleRendering() = default;

std::size_t ParticleRendering::DrawnParticles() const noexcept { return m_state->drawn; }
std::size_t ParticleRendering::FieldParticles() const noexcept { return m_state->field; }
std::size_t ParticleRendering::DrawnStreakSegments() const noexcept { return m_state->streakBeams.size(); }

void ParticleRendering::Draw(Graphics::Device &device, const engine::effects::ParticleWorld &particles, const std::array<float, 16> &view,
	const std::array<float, 16> &projection, const std::array<float, 3> &eye, float alpha, std::span<const BeamSegment> lasers,
	const SnowFlakes *snow, float terrainLowest)
{
	auto &renderer = Graphics::GetParticleRenderer();
	State &state = *m_state;
	state.drawn = 0;
	state.field = 0;
	if (!renderer.Is_Initialized())
		return;
	renderer.Reset_Particles();
	for (auto &[key, group] : state.groups)
		group.particles.clear();

	// W3DParticleSystemManager::doParticles' cull: the box around the terrain the camera can see
	// (getMaximumVisibleBox: the frustum down to the map's lowest height); a particle further from it than its size is
	// not drawn. The drawn particles of ground-aligned AREA_EFFECT systems are its field count, handed to the particle
	// world for its MaxFieldParticleCount. Without terrain (no lowest height): all are drawn, the frustum counts.
	std::array<float, 16> viewProjection{};
	for (std::size_t row = 0; row < 4; ++row)
		for (std::size_t column = 0; column < 4; ++column)
			for (std::size_t k = 0; k < 4; ++k)
				viewProjection[row * 4 + column] += projection[row * 4 + k] * view[k * 4 + column];
	std::optional<VisibleBox> visibleBox;
	if (!std::isnan(terrainLowest))
		if (const auto corners = FrustumCorners(viewProjection))
			visibleBox = MaximumVisibleBox(*corners, terrainLowest);
	Graphics::Matrix4x4 cullViewMatrix, cullProjectionMatrix;
	cullViewMatrix.values = view;
	cullProjectionMatrix.values = projection;
	const Graphics::View cullView(cullViewMatrix, cullProjectionMatrix, {eye[0], eye[1], eye[2]}, Graphics::Viewport{0, 0, 1, 1, 0, 1});
	const auto inView = [&](float x, float y, float z, float radius) {
		if (visibleBox)
			return InVisibleBox(*visibleBox, {x, y, z}, radius);
		for (const Graphics::FrustumPlane *plane : {&cullView.frustum.left, &cullView.frustum.right, &cullView.frustum.bottom, &cullView.frustum.top,
				 &cullView.frustum.near_plane, &cullView.frustum.far_plane})
			if (plane->normal.x * x + plane->normal.y * y + plane->normal.z * z + plane->distance < -radius)
				return false;
		return true;
	};
	const auto systemOf = particles.Systems();
	state.firstSize.clear();

	// Sort this frame's particles by texture and blend.
	for (std::size_t index = 0; index < particles.ParticleCount(); ++index)
	{
		const auto &definition = particles.DefinitionOf(index);
		// Only the original's point-group systems draw here (DrawingOf): drawables never, streaks and heat haze elsewhere;
		// the INI's SMUDGE type draws as particles, as the original never reads it.
		if (DrawingOf(definition) != ParticleDrawing::Quads)
			continue;
		const bool seen = inView(particles.X()[index], particles.Y()[index], particles.Z()[index], particles.Size()[index]);
		if (visibleBox && !seen)
			continue;
		if (CountsAsFieldParticle(definition) && seen)
			++state.field;
		if (VolumeLayers(definition) > 1)
		{
			const std::uint32_t system = systemOf[index];
			if (state.firstSize.size() <= system)
				state.firstSize.resize(system + 1, -1.0f);
			if (state.firstSize[system] < 0.0f)
				state.firstSize[system] = particles.Size()[index];
		}
		const Graphics::ParticleEmitterFlags flags = FlagsFor(definition);
		auto [it, inserted] = state.groups.try_emplace({definition.texture, static_cast<std::uint32_t>(flags)});
		State::Group &group = it->second;
		if (inserted)
		{
			group.material = state.MaterialFor(definition.texture);
			group.flags = flags;
			Graphics::ParticleEmitter emitter;
			emitter.material = group.material;
			emitter.flags = flags;
			emitter.pipeline = renderer.Pipeline_For_Flags(flags);
			emitter.max_particles = 8192;
			group.emitter = renderer.Create_Emitter(emitter);
		}
		group.particles.push_back(static_cast<std::uint32_t>(index));
	}

	for (auto &[key, group] : state.groups)
	{
		std::size_t count = 0;
		for (const std::uint32_t i : group.particles)
			count += VolumeLayers(particles.DefinitionOf(i));
		if (count == 0 || !group.emitter.Is_Valid())
			continue;
		for (auto *column : {&state.x, &state.y, &state.z, &state.zero, &state.ones, &state.size, &state.r, &state.g, &state.b, &state.a, &state.angle})
			column->resize(count);
		state.materialColumn.assign(count, group.material);
		state.flagColumn.assign(count, group.flags);
		state.pipelineColumn.assign(count, renderer.Pipeline_For_Flags(group.flags));
		const bool billboard = Graphics::Has_Particle_Emitter_Flag(group.flags, Graphics::ParticleEmitterFlags::Billboard);
		std::size_t row = 0;
		for (const std::uint32_t i : group.particles)
		{
			const std::array<float, 3> at{particles.X()[i] + particles.MotionX()[i] * alpha, particles.Y()[i] + particles.MotionY()[i] * alpha,
				particles.Z()[i] + particles.MotionZ()[i] * alpha};
			// PointGroupClass::RenderVolumeParticle: a VOLUME_PARTICLE system draws each particle once per layer, each layer
			// nearer the camera (VolumeLayerPosition).
			const std::uint32_t layers = VolumeLayers(particles.DefinitionOf(i));
			const float firstSize = layers > 1 ? state.firstSize[systemOf[i]] : 0.0f;
			for (std::uint32_t layer = 0; layer < layers; ++layer, ++row)
			{
				const std::array<float, 3> placed = VolumeLayerPosition(at, eye, firstSize, layers, layer, billboard);
				state.x[row] = placed[0];
				state.y[row] = placed[1];
				state.z[row] = placed[2];
				state.zero[row] = 0.0f;
				state.ones[row] = 1.0f;
				// Authored billboard sizes are full widths; ground-aligned ones half extents.
				state.size[row] = billboard ? particles.Size()[i] * 0.5f : particles.Size()[i];
				state.r[row] = particles.Red()[i];
				state.g[row] = particles.Green()[i];
				state.b[row] = particles.Blue()[i];
				state.a[row] = particles.Alpha()[i];
				state.angle[row] = particles.Angle()[i];
			}
		}
		const Graphics::ParticleData data{std::span<const float>(state.x), std::span<const float>(state.y), std::span<const float>(state.z),
			std::span<const float>(state.zero), std::span<const float>(state.zero), std::span<const float>(state.zero),
			std::span<const float>(state.ones), std::span<const float>(state.size), std::span<const float>(state.r),
			std::span<const float>(state.g), std::span<const float>(state.b), std::span<const float>(state.a),
			std::span<const float>(state.angle), std::span<const Graphics::MaterialHandle>(state.materialColumn),
			std::span<const Graphics::ParticleEmitterFlags>(state.flagColumn), {},
			std::span<const Graphics::PipelineHandle>(state.pipelineColumn)};
		if (renderer.Append_Particles(group.emitter, data))
			state.drawn += count;
	}
	// The weather's flakes after the particle systems (W3DParticleSystemManager::doParticles: TheSnowManager->render):
	// _PresetAlphaShader, opaque white, each its own size (pixels for point sprites, a quad's half width otherwise).
	if (snow != nullptr && snow->Size() != 0)
	{
		using Flags = Graphics::ParticleEmitterFlags;
		const Flags flags = Flags::Enabled | Flags::Billboard | (snow->pointSprites ? Flags::PointSprite : Flags::None);
		if (!state.snowEmitter.Is_Valid() || state.snowFlags != flags || state.snowTexture != snow->texture)
		{
			if (state.snowEmitter.Is_Valid())
				renderer.Destroy_Emitter(state.snowEmitter);
			Graphics::ParticleEmitter emitter;
			emitter.material = state.MaterialFor(snow->texture);
			emitter.flags = flags;
			emitter.pipeline = renderer.Pipeline_For_Flags(flags);
			emitter.max_particles = 65536;
			state.snowEmitter = renderer.Create_Emitter(emitter);
			state.snowFlags = flags;
			state.snowTexture = snow->texture;
		}
		const std::size_t count = snow->Size();
		if (state.snowEmitter.Is_Valid())
		{
			for (auto *column : {&state.zero, &state.ones, &state.r, &state.g, &state.b, &state.a, &state.angle})
				column->resize(count);
			std::fill(state.zero.begin(), state.zero.end(), 0.0f);
			std::fill(state.angle.begin(), state.angle.end(), 0.0f);
			for (auto *column : {&state.ones, &state.r, &state.g, &state.b, &state.a})
				std::fill(column->begin(), column->end(), 1.0f);
			state.materialColumn.assign(count, state.MaterialFor(snow->texture));
			state.flagColumn.assign(count, flags);
			state.pipelineColumn.assign(count, renderer.Pipeline_For_Flags(flags));
			const Graphics::ParticleData data{std::span<const float>(snow->x), std::span<const float>(snow->y), std::span<const float>(snow->z),
				std::span<const float>(state.zero), std::span<const float>(state.zero), std::span<const float>(state.zero),
				std::span<const float>(state.ones), std::span<const float>(snow->size), std::span<const float>(state.r),
				std::span<const float>(state.g), std::span<const float>(state.b), std::span<const float>(state.a),
				std::span<const float>(state.angle), std::span<const Graphics::MaterialHandle>(state.materialColumn),
				std::span<const Graphics::ParticleEmitterFlags>(state.flagColumn), {},
				std::span<const Graphics::PipelineHandle>(state.pipelineColumn)};
			if (renderer.Append_Particles(state.snowEmitter, data))
				state.drawn += count;
		}
	}
	auto &swapChain = device.Get_Swap_Chain();
	const auto target = swapChain.Backbuffer();
	const auto depth = swapChain.Depth_Target();
	if (state.drawn > 0)
	{
		const Graphics::Viewport viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
		Graphics::Matrix4x4 viewMatrix, projectionMatrix;
		viewMatrix.values = view;
		projectionMatrix.values = projection;
		renderer.Set_View(Graphics::View(viewMatrix, projectionMatrix, {eye[0], eye[1], eye[2]}, viewport));
		renderer.Render(device.Immediate_Command_List(), target.texture, depth.texture,
			{0, 0, target.width, target.height, 0.0f, 1.0f});
	}

	// Streaks: the original's streak systems draw a ribbon through their particles, oldest to newest, each
	// stretch as wide as its particles and shaded from one's colour to the next's (additive ones by colour alone).
	auto &beams = Graphics::GetBeamRenderer();
	if (!beams.Is_Initialized())
		return;
	for (const Graphics::BeamHandle beam : state.streakBeams)
		beams.Destroy(beam);
	state.streakBeams.clear();
	state.streakParticles.clear();
	// Lasers (W3DLaserDraw's lines, additive).
	for (const BeamSegment &laser : lasers)
	{
		Graphics::BeamDescription beam;
		beam.start = {laser.start[0], laser.start[1], laser.start[2]};
		beam.end = {laser.end[0], laser.end[1], laser.end[2]};
		beam.width = laser.width;
		beam.color = {laser.color[0], laser.color[1], laser.color[2], laser.color[3]};
		beam.uv_scale = laser.uvScale;
		beam.uv_offset = laser.uvOffset;
		beam.material = state.BeamMaterialFor(std::string(laser.texture));
		beam.flags = Graphics::BeamFlags::Enabled | (laser.additive ? Graphics::BeamFlags::Additive : Graphics::BeamFlags::None) |
			(laser.depthTest ? Graphics::BeamFlags::None : Graphics::BeamFlags::NoDepthTest);
		beam.pipeline = beams.Pipeline_For_Flags(beam.flags);
		if (const Graphics::BeamHandle handle = beams.Create(beam); handle.Is_Valid())
			state.streakBeams.push_back(handle);
	}
	// doParticles: a streak runs through its system's particles that pass the visible-box cull (StreakParticles).
	state.streakParticles = StreakParticles(particles, visibleBox);
	const auto at = [&](std::uint32_t i) {
		return Graphics::Vec3{particles.X()[i] + particles.MotionX()[i] * alpha, particles.Y()[i] + particles.MotionY()[i] * alpha,
			particles.Z()[i] + particles.MotionZ()[i] * alpha};
	};
	for (std::size_t k = 1; k < state.streakParticles.size(); ++k)
	{
		const auto [system, to] = state.streakParticles[k];
		const auto [previousSystem, from] = state.streakParticles[k - 1];
		if (system != previousSystem)
			continue;
		const auto &definition = particles.DefinitionOf(to);
		const bool additive = definition.shader == engine::effects::ParticleShader::Additive;
		Graphics::BeamDescription beam;
		beam.start = at(from);
		beam.end = at(to);
		beam.width = (particles.Size()[from] + particles.Size()[to]) * 0.5f;
		beam.color_gradient = true;
		beam.start_color = {particles.Red()[from], particles.Green()[from], particles.Blue()[from], additive ? 1.0f : particles.Alpha()[from]};
		beam.end_color = {particles.Red()[to], particles.Green()[to], particles.Blue()[to], additive ? 1.0f : particles.Alpha()[to]};
		beam.material = state.BeamMaterialFor(definition.texture);
		beam.flags = Graphics::BeamFlags::Enabled | (additive ? Graphics::BeamFlags::Additive
			: definition.shader == engine::effects::ParticleShader::Multiply ? Graphics::BeamFlags::Multiply
			: definition.shader == engine::effects::ParticleShader::AlphaTest ? Graphics::BeamFlags::AlphaTest : Graphics::BeamFlags::None);
		beam.pipeline = beams.Pipeline_For_Flags(beam.flags);
		if (const Graphics::BeamHandle handle = beams.Create(beam); handle.Is_Valid())
			state.streakBeams.push_back(handle);
	}
	if (state.streakBeams.empty())
		return;
	// The beams face the camera: its axes from the view matrix (row-major, rows are the camera's axes).
	Graphics::BeamView beamView;
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
		{
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k)
				sum += projection[row * 4 + k] * view[k * 4 + column];
			beamView.view_projection[row * 4 + column] = sum;
		}
	beamView.camera_right = {view[0], view[1], view[2]};
	beamView.camera_up = {view[4], view[5], view[6]};
	beamView.camera_forward = {-view[8], -view[9], -view[10]};
	beams.Set_View(beamView);
	beams.Render(device.Immediate_Command_List(), target.texture, depth.texture, {0, 0, target.width, target.height, 0.0f, 1.0f});
}
}
