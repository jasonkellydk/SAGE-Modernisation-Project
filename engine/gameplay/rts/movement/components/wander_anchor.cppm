export module engine.gameplay.rts.movement.components.wander_anchor;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// The point a unit wanders about (AIWanderInPlaceState::m_origin: where it stood when told to): each goal it reaches,
// its next is somewhere within its locomotor's WanderAboutPointRadius of here (MoveMode::WanderInPlace). Simulation
// state: checkpointed.
export namespace engine::gameplay
{
struct WanderAnchor
{
	Engine::Math::FixedVector2 origin;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::WanderAnchor>
{
	static constexpr std::string_view StableName = "engine.gameplay.wander_anchor";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::WanderAnchor &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.origin.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.origin.y.Raw()));
	}
};
}
