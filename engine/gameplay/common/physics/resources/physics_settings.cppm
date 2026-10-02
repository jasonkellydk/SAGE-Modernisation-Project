export module engine.gameplay.common.physics.resources.physics_settings;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// The world's physical constants, configured by the game: gravity (per tick
// squared, negative is down) and how hard the ground pushes back on a bounce.
export namespace engine::gameplay
{
struct PhysicsSettings
{
	Engine::Math::Fixed gravity{Engine::Math::Fixed::FromRatio(-64, 900)}; // -64 per second squared at 30 ticks
	Engine::Math::Fixed groundStiffness{Engine::Math::Fixed::FromRatio(8, 10)};
	// How falling hurts and kills (the game's damage and death types).
	std::uint32_t fallDamageType{0};
	std::uint32_t fallDeathType{0};

	// The speed a fall from `height` lands at (v = sqrt(2 g h)).
	Engine::Math::Fixed FallSpeed(Engine::Math::Fixed height) const noexcept
	{
		return Engine::Math::Sqrt(Engine::Math::Abs(Engine::Math::Fixed::FromInt(2) * gravity * height));
	}

	// High enough to take more than three ticks to fall back: really airborne.
	Engine::Math::Fixed SignificantHeight() const noexcept { return Engine::Math::Fixed{} - gravity * Engine::Math::Fixed::FromInt(9); }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PhysicsSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.physics_settings";
};
}
