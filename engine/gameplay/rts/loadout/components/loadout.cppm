export module engine.gameplay.rts.loadout.components.loadout;
import std;

export import engine.ecs.core.component_registry;

// Which of its definition's weapon sets and armor sets an entity uses (the
// original's WeaponSetFlags and ArmorSetFlags, and the sets they picked):
// flags are the game's condition bits (upgrades, crates, riders, ...); the
// sets are indices into its definition's loadout (none: not picked yet). A
// change of flags that picks another set equips it (the loadout system).
export namespace engine::gameplay
{
struct Loadout
{
	static constexpr std::uint16_t Unpicked = 0xFFFFu;
	std::uint32_t weaponFlags{0};
	std::uint32_t armorFlags{0};
	std::uint16_t weaponSet{Unpicked};
	std::uint16_t armorSet{Unpicked};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Loadout>
{
	static constexpr std::string_view StableName = "engine.gameplay.loadout";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
