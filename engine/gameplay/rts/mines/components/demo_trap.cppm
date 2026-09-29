export module engine.gameplay.rts.mines.components.demo_trap;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// A demo trap (the original's DemoTrapUpdate): its mode is the weapon slot it is locked to (its SWITCH_WEAPON buttons:
// `proximitySlot`, `manualSlot`, `detonationSlot`). In the detonation slot it goes off at once; in proximity mode, every
// `scanTicks` it looks within `range` (centre to centre, flat) for an enemy on the ground (not of `ignoreClasses`) and
// goes off if it finds one (unless an ally or neutral is also there and `friendlyDetonation` is off); in manual mode it
// waits. Going off fires its `weapon` (if any) where it is and kills it (its death does the rest); killed, it goes
// off too when `detonateWhenKilled`. `countdown`: ticks to its next scan. Found unlocked (as it is made: its loadout
// equips after the lock is set in the original's order), it takes its `defaultSlot`. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct DemoTrap
{
	static constexpr std::uint32_t NoWeapon = 0xFFFFFFFFu;

	Engine::Math::Fixed range;
	std::uint64_t scanTicks{0};
	std::uint64_t countdown{0};
	std::uint32_t ignoreClasses{0};
	std::uint32_t weapon{NoWeapon};
	std::uint8_t detonationSlot{0};
	std::uint8_t proximitySlot{0};
	std::uint8_t manualSlot{0};
	std::uint8_t friendlyDetonation{0};
	std::uint8_t detonateWhenKilled{0};
	std::uint8_t detonated{0};
	std::uint8_t defaultSlot{0}; // DefaultProximityMode: proximity, else manual (onObjectCreated's LOCKED_TEMPORARILY)
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DemoTrap>
{
	static constexpr std::string_view StableName = "engine.gameplay.demo_trap";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
