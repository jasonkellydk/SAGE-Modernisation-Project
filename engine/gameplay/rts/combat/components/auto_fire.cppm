export module engine.gameplay.rts.combat.components.auto_fire;
import std;

import engine.ecs.core.component_registry;

// A weapon an entity fires at its own position whenever it is ready (the
// original's FireWeaponUpdate: fire fields, radiation pools, hazard zones):
// from `readyTick` on, every time its delay (and clip reload) allows; held
// back for `exclusiveDelay` ticks after the entity fires its own weapons.
export namespace engine::gameplay
{
struct AutoFire
{
	std::uint32_t weapon{0};
	std::uint32_t clip{0};
	std::uint64_t readyTick{0};
	std::uint64_t exclusiveDelay{0};
	// Its weapon's FX suspended until this tick (made plus SuspendFXDelay).
	std::uint64_t suspendFxUntil{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AutoFire>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_fire";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
