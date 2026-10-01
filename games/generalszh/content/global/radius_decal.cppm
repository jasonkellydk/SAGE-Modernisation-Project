export module games.generalszh.content.global.radius_decal;
import std;

export import engine.config.binding.schema;

// A RadiusDecalTemplate (RadiusDecal.cpp): a texture laid on the ground as a square of twice the decal's radius
// (a projected shadow decal: SHADOW_ALPHA_DECAL blended by its alpha, SHADOW_ADDITIVE_DECAL added), tinted by its
// Color (none: its owner's player colour), its opacity throbbing between OpacityMin and OpacityMax over
// OpacityThrobTime, seen only by its owner unless OnlyVisibleToOwningPlayer is No. The radius cursors (InGameUI's
// *RadiusCursor templates, by RadiusCursorType) are such decals following the pointer.
export namespace generalszh::content
{
struct RadiusDecalLook
{
	std::string texture;
	bool additive{false};
	Engine::Math::Fixed minOpacity{Engine::Math::Fixed::One()};
	Engine::Math::Fixed maxOpacity{Engine::Math::Fixed::One()};
	std::uint64_t throbTicks{30}; // LOGICFRAMES_PER_SECOND unless given
	std::array<std::uint8_t, 4> color{};
	bool hasColor{false};
	bool onlyOwner{true};

	bool Present() const noexcept { return !texture.empty(); }
	bool operator==(const RadiusDecalLook &) const = default;
};

// TheRadiusCursorNames (InGameUI.h), in RadiusCursorType order; NONE first.
inline constexpr std::array<std::string_view, 30> RadiusCursorNames{"NONE", "ATTACK_DAMAGE_AREA", "ATTACK_SCATTER_AREA", "ATTACK_CONTINUE_AREA", "GUARD_AREA",
	"EMERGENCY_REPAIR", "FRIENDLY_SPECIALPOWER", "OFFENSIVE_SPECIALPOWER", "SUPERWEAPON_SCATTER_AREA", "PARTICLECANNON", "A10STRIKE", "CARPETBOMB", "DAISYCUTTER",
	"PARADROP", "SPYSATELLITE", "SPECTREGUNSHIP", "HELIX_NAPALM_BOMB", "NUCLEARMISSILE", "EMPPULSE", "ARTILLERYBARRAGE", "NAPALMSTRIKE", "CLUSTERMINES",
	"SCUDSTORM", "ANTHRAXBOMB", "AMBUSH", "RADAR", "SPYDRONE", "FRENZY", "CLEARMINES", "AMBULANCE"};

namespace radius_cursor
{
inline constexpr std::uint8_t None = 0;
inline constexpr std::uint8_t AttackDamageArea = 1;
inline constexpr std::uint8_t AttackScatterArea = 2;
inline constexpr std::uint8_t AttackContinueArea = 3;
inline constexpr std::uint8_t GuardArea = 4;
inline constexpr std::uint8_t ClearMines = 28;
}

// InGameUI's fields for each cursor type (InGameUI.cpp's field table), by RadiusCursorType.
inline constexpr std::array<std::pair<std::string_view, std::uint8_t>, 29> RadiusCursorFields{{{"AttackDamageAreaRadiusCursor", 1},
	{"AttackScatterAreaRadiusCursor", 2}, {"AttackContinueAreaRadiusCursor", 3}, {"FriendlySpecialPowerRadiusCursor", 6},
	{"OffensiveSpecialPowerRadiusCursor", 7}, {"SuperweaponScatterAreaRadiusCursor", 8}, {"GuardAreaRadiusCursor", 4}, {"EmergencyRepairRadiusCursor", 5},
	{"ParticleCannonRadiusCursor", 9}, {"A10StrikeRadiusCursor", 10}, {"CarpetBombRadiusCursor", 11}, {"DaisyCutterRadiusCursor", 12},
	{"ParadropRadiusCursor", 13}, {"SpySatelliteRadiusCursor", 14}, {"SpectreGunshipRadiusCursor", 15}, {"HelixNapalmBombRadiusCursor", 16},
	{"NuclearMissileRadiusCursor", 17}, {"EMPPulseRadiusCursor", 18}, {"ArtilleryRadiusCursor", 19}, {"FrenzyRadiusCursor", 27},
	{"NapalmStrikeRadiusCursor", 20}, {"ClusterMinesRadiusCursor", 21}, {"ScudStormRadiusCursor", 22}, {"AnthraxBombRadiusCursor", 23},
	{"AmbushRadiusCursor", 24}, {"RadarRadiusCursor", 25}, {"SpyDroneRadiusCursor", 26}, {"ClearMinesRadiusCursor", 28}, {"AmbulanceRadiusCursor", 29}}};

inline std::uint8_t RadiusCursorIndex(std::string_view name) noexcept
{
	for (std::size_t index = 0; index < RadiusCursorNames.size(); ++index)
		if (std::ranges::equal(RadiusCursorNames[index], name, [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); }))
			return static_cast<std::uint8_t>(index);
	return radius_cursor::None;
}

// RadiusDecalTemplate::parseRadiusDecalTemplate: Texture, Style (TheShadowNames bits: SHADOW_ADDITIVE_DECAL adds),
// OpacityMin / OpacityMax (percent), OpacityThrobTime (INI::parseDurationUnsignedInt: milliseconds to frames, rounded
// up), Color (INI::parseColorInt, "R:255 G:0 B:0 A:255"), OnlyVisibleToOwningPlayer.
inline RadiusDecalLook ReadRadiusDecal(const engine::config::Node &block, std::uint32_t ticksPerSecond)
{
	RadiusDecalLook look;
	const auto percent = [](std::string_view text) -> std::optional<Engine::Math::Fixed> {
		if (!text.empty() && text.back() == '%')
			text.remove_suffix(1);
		const auto value = engine::config::values::ParseFixed(text);
		return value ? std::optional(*value / Engine::Math::Fixed::FromInt(100)) : std::nullopt;
	};
	for (const engine::config::Node &field : block.children)
	{
		if (field.values.empty())
			continue;
		const std::string_view key = field.key;
		if (key == "Texture")
			look.texture = std::string(field.Value());
		else if (key == "Style")
		{
			look.additive = false;
			for (const std::string_view token : field.values)
				if (token == "SHADOW_ADDITIVE_DECAL")
					look.additive = true;
		}
		else if (key == "OpacityMin")
			look.minOpacity = percent(field.Value()).value_or(look.minOpacity);
		else if (key == "OpacityMax")
			look.maxOpacity = percent(field.Value()).value_or(look.maxOpacity);
		else if (key == "OpacityThrobTime")
		{
			if (const auto ms = engine::config::values::ParseFixed(field.Value()))
				look.throbTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(
					(*ms * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticksPerSecond)) / Engine::Math::Fixed::FromInt(1000)).Ceil(), 1));
		}
		else if (key == "Color")
		{
			std::array<int, 4> channels{0, 0, 0, 255};
			for (const std::string_view value : field.values)
				for (const auto &[prefix, index] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}})
					if (value.starts_with(prefix))
						std::from_chars(value.data() + 2, value.data() + value.size(), channels[static_cast<std::size_t>(index)]);
			for (std::size_t index = 0; index < 4; ++index)
				look.color[index] = static_cast<std::uint8_t>(std::clamp(channels[index], 0, 255));
			// parseColorInt packs A R G B; a colour of 0 is none (the owner's colour).
			look.hasColor = look.color != std::array<std::uint8_t, 4>{};
		}
		else if (key == "OnlyVisibleToOwningPlayer")
			look.onlyOwner = engine::config::values::ParseBool(field.Value()).value_or(look.onlyOwner);
	}
	return look;
}
}
