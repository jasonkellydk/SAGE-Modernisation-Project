export module games.generalszh.content.terrain.bridge_content;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
import engine.config.binding.values;
export import engine.config.document.document;

// Roads.ini's Bridge blocks (TerrainRoadType as a bridge, TerrainRoadCollection::findBridge by name): its scale, its
// models and textures by damage state (the pristine model's BRIDGE_LEFT mesh sets how wide the bridge is), its radar
// colour, its towers, scaffolding, transition effects and sounds. A Bridge block starts from the DefaultBridge block
// written before it, when there is one (TerrainRoadCollection::newBridge: its texture, scale, models, damaged textures,
// TransitionEffectsHeight and NumFXPerType).
export namespace generalszh::content
{
// BodyDamageType: PRISTINE, DAMAGED, REALLYDAMAGED, RUBBLE.
inline constexpr std::size_t BodyDamageStates = 4;
// MAX_BRIDGE_BODY_FX.
inline constexpr std::size_t BridgeBodyEffects = 3;

// BridgeTowerType: FROM_LEFT, FROM_RIGHT, TO_LEFT, TO_RIGHT.
inline constexpr std::size_t BridgeTowers = 4;

struct BridgeContent
{
	std::string name;
	Engine::Math::Fixed scale{Engine::Math::Fixed::One()}; // BridgeScale
	std::array<std::string, BodyDamageStates> models;      // BridgeModelName, ...Damaged, ...ReallyDamaged, ...Broken
	std::array<std::string, BodyDamageStates> textures;    // Texture, TextureDamaged, TextureReallyDamaged, TextureBroken
	std::array<std::uint8_t, 3> radarColor{};               // RadarColor (R:, G:, B: 0..255; the original keeps them / 255)
	std::array<std::string, BridgeTowers> towers;          // TowerObjectNameFromLeft ...
	std::string scaffold, scaffoldSupport;                  // ScaffoldObjectName, ScaffoldSupportObjectName
	Engine::Math::Fixed transitionEffectsHeight;            // TransitionEffectsHeight
	std::uint32_t fxPerType{0};                             // NumFXPerType
	std::string damagedSound, repairedSound;                // DamagedToSound, RepairedToSound (to DAMAGED)
	// TransitionToOCL / TransitionToFX: by the state it goes to, three each (EffectNum 1..3).
	std::array<std::array<std::string, BridgeBodyEffects>, BodyDamageStates> damageOcl, damageFx, repairOcl, repairFx;
	// The pristine model's BRIDGE_LEFT mesh, least and greatest y at rest (W3DBridge::load's m_minY, m_maxY); none: the
	// model is not there (the original then makes no bridge).
	std::optional<std::pair<Engine::Math::Fixed, Engine::Math::Fixed>> extentY;
};

using BridgeCatalog = std::map<std::string, BridgeContent, std::less<>>;

// A BridgeBehavior's BridgeDieFX / BridgeDieOCL (`FX:<fx> Delay:<ms> [Bone:<bone>]`, `OCL:<ocl> Delay:<ms>
// [Bone:<bone>|Bone:ParentObject]`): what goes off how long after the bridge died, and where: at a bone of its model
// (at rest, in the bridge's own frame), from the bridge itself (ParentObject: an OCL made with no position), or at a
// random point of the deck.
struct BridgeDieEffect
{
	enum class Where : std::uint8_t
	{
		Surface,
		Bone,
		Parent,
	};
	bool creationList{false}; // BridgeDieOCL, else BridgeDieFX
	std::string name;
	std::uint64_t delayTicks{0};
	Where where{Where::Surface};
	Engine::Math::FixedVector3 bone; // Where::Bone: the bone at rest (none there: the bridge's own position)
};

namespace bridge_content_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}
}

// INI::getNextSubToken over a line of `Name:Value` tokens (the value in the same token after the colon, or the next
// token when the colon ends one): the value for `name`, case blind.
inline std::optional<std::string> SubToken(std::span<const std::string_view> tokens, std::string_view name)
{
	for (std::size_t index = 0; index < tokens.size(); ++index)
	{
		const std::string_view token = tokens[index];
		const auto colon = token.find(':');
		if (colon == std::string_view::npos || !bridge_content_detail::Same(token.substr(0, colon), name))
			continue;
		if (colon + 1 < token.size())
			return std::string(token.substr(colon + 1));
		if (index + 1 < tokens.size())
			return std::string(tokens[index + 1]);
		return std::nullopt;
	}
	return std::nullopt;
}

// TheBodyDamageTypeNames' index.
inline std::optional<std::size_t> BodyDamageStateIndex(std::string_view name)
{
	constexpr std::array<std::string_view, BodyDamageStates> names{"PRISTINE", "DAMAGED", "REALLYDAMAGED", "RUBBLE"};
	for (std::size_t index = 0; index < names.size(); ++index)
		if (bridge_content_detail::Same(names[index], name))
			return index;
	return std::nullopt;
}

inline BridgeCatalog BindBridges(const engine::config::Document &document)
{
	BridgeCatalog catalog;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Bridge" || root.values.empty())
			continue;
		BridgeContent fresh;
		// newBridge: the defaults of a DefaultBridge already made.
		if (const auto defaults = catalog.find("DefaultBridge"); defaults != catalog.end())
		{
			const BridgeContent &from = defaults->second;
			fresh.textures = from.textures;
			fresh.scale = from.scale;
			fresh.models = from.models;
			fresh.transitionEffectsHeight = from.transitionEffectsHeight;
			fresh.fxPerType = from.fxPerType;
		}
		BridgeContent &bridge = catalog[std::string(root.Value())];
		bridge = std::move(fresh);
		bridge.name = std::string(root.Value());
		for (const engine::config::Node &field : root.children)
		{
			if (field.values.empty())
				continue;
			const std::string text(field.Value());
			const auto key = field.key;
			if (key == "BridgeScale")
				bridge.scale = engine::config::values::ParseFixed(field.Value()).value_or(bridge.scale);
			else if (key == "ScaffoldObjectName")
				bridge.scaffold = text;
			else if (key == "ScaffoldSupportObjectName")
				bridge.scaffoldSupport = text;
			else if (key == "TransitionEffectsHeight")
				bridge.transitionEffectsHeight = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{});
			else if (key == "NumFXPerType")
				bridge.fxPerType = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(field.Value()).value_or(0)));
			else if (key == "BridgeModelName")
				bridge.models[0] = text;
			else if (key == "BridgeModelNameDamaged")
				bridge.models[1] = text;
			else if (key == "BridgeModelNameReallyDamaged")
				bridge.models[2] = text;
			else if (key == "BridgeModelNameBroken")
				bridge.models[3] = text;
			else if (key == "Texture")
				bridge.textures[0] = text;
			else if (key == "TextureDamaged")
				bridge.textures[1] = text;
			else if (key == "TextureReallyDamaged")
				bridge.textures[2] = text;
			else if (key == "TextureBroken")
				bridge.textures[3] = text;
			else if (key == "RadarColor")
			{
				// INI::parseRGBColor: R, G and B in that order, each 0..255.
				const std::vector<std::string_view> tokens(field.values.begin(), field.values.end());
				constexpr std::array<std::string_view, 3> names{"R", "G", "B"};
				for (std::size_t channel = 0; channel < names.size(); ++channel)
					if (const auto value = SubToken(tokens, names[channel]))
						if (const auto number = engine::config::values::ParseInt(*value); number && *number >= 0 && *number <= 255)
							bridge.radarColor[channel] = static_cast<std::uint8_t>(*number);
			}
			else if (key == "TowerObjectNameFromLeft")
				bridge.towers[0] = text;
			else if (key == "TowerObjectNameFromRight")
				bridge.towers[1] = text;
			else if (key == "TowerObjectNameToLeft")
				bridge.towers[2] = text;
			else if (key == "TowerObjectNameToRight")
				bridge.towers[3] = text;
			else if (key == "DamagedToSound")
				bridge.damagedSound = text;
			else if (key == "RepairedToSound")
				bridge.repairedSound = text;
			else if (key == "TransitionToOCL" || key == "TransitionToFX")
			{
				std::vector<std::string_view> tokens(field.values.begin(), field.values.end());
				const auto transition = SubToken(tokens, "Transition");
				const auto state = SubToken(tokens, "ToState");
				const auto number = SubToken(tokens, "EffectNum");
				const auto name = SubToken(tokens, key == "TransitionToOCL" ? "OCL" : "FX");
				if (!transition || !state || !number || !name)
					continue;
				const auto index = BodyDamageStateIndex(*state);
				const auto effect = engine::config::values::ParseInt(*number);
				if (!index || !effect || *effect < 1 || *effect > static_cast<std::int64_t>(BridgeBodyEffects))
					continue;
				const bool damage = bridge_content_detail::Same(*transition, "Damage");
				if (!damage && !bridge_content_detail::Same(*transition, "Repair"))
					continue;
				auto &table = key == "TransitionToOCL" ? (damage ? bridge.damageOcl : bridge.repairOcl) : (damage ? bridge.damageFx : bridge.repairFx);
				table[*index][static_cast<std::size_t>(*effect - 1)] = *name;
			}
		}
	}
	return catalog;
}
}
