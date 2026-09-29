export module engine.gameplay.rts.lifecycle.resources.casualties;
import std;

export import engine.gameplay.common.spatial.components.transform;
export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Who left the world this tick and how, in deterministic order: the
// client's death effects and sounds read it (through the event bus). The
// session clears it as a tick starts; scripted removals and the removal
// system both add to it.
export namespace engine::gameplay
{
enum class Departure : std::uint8_t
{
	Killed,   // destroyed by damage or a scripted kill
	Removed,  // deleted, or a carrier that finished its run
	WentDown, // a passenger lost with its transport
};

struct Casualty
{
	ecs::Entity entity;
	std::uint32_t definition{0};
	Transform transform;
	ecs::Entity killer;
	Departure departure{Departure::Killed};
	std::uint32_t team{0xFFFFFFFFu}; // the team it was on (none: 0xFFFFFFFF)
};

struct Casualties
{
	std::vector<Casualty> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Casualties>
{
	static constexpr std::string_view StableName = "engine.gameplay.casualties";
};
}
