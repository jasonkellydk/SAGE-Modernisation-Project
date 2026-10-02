module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module games.generalszh.presentation.rendering.object_rendering;

import Engine.Core.Math.FixedPresentation;
import Assets.Runtime;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import Assets.Models;
import Assets.States;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Lighting.Local;
import Graphics.Scene.Props.LightingParameters;
import games.generalszh.presentation.rendering.infantry_lighting;

namespace generalszh::presentation
{
struct ObjectRendering::State
{
	// Per library entry: its GPU binding (made once the entry is ready), or none yet; whether it failed to bind.
	std::vector<std::unique_ptr<Graphics::PropAssetBinding>> bindings;
	std::vector<std::uint8_t> failed;
	std::string failures;

	// The look's model, bound for drawing: its entry and binding, or none while it loads, when it failed or draws nothing.
	std::pair<ModelEntry *, Graphics::PropAssetBinding *> Bound(Graphics::Device &device, Assets::AssetCache &cache, ModelLibrary &library, std::uint32_t look)
	{
		if (look >= library.entryOfLook.size())
			return {};
		const std::uint32_t entry = library.entryOfLook[look];
		if (entry >= library.entries.size() || library.loads[entry].status != ModelStatus::Ready)
			return {};
		if (entry >= bindings.size())
		{
			bindings.resize(entry + 1);
			failed.resize(entry + 1, 0);
		}
		if (!bindings[entry] && failed[entry] == 0)
		{
			const ModelLoad &load = library.loads[entry];
			// A replacement texture that failed to load leaves the model's own.
			const Assets::TextureAssetHandle replacement =
				load.texture.Is_Valid() && cache.Get_State(load.texture) == Assets::AssetState::Ready ? load.texture : Assets::TextureAssetHandle{};
			auto binding = std::make_unique<Graphics::PropAssetBinding>();
			std::string error;
			if (binding->Load(device, Graphics::Get_Prop_Renderer(), cache, load.handle, error, replacement) &&
				binding->Part_Count() == library.entries[entry].partNames.size())
				bindings[entry] = std::move(binding);
			else
			{
				failed[entry] = 1;
				failures += "  " + library.keys[entry] + ": " + (error.empty() ? std::string("its parts do not match its entry") : error) + "\n";
			}
		}
		return bindings[entry] ? std::pair{&library.entries[entry], bindings[entry].get()} : std::pair<ModelEntry *, Graphics::PropAssetBinding *>{};
	}

	// Its model's pose for the instance (the free InstancePose); a clip that cannot be sampled is reported once.
	const Graphics::ModelAssetPose *InstancePose(ModelEntry &model, const ObjectInstance &instance)
	{
		bool broken = false;
		const Graphics::ModelAssetPose *pose = presentation::InstancePose(model, instance, broken);
		if (broken)
			failures += "  " + model.request.name + " (animation): pose sampling failed\n";
		return pose;
	}
};

ObjectRendering::ObjectRendering() : m_state(std::make_unique<State>()) {}
ObjectRendering::~ObjectRendering() = default;

const std::string &ObjectRendering::Failures() const noexcept { return m_state->failures; }

void ObjectRendering::Preload(Graphics::Device &device, std::span<const ObjectInstance> instances, ModelLibrary &library)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	for (const ObjectInstance &instance : instances)
		m_state->Bound(device, *cache, library, instance.look);
}

void ObjectRendering::Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection,
	const std::array<float, 3> &eye, const engine::level::LightingSet *lighting, std::span<const ObjectInstance> instances,
	ModelLibrary &library, std::span<const ShownLight> lights, const ShroudBinding *shroud, float infantryLightScale, const OcclusionPlan *occlusion)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	// W3DShroudMaterialPassClass: objects multiplied by the viewer's shroud as the terrain is.
	const Graphics::RHITextureHandle shroudTexture = shroud != nullptr && shroud->Active() ? shroud->texture : Graphics::RHITextureHandle{};
	if (cache == nullptr)
		return;
	State &state = *m_state;

	Graphics::PropParameters parameters;
	parameters.view_projection = viewProjection;
	parameters.camera_position = {eye[0], eye[1], eye[2], 1};
	if (shroudTexture.Is_Valid())
	{
		parameters.shroud = 1;
		parameters.shroud_projection = shroud->projection;
	}
	if (lighting != nullptr && !lighting->objects.empty())
	{
		const auto &ambient = lighting->objects[0].ambient;
		parameters.scene_ambient = {Engine::Math::ToFloat(ambient[0]), Engine::Math::ToFloat(ambient[1]), Engine::Math::ToFloat(ambient[2]), 1};
		// The original applies GameData NumberGlobalLights (3) global lights.
		for (std::size_t index = 0; index < lighting->objects.size() && index < 3; ++index)
		{
			const auto &light = lighting->objects[index];
			// Map lights store the direction they shine; the shader takes the direction towards the light. w enables it.
			parameters.light_direction[index] = {-Engine::Math::ToFloat(light.direction.x), -Engine::Math::ToFloat(light.direction.y),
				-Engine::Math::ToFloat(light.direction.z), 1};
			parameters.light_diffuse[index] = {Engine::Math::ToFloat(light.diffuse[0]), Engine::Math::ToFloat(light.diffuse[1]),
				Engine::Math::ToFloat(light.diffuse[2]), 1};
		}
	}
	// W3DScene's per-object light environment: the scene ambient and global lights, then (if it takes them) each enabled
	// dynamic light whose sphere reaches its own; the four strongest light it (LocalLighting, Set_Prop_Lighting).
	std::array<float, 3> ambient{};
	std::vector<Graphics::MaterialLightSource> globals;
	// RTS3DScene::updateFixedLightEnvironments: infantry's copies of the global lights (their diffuse scaled and clamped;
	// the scene's ambient as it is).
	std::vector<Graphics::MaterialLightSource> infantryGlobals;
	if (lighting != nullptr && !lighting->objects.empty())
	{
		const auto &first = lighting->objects[0].ambient;
		ambient = {Engine::Math::ToFloat(first[0]), Engine::Math::ToFloat(first[1]), Engine::Math::ToFloat(first[2])};
		for (std::size_t index = 0; index < lighting->objects.size() && index < 3; ++index)
		{
			const auto &light = lighting->objects[index];
			Graphics::MaterialLightSource source;
			source.type = Graphics::RenderLightType::Directional;
			// Map lights store the direction they shine; the shader takes the direction towards the light.
			source.direction = {-Engine::Math::ToFloat(light.direction.x), -Engine::Math::ToFloat(light.direction.y), -Engine::Math::ToFloat(light.direction.z)};
			source.diffuse = {Engine::Math::ToFloat(light.diffuse[0]), Engine::Math::ToFloat(light.diffuse[1]), Engine::Math::ToFloat(light.diffuse[2])};
			globals.push_back(source);
			const std::array<float, 3> scaled = InfantryLightColor({source.diffuse[0], source.diffuse[1], source.diffuse[2]}, infantryLightScale);
			source.diffuse = {scaled[0], scaled[1], scaled[2]};
			infantryGlobals.push_back(source);
		}
	}
	const auto drawOne = [&](const ObjectInstance &instance, const Graphics::RHIStencilDescription *stencil) {
		const auto [model, binding] = state.Bound(device, *cache, library, instance.look);
		if (model == nullptr)
			return;
		if (lighting != nullptr)
		{
			Graphics::LocalLighting environment;
			const std::array<float, 3> centre{instance.lightSphere[0], instance.lightSphere[1], instance.lightSphere[2]};
			environment.Reset(centre, ambient);
			for (const Graphics::MaterialLightSource &source : instance.infantry ? infantryGlobals : globals)
				environment.Add(source);
			if (instance.receivesDynamicLights)
				for (const ShownLight &light : lights)
				{
					if (!LightReaches(light, instance.lightSphere))
						continue;
					Graphics::MaterialLightSource source;
					source.type = Graphics::RenderLightType::Point;
					source.position = light.at;
					source.ambient = light.ambient;
					source.diffuse = light.diffuse;
					source.attenuate = true;
					source.attenuation_start = light.nearRange;
					source.attenuation_end = light.farRange;
					environment.Add(source);
				}
			environment.Finalize();
			Graphics::Set_Prop_Lighting(parameters, &environment);
			// Darkened (a pushed-aside tree: the tree shader's colour * sway.y): its lights scaled, set afresh per object.
			if (instance.shade != 1.0f)
			{
				for (std::size_t channel = 0; channel < 3; ++channel)
					parameters.scene_ambient[channel] *= instance.shade;
				for (std::size_t index = 0; index < parameters.light_diffuse.size(); ++index)
					for (std::size_t channel = 0; channel < 3; ++channel)
					{
						parameters.light_diffuse[index][channel] *= instance.shade;
						parameters.light_ambient[index][channel] *= instance.shade;
					}
			}
		}
		const Graphics::ModelAssetPose *pose = state.InstancePose(*model, instance);
		parameters.world = instance.world;
		parameters.surface.team_color = instance.teamColor;
		// The original's effective opacity: an override of the model's own.
		parameters.vertex_material_override = {instance.opacity, 1, 0, instance.opacity < 1.0f ? 1.0f : 0.0f};
		for (std::size_t part = 0; part < binding->Part_Count(); ++part)
		{
			if (part >= model->partNames.size() || !PartShown(*model, part, instance))
				continue;
			// Treads roll their texture along U (stage 0); everything else as authored.
			const std::uint8_t tread = model->partTread[part];
			parameters.uv_transform[0][3] = tread == 0 ? 0.0f : instance.treads[tread - 1];
			if (!instance.heatOnly)
			{
				if (instance.opacity < 1.0f)
					binding->Draw_Part_Translucent(commands, part, parameters, instance.opacity, pose, 0, shroudTexture, stencil);
				else
					binding->Draw_Part(commands, part, parameters, pose, 0, shroudTexture, stencil);
			}
			// W3DScene's heat vision pass (m_heatVisionMaterialPass / m_heatVisionOnlyPass): additive, lit with diffuse
			// (0.02, 0.01, 0) and emissive (0.5, 0.2, 0) scaled by its strength; over its own drawing (depth equal), or
			// alone for an enemy (depth less-equal, no depth write).
			// Drawable::colorTint (an illegal placement's red): its colour added over it, lit as emissive.
			if (instance.tint[0] > 0.0f || instance.tint[1] > 0.0f || instance.tint[2] > 0.0f)
			{
				Graphics::PropParameters tinted = parameters;
				tinted.material_diffuse_replacement = {0.0f, 0.0f, 0.0f, 1.0f};
				tinted.material_emissive_replacement = {instance.tint[0], instance.tint[1], instance.tint[2], 1.0f};
				tinted.vertex_material_override = {1.0f, instance.opacity, 0.0f, 0.0f};
				Graphics::PropStyle pass;
				pass.source_blend = Graphics::RHIBlendFactor::One;
				pass.destination_blend = Graphics::RHIBlendFactor::One;
				pass.depth_write = false;
				pass.depth_comparison = Graphics::RHIComparison::LessEqual;
				binding->Draw_Part_Pass(commands, part, tinted, pass, pose, 0, shroudTexture);
			}
			if (instance.heatVision > 0.0f)
			{
				Graphics::PropParameters heat = parameters;
				heat.material_diffuse_replacement = {0.02f, 0.01f, 0.0f, 1.0f};
				heat.material_emissive_replacement = {0.5f, 0.2f, 0.0f, 1.0f};
				heat.vertex_material_override = {1.0f, instance.heatVision, 0.0f, 0.0f};
				Graphics::PropStyle pass;
				pass.source_blend = Graphics::RHIBlendFactor::One;
				pass.destination_blend = Graphics::RHIBlendFactor::One;
				pass.depth_write = false;
				pass.depth_comparison = instance.heatOnly ? Graphics::RHIComparison::LessEqual : Graphics::RHIComparison::Equal;
				binding->Draw_Part_Pass(commands, part, heat, pass, pose, 0, shroudTexture);
			}
		}
		parameters.uv_transform[0][3] = 0.0f;
	};
	if (occlusion == nullptr || !occlusion->active)
	{
		for (const ObjectInstance &instance : instances)
			drawOne(instance, nullptr);
		return;
	}
	// RTS3DScene::flushOccludedObjectsIntoStencil: the occludees store their player's colour index (Always, Replace); the
	// occluders set the top bit where they draw in front (reference 0xFF, write mask 0x80).
	for (const OcclusionPlan::Entry &entry : occlusion->order)
	{
		if (entry.instance >= instances.size())
			continue;
		if (entry.group == OcclusionPlan::Group::Plain)
		{
			drawOne(instances[entry.instance], nullptr);
			continue;
		}
		Graphics::RHIStencilDescription stencil;
		stencil.enabled = true;
		stencil.read_mask = 0xFF;
		stencil.write_mask = entry.group == OcclusionPlan::Group::Occluder ? 0x80 : 0xFF;
		stencil.reference = entry.reference;
		stencil.front.comparison = Graphics::RHIComparison::Always;
		stencil.front.fail = Graphics::RHIStencilOperation::Keep;
		stencil.front.depth_fail = Graphics::RHIStencilOperation::Keep;
		stencil.front.pass = Graphics::RHIStencilOperation::Replace;
		stencil.back = stencil.front;
		drawOne(instances[entry.instance], &stencil);
	}
}

// W3DDirectionalShadows' Collect_Object: each instance that casts a shadow, its visible parts (not muzzle flashes or
// unlit headlights) as directional shadow casters in its pose.
void ObjectRendering::SubmitShadows(Graphics::Device &device, std::span<const ObjectInstance> instances, ModelLibrary &library)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	State &state = *m_state;
	auto &submission = Graphics::Get_Prop_Submission();
	for (const ObjectInstance &instance : instances)
	{
		if (!instance.castsShadow || instance.opacity <= 0.0f)
			continue;
		const auto [model, binding] = state.Bound(device, *cache, library, instance.look);
		if (model == nullptr)
			continue;
		const Graphics::ModelAssetPose *pose = state.InstancePose(*model, instance);
		Graphics::PropParameters parameters;
		parameters.world = instance.world;
		for (std::size_t part = 0; part < binding->Part_Count(); ++part)
		{
			if (part >= model->partNames.size() || !PartCastsShadow(*model, part, instance))
				continue;
			binding->Submit_Part(submission, part, parameters, Graphics::PropDrawPhase::Shadow, {}, pose);
		}
	}
}
}
