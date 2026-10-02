export module engine.gameplay.rts.combat.components.damage_reaction;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Weapons an entity fires at its own position by how damaged it is (the original's FireWeaponWhenDamagedBehavior):
// hurt by at least `threshold` by a damage type in `damageTypes`, the reaction weapon of its damage state then (pristine,
// damaged, really damaged, rubble) if it is ready; and every tick the continuous weapon of its damage state whenever
// it is ready. Each weapon keeps its own delay and clip (Weapon::getStatus READY_TO_FIRE). Its damage states are
// health at or below `damaged` / `reallyDamaged` of its most (ActiveBody::calcDamageState), rubble at none. Only while
// `active` (StartsActive, or its upgrade). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct ReactionWeapon
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	std::uint32_t weapon{None};
	std::uint32_t clip{0};
	std::uint64_t readyTick{0};
};

struct DamageReaction
{
	std::array<ReactionWeapon, 4> reaction{}; // by damage state: pristine, damaged, really damaged, rubble
	std::array<ReactionWeapon, 4> continuous{};
	std::uint64_t damageTypes{~std::uint64_t{0}};
	Engine::Math::Fixed threshold;
	Engine::Math::Fixed damaged;
	Engine::Math::Fixed reallyDamaged;
	std::uint8_t active{0};
	std::uint8_t reserved[7]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DamageReaction>
{
	static constexpr std::string_view StableName = "engine.gameplay.damage_reaction";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
