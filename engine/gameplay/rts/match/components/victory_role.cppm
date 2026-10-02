export module engine.gameplay.rts.match.components.victory_role;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// What an object means to its player's standing in a match (set by the game from its definition): whether it keeps
// its player in the game while it stands (a structure that counts for victory: KINDOF_STRUCTURE with
// KINDOF_MP_COUNT_FOR_VICTORY), and whether it is handed to the neutral side rather than killed when its player is
// defeated (Team::killTeam: KINDOF_TECH_BUILDING).
export namespace engine::gameplay
{
namespace victory_role
{
inline constexpr std::uint8_t CountsForVictory = 1u << 0;
inline constexpr std::uint8_t NeutralOnDefeat = 1u << 1;
// Its death, once built, leaves in its place something that counts for victory (a GLA structure's rebuild hole): dying
// this tick, it still keeps its player in (the original checks at the tick's end, the hole standing by then).
inline constexpr std::uint8_t LeavesCountedHole = 1u << 2;
}

struct VictoryRole
{
	std::uint8_t flags{0};

	bool Has(std::uint8_t flag) const noexcept { return (flags & flag) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::VictoryRole>
{
	static constexpr std::string_view StableName = "engine.gameplay.victory_role";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
