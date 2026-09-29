export module engine.gameplay.rts.veterancy.components.experience;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// An object's experience (the original's ExperienceTracker): its points and
// veterancy level, whether it can gain any, what its gains are scaled by
// (ExperienceScalarUpgrade), and where its gains go instead (its experience
// sink: a spawner, launcher or container that takes them).
export namespace engine::gameplay
{
struct Experience
{
	std::int32_t points{0};
	std::uint8_t level{0};
	bool trainable{false};
	std::uint8_t reserved[2]{}; // no padding: checkpoints hold its bytes
	Engine::Math::Fixed scalar{Engine::Math::Fixed::One()};
	ecs::Entity sink;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Experience>
{
	static constexpr std::string_view StableName = "engine.gameplay.experience";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Experience &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.points)) << 32) | (std::uint64_t{value.level} << 8) |
			(value.trainable ? 1u : 0u));
		hasher.AppendU64(static_cast<std::uint64_t>(value.scalar.Raw()));
		hasher.AppendU64((static_cast<std::uint64_t>(value.sink.index) << 32) | value.sink.generation);
	}
};
}
