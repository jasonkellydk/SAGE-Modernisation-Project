export module games.generalszh.gameplay.combat.components.weapon_bonus_pulse;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import games.generalszh.content.objects.kind_of;

// Something that gives a weapon bonus to allies about it every so often (WeaponBonusUpdate: the Frenzy clouds): the tick
// of its next pulse (its first update, then BonusDelay on). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct WeaponBonusPulse
{
	std::uint64_t nextPulse{0};
};

// Each definition's WeaponBonusUpdate (present or not): RequiredAffectKindOf and ForbiddenAffectKindOf, BonusDuration and
// BonusDelay (ticks), BonusRange and BonusConditionType (its bit).
struct WeaponBonusPulseConfig
{
	bool present{false};
	content::KindOfMask required{};
	content::KindOfMask forbidden{};
	std::uint32_t durationTicks{0};
	std::uint32_t delayTicks{0};
	Engine::Math::Fixed range;
	std::uint32_t bit{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::WeaponBonusPulse>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.weapon_bonus_pulse";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
