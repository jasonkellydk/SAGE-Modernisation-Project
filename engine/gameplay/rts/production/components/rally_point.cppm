export module engine.gameplay.rts.production.components.rally_point;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A factory's rally point its player set (ExitInterface::setRallyPoint: a production exit's m_rallyPoint, present as
// m_rallyPointExists): what it makes goes on there from its natural rally point.
export namespace engine::gameplay
{
struct RallyPoint
{
	Engine::Math::FixedVector2 at;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::RallyPoint>
{
	static constexpr std::string_view StableName = "engine.gameplay.rally_point";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
