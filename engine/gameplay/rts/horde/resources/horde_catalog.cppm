export module engine.gameplay.rts.horde.resources.horde_catalog;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Per definition (DefinitionRef::index): how it hordes (HordeUpdate module
// data, with the game's kinds as opaque bits: the kinds a fellow must all
// have, and its own), and its bounding sphere (radius, and its centre's
// height over its position) for the original's FROM_BOUNDINGSPHERE_3D.
// The game's bonuses: the weapon bonus condition bits a horde holds, and an
// upgrade rule (AIUpdateInterface::evaluateMoraleBonus, the classic form):
// with the player's `upgrade` a horde member holds `upgradeBonus` and then
// `followerBonus` exactly while it also has `followerUpgrade`; without
// `upgrade` it loses `upgradeBonus` and keeps whatever follower bonus it has.
export namespace engine::gameplay
{
struct HordeDefinition
{
	bool hordes{false};                 // it has a HordeUpdate
	std::uint64_t updateTicks{30};      // UpdateRate
	std::array<std::uint64_t, 2> requiredKinds{}; // KindOf: a fellow must have all of these
	std::array<std::uint64_t, 2> kinds{};         // its own kinds (the same bits)
	std::uint32_t count{0};             // Count
	Engine::Math::Fixed radius;         // Radius
	Engine::Math::Fixed rubOffRadius{Engine::Math::Fixed::FromInt(20)}; // RubOffRadius
	bool alliesOnly{true};              // AlliesOnly
	bool exactMatch{false};             // ExactMatch
	bool upgradeBonusAllowed{true};     // the upgrade rule applies (AllowedNationalism)
	bool infantry{false};               // KINDOF_INFANTRY: looks every UpdateRate (vehicles: every UpdateRate + 1)
	Engine::Math::Fixed sphereRadius;   // GeometryInfo::getBoundingSphereRadius
	Engine::Math::Fixed centerHeight;   // GeometryInfo::getZDeltaToCenterPosition
};

struct HordeCatalog
{
	static constexpr std::uint32_t NoUpgrade = 0xFFFFFFFFu;

	std::vector<HordeDefinition> byDefinition;
	std::uint32_t hordeBonus{0};
	std::uint32_t upgrade{NoUpgrade};
	std::uint32_t upgradeBonus{0};
	std::uint32_t followerUpgrade{NoUpgrade};
	std::uint32_t followerBonus{0};

	const HordeDefinition *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() && byDefinition[definition].hordes ? &byDefinition[definition] : nullptr;
	}
};

// Each tick's hordes before any looks (a fellow's true membership is as it was).
struct HordeRosterEntry
{
	std::uint32_t entityIndex{0};
	std::uint32_t entityGeneration{0};
	std::uint32_t definition{0};
	std::uint32_t player{0};
	Engine::Math::Fixed x, y, z; // its bounding sphere's centre
	bool trueMember{false};
	std::uint32_t team{0xFFFFFFFFu}; // its team (none: NoTeam)
};

struct HordeRoster
{
	std::vector<HordeRosterEntry> entries;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HordeCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.horde_catalog";
};

template<>
struct ResourceTraits<engine::gameplay::HordeRoster>
{
	static constexpr std::string_view StableName = "engine.gameplay.horde_roster";
};
}
