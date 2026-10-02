export module engine.gameplay.rts.propaganda.components.propaganda;
import std;

export import Engine.Core.Math.Fixed;
export import engine.ecs.core.entity;
import engine.ecs.core.component_registry;

// Propaganda (the original's PropagandaTowerBehavior): a tower that, every `delay` ticks, takes in the allies within
// `radius` of it (centre to centre, flat; alive, on the map, no structures; itself only when `affectsSelf`), and each
// tick gives those it took in its weapon bonuses (`bonus`, and `upgradedBonus` too once its player has `upgrade`; only
// to the armed) and heals them `heal` (`upgradedHeal` with the upgrade) of their most a second, as their sole healer
// for `delay` ticks. It works only while `active`: standing whole (none of `pausedStatus`), alive, not disabled (but
// held), not carried inside something, not neutral; and not at all while its player is `neutralPlayer`.
//
// PropagandaInfluence: what may be taken in (the original keeps only scoring kinds: KINDOF_SCORE, SCORE_CREATE,
// SCORE_DESTROY, MP_COUNT_FOR_VICTORY), and by which tower it is now (none: no one's). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct PropagandaTower
{
	static constexpr std::uint32_t NoUpgrade = 0xFFFFFFFFu;
	static constexpr std::uint32_t NoEffect = 0xFFFFFFFFu;

	Engine::Math::Fixed radius;
	Engine::Math::Fixed heal;         // share of the most a second
	Engine::Math::Fixed upgradedHeal; // with the upgrade
	std::uint64_t delay{100};         // ticks between scans (DelayBetweenUpdates)
	std::uint64_t lastScan{0};
	std::uint64_t pausedStatus{0};    // StatusFlags bits it does nothing under (under construction, sold)
	std::uint32_t upgrade{NoUpgrade}; // a player upgrade (UpgradeRequired)
	std::uint32_t bonus{0};           // WeaponBonusConditions bits (ENTHUSIASTIC)
	std::uint32_t upgradedBonus{0};   // (SUBLIMINAL)
	std::uint32_t pulseEffect{NoEffect};         // FX played as it scans (PulseFX), for the game
	std::uint32_t upgradedPulseEffect{NoEffect}; // (UpgradedPulseFX)
	std::uint32_t neutralPlayer{0xFFFFFFFFu};
	std::uint8_t affectsSelf{0};
	std::uint8_t active{0};
	std::uint8_t reserved[6]{};
};

struct PropagandaInfluence
{
	ecs::Entity tower;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PropagandaTower>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_tower";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::PropagandaInfluence>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_influence";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
