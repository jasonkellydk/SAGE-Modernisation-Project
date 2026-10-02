export module engine.gameplay.common.spatial.components.transform;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Where an entity is in the world: position (world units, z up) and the
// direction it faces around z.
export namespace engine::gameplay
{
struct Transform
{
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Transform>
{
	static constexpr std::string_view StableName = "engine.gameplay.transform";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Transform &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.z.Raw()));
		hasher.AppendU64(value.facing.units);
	}
};
}
