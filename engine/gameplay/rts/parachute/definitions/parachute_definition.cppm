export module engine.gameplay.rts.parachute.definitions.parachute_definition;
import std;

export import engine.gameplay.rts.movement.algorithms.hover_motion;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A kind of parachute (the original's ParachuteContain on its own object): the sway it may start with, where it opens,
// how its sway settles low down, how hard losing it is, its model's sway centre and harness at rest (PARA_COG,
// PARA_ATTCH), and the two ways it moves (its locomotors: falling closed, gliding open).
export namespace engine::gameplay
{
struct ParachuteDefinition
{
	std::int32_t pitchRateMax{0}; // turn units a tick (PitchRateMax)
	std::int32_t rollRateMax{0};  // (RollRateMax)
	Engine::Math::Fixed lowAltitudeDamping{Engine::Math::Fixed::FromRatio(2, 10)};
	Engine::Math::Fixed openDistance;                                              // ParachuteOpenDist
	Engine::Math::Fixed freeFallDamage{Engine::Math::Fixed::FromRatio(1, 2)};    // FreeFallDamagePercent, of the rider's most
	Engine::Math::Fixed waterSlop{Engine::Math::Fixed::FromInt(10)};              // KillWhenLandingInWaterSlop
	std::uint32_t drownDamageType{0}; // DAMAGE_WATER, as a rider landing in water is hurt
	std::uint32_t drownDeathType{0};  // DEATH_FLOODED
	std::uint32_t fallDamageType{0};  // DAMAGE_FALLING, as a rider losing its chute aloft is hurt
	std::uint32_t fallDeathType{0};   // DEATH_SPLATTED
	Engine::Math::FixedVector3 swayBone;   // PARA_COG
	Engine::Math::FixedVector3 attachBone; // PARA_ATTCH
	HoverLocomotor open;                   // its NORMAL locomotor
	HoverLocomotor freeFall;               // its FREEFALL locomotor
};

// Every kind's parachute, by index (a Parachute holds its own).
class ParachuteCatalog
{
public:
	std::uint32_t Add(ParachuteDefinition definition)
	{
		m_definitions.push_back(std::move(definition));
		return static_cast<std::uint32_t>(m_definitions.size() - 1);
	}
	const ParachuteDefinition &At(std::uint32_t index) const { return m_definitions.at(index); }
	std::size_t Size() const noexcept { return m_definitions.size(); }

private:
	std::vector<ParachuteDefinition> m_definitions;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ParachuteCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_catalog";
};
}
