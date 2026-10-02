module;
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module games.generalszh.presentation.rendering.sky_box_rendering;

import engine.config.binding.schema;
import games.generalszh.content.water.water_settings;
import Assets.Runtime;
import Assets.Cache;
import Assets.Handles;
import Assets.States;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.Props.Renderer;

namespace generalszh::presentation
{
namespace
{
// "Maps/X/X.map" -> "Maps/X/map" (the map's map.ini content set).
std::string SkyMapIniSet(std::string_view mapPath)
{
	const auto slash = mapPath.find_last_of("/\\");
	if (slash == std::string_view::npos)
		return "map";
	return std::string(mapPath.substr(0, slash + 1)) + "map";
}

std::array<std::string, 5> Faces(const content::WaterTransparencyDefinition &transparency)
{
	return {transparency.skyboxTextureN, transparency.skyboxTextureE, transparency.skyboxTextureS, transparency.skyboxTextureW, transparency.skyboxTextureT};
}
}

struct SkyBoxRendering::State
{
	SkyBoxSetting setting;
	Assets::ModelAssetHandle model;
	std::vector<std::pair<Assets::TextureAssetHandle, Assets::TextureAssetHandle>> replacements;
	std::unique_ptr<Graphics::PropAssetBinding> binding;
	bool failed{false};
};

SkyBoxRendering::SkyBoxRendering() : m_state(std::make_unique<State>()) {}
SkyBoxRendering::~SkyBoxRendering() = default;

const SkyBoxSetting &SkyBoxRendering::Setting() const noexcept { return m_state->setting; }

void SkyBoxRendering::Load(content::ContentLoader &loader, std::string_view mapPath, float scale, float positionZ)
{
	State &state = *m_state;
	state = State{};
	content::WaterSettings settings;
	{
		const auto &document = loader.Load({"Data/INI/Default/Water", "Data/INI/Water"});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		content::BindWaterSettings(document, settings, context);
	}
	state.setting.textures = Faces(settings.transparency);
	if (!mapPath.empty())
	{
		const auto &document = loader.Load({SkyMapIniSet(mapPath)});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		content::BindWaterSettings(document, settings, context);
	}
	state.setting.mapTextures = Faces(settings.transparency);
	state.setting.scale = scale;
	state.setting.positionZ = positionZ;
}

void SkyBoxRendering::Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection,
	const std::array<float, 3> &eye, bool shown)
{
	State &state = *m_state;
	if (!shown || state.failed)
		return;
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	if (!state.binding)
	{
		// WaterSkyboxSystem::Initialize: the model, asked for the first time it shows; its faces replaced
		// (replacePrototypeTexture) once both names have loaded.
		if (!state.model.Is_Valid())
		{
			state.model = cache->Request_Model(SkyBoxModel);
			for (const auto &[original, replacement] : SkyBoxReplacements(state.setting))
				state.replacements.emplace_back(cache->Request_Texture(original), cache->Request_Texture(replacement));
		}
		const Assets::AssetState modelState = cache->Get_State(state.model);
		if (modelState == Assets::AssetState::Failed)
		{
			state.failed = true;
			return;
		}
		if (modelState != Assets::AssetState::Ready)
			return;
		for (const auto &[original, replacement] : state.replacements)
			if (cache->Get_State(replacement) != Assets::AssetState::Ready && cache->Get_State(replacement) != Assets::AssetState::Failed)
				return;
		auto binding = std::make_unique<Graphics::PropAssetBinding>();
		std::string error;
		std::vector<std::pair<Assets::TextureAssetHandle, Assets::TextureAssetHandle>> ready;
		for (const auto &pair : state.replacements)
			if (cache->Get_State(pair.second) == Assets::AssetState::Ready)
				ready.push_back(pair);
		if (!binding->Load(device, Graphics::Get_Prop_Renderer(), *cache, state.model, error, {}, ready))
		{
			state.failed = true;
			return;
		}
		binding->Clamp_Texture_Addressing();
		state.binding = std::move(binding);
	}
	Graphics::PropParameters parameters;
	parameters.view_projection = viewProjection;
	parameters.camera_position = {eye[0], eye[1], eye[2], 1};
	parameters.world = SkyBoxWorld(state.setting, eye);
	for (std::size_t part = 0; part < state.binding->Part_Count(); ++part)
		state.binding->Draw_Part(commands, part, parameters);
}
}
