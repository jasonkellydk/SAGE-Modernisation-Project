export module games.renegade.gameplay.defense.components.defense;
import std;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.shield;

export namespace renegade
{
struct Defense
{
	std::uint32_t skin{0};
	std::uint32_t canDie{1};
};
}
export namespace ecs
{
template<> struct ComponentTraits<renegade::Defense>
{
	static constexpr std::string_view StableName = "renegade.defense";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
