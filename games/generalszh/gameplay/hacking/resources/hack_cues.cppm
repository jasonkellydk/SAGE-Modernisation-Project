export module games.generalszh.gameplay.hacking.resources.hack_cues;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
import engine.ecs.system.system;
export import Engine.Core.Math.FixedVector;

// What the tick's hackers gave the presentation: UnitUnpack as one unpacks, UnitPack as one packs, and its pay (the cash
// shown over it, UnitCashPing). The tick's only: cleared as each tick starts.
export namespace generalszh::gameplay
{
struct HackCue
{
	enum class Kind : std::uint8_t
	{
		Unpack,
		Pack,
		Cash,
	};
	Kind kind{Kind::Cash};
	ecs::Entity hacker;
	std::uint32_t definition{0};
	std::uint32_t amount{0};
	Engine::Math::FixedVector3 at;
};

struct HackCues
{
	std::vector<HackCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::HackCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hack_cues";
};
}
