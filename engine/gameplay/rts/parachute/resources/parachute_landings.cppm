export module engine.gameplay.rts.parachute.resources.parachute_landings;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Riders a parachute set down on the ground this tick (ParachuteContain::onCollide -> onRemoving), in the order they
// landed: what each does next is the game's (a skirmish computer's hunts; one its delivery's building sends on goes out
// by that building's exit; else it idles). Spent by the game after the tick.
export namespace engine::gameplay
{
struct ParachuteLandings
{
	std::vector<ecs::Entity> riders;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ParachuteLandings>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_landings";
};
}
