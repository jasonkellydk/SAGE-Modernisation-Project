export module engine.gameplay.rts.aircraft.resources.jet_touchdowns;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The jets that touched down on their runway (or flight deck) this tick, where they were (JetTakeoffOrLandingState's
// landing: the presentation's wheel screech). Refilled every tick by the touchdown system; not saved.
export namespace engine::gameplay
{
struct JetTouchdown
{
	ecs::Entity jet;
	Engine::Math::FixedVector3 position;
};

struct JetTouchdowns
{
	std::vector<JetTouchdown> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::JetTouchdowns>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet_touchdowns";
};
}
