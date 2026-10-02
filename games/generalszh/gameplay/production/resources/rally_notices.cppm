export module games.generalszh.gameplay.production.resources.rally_notices;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Rally points players tried to set this tick (GameLogic's doSetRallyPoint), for the setter's feedback: set (GUI:
// RallyPointSet with the factory's name, the RallyPointSet sound) or not, no path there (GUI:RallyPointNoPath,
// UnableToSetRallyPoint). Per tick: not saved.
export namespace generalszh::gameplay
{
struct RallyNotice
{
	ecs::Entity factory;
	std::uint32_t player{0};
	Engine::Math::FixedVector2 at;
	bool set{false};
};

struct RallyNotices
{
	std::vector<RallyNotice> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::RallyNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rally_notices";
};
}
