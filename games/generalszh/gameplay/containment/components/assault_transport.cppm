export module games.generalszh.gameplay.containment.components.assault_transport;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// A transport whose riders fight around it (the original's AssaultTransportAIUpdate: the Troop Crawler): its members
// (m_memberIDs, contiguous; whether each was wounded as it joined, m_memberHealing, and joined after the last order,
// m_newMember: those stay in until a new attack order), the enemy its DEPLOY weapon named (m_designatedTarget; the
// members go for it), where a player's attack-move sends it (m_attackMoveGoalPos) and whether it was ordered to
// attack-move or attack (m_isAttackMove, m_isAttackObject), and whether riders from now on join as new members
// (m_newOccupantsAreNewMembers). `done`: dead, its members given their last orders. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct AssaultTransport
{
	static constexpr std::uint32_t Capacity = 10; // MAX_TRANSPORT_SLOTS
	std::array<ecs::Entity, Capacity> members{};
	std::array<std::uint8_t, Capacity> healing{};
	std::array<std::uint8_t, Capacity> newMember{};
	std::uint32_t count{0};
	std::uint8_t isAttackMove{0};
	std::uint8_t isAttackObject{0};
	std::uint8_t newOccupantsAreNewMembers{0};
	std::uint8_t done{0};
	ecs::Entity designatedTarget;
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
	Engine::Math::FixedVector2 attackMoveGoal;
	Engine::Math::Fixed healAtLifeRatio{Engine::Math::Fixed::FromRatio(1, 2)}; // MembersGetHealedAtLifeRatio

	// reset.
	void Reset() noexcept
	{
		const Engine::Math::Fixed ratio = healAtLifeRatio;
		*this = AssaultTransport{};
		healAtLifeRatio = ratio;
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::AssaultTransport>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.assault_transport";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
