export module engine.gameplay.rts.lifecycle.resources.kill_requests;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Entities killed outright (by script or special rules) before the step:
// they die with the step's casualties, like any other death.
export namespace engine::gameplay
{
// Object::kill(damageType, deathType): killed outright in the way it says.
struct TypedKill
{
	ecs::Entity entity;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
};

struct KillRequests
{
	std::vector<ecs::Entity> entities;
	std::vector<TypedKill> typed;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::KillRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.kill_requests";
};
}
