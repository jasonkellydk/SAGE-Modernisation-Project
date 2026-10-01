export module engine.gameplay.rts.stealth.components.stealth_rider;
import std;

import engine.ecs.core.component_registry;

// A container whose stealth follows its rider (UseRiderStealth: StealthUpdate::calcStealthOwner, the combat bike): its
// first rider's stealth rules as found this tick (StealthRiderSystem), which its own stealth then follows: the rider's
// StealthForbiddenConditions and StealthDelay, whether the rider has OBJECT_STATUS_CAN_STEALTH and whether idle enemies
// are ordered at it when revealed. With no rider its own rules hold.
export namespace engine::gameplay
{
namespace stealth_rider_flag
{
inline constexpr std::uint32_t Rider = 1u << 0;            // it has a rider
inline constexpr std::uint32_t RiderStealth = 1u << 1;     // the rider has a StealthUpdate (its rules replace the container's)
inline constexpr std::uint32_t CanStealth = 1u << 2;       // the rider's OBJECT_STATUS_CAN_STEALTH
inline constexpr std::uint32_t OrderIdleEnemies = 1u << 3; // the rider's OrderIdleEnemiesToAttackMeUponReveal
}

struct StealthRider
{
	std::uint64_t delay{0};
	std::uint32_t forbidden{0};
	std::uint32_t flags{0};

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::StealthRider>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_rider";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
