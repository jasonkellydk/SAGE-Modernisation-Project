export module games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
import engine.ecs.system.system;
export import Engine.Core.Math.FixedVector;

// What the tick's sticky bombs gave the presentation: a bomb stuck on ("StickyBombCreated" where it is), its pings
// ("UnitBombPing" on it) and a booby trap's GeometryBasedDamageFX (where its victim stands, over its blast's
// radius). The tick's only: cleared as each tick starts.
export namespace generalszh::gameplay
{
struct StickyBombCue
{
	enum class Kind : std::uint8_t
	{
		Created,
		Ping,
		Effect,
	};
	Kind kind{Kind::Ping};
	ecs::Entity bomb;
	std::uint32_t definition{0}; // the bomb's
	std::uint32_t effect{0};     // Effect: the named effect id
	std::uint32_t player{0};     // the bomb's player
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed radius;
};

struct StickyBombCues
{
	std::vector<StickyBombCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::StickyBombCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.sticky_bomb_cues";
};
}
