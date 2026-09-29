export module engine.gameplay.rts.movement.components.move_path;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Points still to go to after the current move (AIUpdateInterface::aiFollowExitProductionPath: a path of positions,
// taken in order). The move path system starts the next once its move order is done.
export namespace engine::gameplay
{
struct MovePath
{
	static constexpr std::uint32_t Capacity = 4;
	std::uint32_t count{0};
	std::uint32_t next{0};
	std::array<Engine::Math::FixedVector2, Capacity> points{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MovePath>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_path";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
