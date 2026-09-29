export module engine.gameplay.rts.containment.definitions.transport;
import std;
export import Engine.Core.Math.Fixed;

// What a transport carries (bound from config by the game): slots, and how
// fast passengers leave when it unloads. Airborne transports land to unload
// unless they may drop passengers in the air.
export namespace engine::gameplay
{
struct TransportDefinition
{
	std::uint32_t slots{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
	std::uint64_t exitDelay{0}; // ticks between passengers leaving
	bool unloadInAir{false};
	// PassengersAllowedToFire; a transport (TransportContain and kin) lets only infantry fire.
	bool passengersFire{false};
	bool infantryOnly{false};
	// Its riders when it dies (OpenContain::onDie): the share of their maximum they take (DamagePercentToUnits, as
	// damage of `riderDamageType`, dying of `riderDeathType`: BURNED unless BurnedDeathToUnits = No); whether riders
	// that may not get out are killed first (TransportContain::killRidersWhoAreNotFreeToExit, of `stuckDeathType`), or
	// deleted (DestroyRidersWhoAreNotFreeToExit).
	bool checksRiderExit{false};
	bool deletesStuckRiders{false};
	// Its riders are held while aboard and fight as garrisoned (HelixContain::onContaining: DISABLED_HELD,
	// WEAPONBONUSCONDITION_GARRISONED), the game's rules.
	bool garrisonsRiders{false};
	// It carries a portable structure on top (AllowInsideKindOf PORTABLE_STRUCTURE: HelixContain, OverlordContain).
	bool mountsPortable{false};
	// Its mounted portable structure's experience goes to it (OverlordContain ExperienceSinkForRider, default Yes).
	bool experienceSink{false};
	// Carrying a portable structure that holds passengers (a Battle Bunker), it takes them in in its place: its room,
	// firing and rider damage become the structure's (OverlordContain::getRedirectedContain), the game's rules.
	bool redirectsToMount{false};
	// Its riders are deleted as it dies, not let out (RiderChangeContain::onRemoving: a dead bike destroys its rider),
	// after its rider damage (which may kill them first).
	bool deletesRiders{false};
	std::uint8_t reserved2[6]{};
	Engine::Math::Fixed riderDamage;
	std::uint32_t riderDamageType{0};
	std::uint32_t riderDeathType{0};
	std::uint32_t stuckDeathType{0};
	std::uint32_t reserved3{0};
	// The share of their maximum health its riders regain each tick (TransportContain HealthRegen%PerSec).
	Engine::Math::Fixed riderRegen;
	// How far above it its riders ride (HelixContain::redeployOccupants: 8), where it has no fire points.
	Engine::Math::Fixed riderHeight;
	// Its riders turn aggressive as they get out (TransportContain GoAggressiveOnExit: onRemoving sets their attitude).
	bool goAggressiveOnExit{false};
	// Its own weapon bonus conditions add to its riders' (OpenContain WeaponBonusPassedToPassengers).
	bool bonusToPassengers{false};
	std::uint8_t reserved4[6]{};
	// How long its door stands open after each one gets out (OpenContain DoorOpenTime, default a frame; 0: it leaves
	// its doors alone).
	std::uint64_t doorOpenTicks{1};
};
}
