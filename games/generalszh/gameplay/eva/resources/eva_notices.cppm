export module games.generalszh.gameplay.eva.resources.eva_notices;
import std;

import engine.ecs.system.system;
export import Engine.Core.Math.FixedVector;

// What the simulation told EVA this tick (the original's TheEva->setShouldPlay calls from the logic), for the
// presentation's EVA to turn into announcements for whoever watches: each with the player it concerns (the owner of
// what was lost, launched or built; the general who ranked) and, for superweapons, which kind. Whether it is the
// watcher's own, an ally's or an enemy's is the presentation's to decide. Per tick: not saved.
export namespace generalszh::gameplay
{
enum class EvaCue : std::uint8_t
{
	UnitLost,            // Object::onDie: an infantry or vehicle, not self-inflicted
	BuildingLost,        // Object::onDie: a structure that counts for victory, not self-inflicted
	GeneralLevelUp,      // Player::setRankLevel
	SuperweaponDetected, // Player::onStructureConstructionComplete
	SuperweaponLaunched, // SpecialPowerModule::aboutToDoSpecialPower
	BuildingBeingStolen, // SpecialAbilityUpdate::startPreparation: an infantry capture begins (the building's owner)
	BuildingStolen,      // SpecialAbilityUpdate::triggerAbilityEffect: a building captured (its owner till then)
	VehicleStolen,       // ConvertToHijackedVehicleCrateCollide::executeCrateBehavior: a vehicle hijacked (its owner till then)
};

enum class EvaWeapon : std::uint8_t
{
	None,
	ParticleCannon, // SPECIAL_PARTICLE_UPLINK_CANNON (and the Superweapon and Laser generals')
	Nuke,           // SPECIAL_NEUTRON_MISSILE (and the Nuke and Superweapon generals')
	ScudStorm,      // SPECIAL_SCUD_STORM
	GpsScrambler,   // SPECIAL_GPS_SCRAMBLER (and the Stealth general's)
	SneakAttack,    // SPECIAL_SNEAK_ATTACK
};

struct EvaNotice
{
	EvaCue cue{EvaCue::UnitLost};
	EvaWeapon weapon{EvaWeapon::None};
	std::uint32_t player{0};
};

struct EvaNotices
{
	std::vector<EvaNotice> list;
};

// Radar::tryInfiltrationEvent calls from the logic this tick: something of `player`'s infiltrated (a capture begun, a
// defection) at `position`; the presentation warns that player if it is the watcher. Per tick: not saved.
struct InfiltrationNotice
{
	std::uint32_t player{0};
	std::uint32_t reserved{0};
	Engine::Math::FixedVector3 position;
};

struct InfiltrationNotices
{
	std::vector<InfiltrationNotice> list;
};

// The superweapon kind a SpecialPower's Enum announces as.
inline EvaWeapon EvaWeaponOf(std::string_view type)
{
	if (type == "SPECIAL_PARTICLE_UPLINK_CANNON" || type == "SUPW_SPECIAL_PARTICLE_UPLINK_CANNON" || type == "LAZR_SPECIAL_PARTICLE_UPLINK_CANNON")
		return EvaWeapon::ParticleCannon;
	if (type == "SPECIAL_NEUTRON_MISSILE" || type == "NUKE_SPECIAL_NEUTRON_MISSILE" || type == "SUPW_SPECIAL_NEUTRON_MISSILE")
		return EvaWeapon::Nuke;
	if (type == "SPECIAL_SCUD_STORM")
		return EvaWeapon::ScudStorm;
	if (type == "SPECIAL_GPS_SCRAMBLER" || type == "SLTH_SPECIAL_GPS_SCRAMBLER")
		return EvaWeapon::GpsScrambler;
	if (type == "SPECIAL_SNEAK_ATTACK")
		return EvaWeapon::SneakAttack;
	return EvaWeapon::None;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::EvaNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.eva_notices";
};

template<>
struct ResourceTraits<generalszh::gameplay::InfiltrationNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.infiltration_notices";
};
}
