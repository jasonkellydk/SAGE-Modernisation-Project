export module engine.gameplay.rts.movement.components.formation_member;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A unit's user formation (Object::m_formationID and m_formationOffset): the formation it is in (0: none,
// NO_FORMATION_ID) and its place in it, its offset from the formation's centre as it was made. A side table on the
// unit; the place outlives the formation (the original keeps m_formationOffset when the id is cleared).
export namespace engine::gameplay
{
struct FormationMember
{
	std::uint32_t id{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
	Engine::Math::FixedVector2 offset;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::FormationMember>
{
	static constexpr std::string_view StableName = "engine.gameplay.formation_member";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::FormationMember &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.id);
		hasher.AppendU64(static_cast<std::uint64_t>(value.offset.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.offset.y.Raw()));
	}
};
}
