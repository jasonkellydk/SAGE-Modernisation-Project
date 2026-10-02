export module engine.gameplay.rts.collision.components.collide_weapon;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A weapon fired at whatever runs into it (the original's FireWeaponCollide): its weapon, only while it burns when it
// requires AFLAME (a burning tree), and at most once ever when it fires once.
export namespace engine::gameplay
{
struct CollideWeapon
{
	std::uint32_t weapon{0xFFFFFFFFu};
	std::uint8_t requiresAflame{0};
	std::uint8_t fireOnce{0};
	std::uint8_t fired{0};
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::CollideWeapon>
{
	static constexpr std::string_view StableName = "engine.gameplay.collide_weapon";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
