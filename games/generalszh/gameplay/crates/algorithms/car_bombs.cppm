export module games.generalszh.gameplay.crates.algorithms.car_bombs;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.loadout_content;
import games.generalszh.content.objects.object_status;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.teams.algorithms.defection;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import games.generalszh.gameplay.abilities.algorithms.special_objects;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.loadout.components.loadout;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.movement.components.move_order;

// Terrorists making car bombs (ConvertToCarBombCrateCollide: the terrorist is the crate, the vehicle it runs into
// collects it):
// - canConvertObjectToCarBomb: the vehicle not dead, not fogged to a human player's order, and the collide would take it
//   (isValidToExecute: CrateCollide's own checks on the vehicle, then not AIRCRAFT or BOAT, not IS_CARBOMB, with a
//   CARBOMB weapon set, findWeaponTemplateSet's pick for it, not already on it).
// - aiEnter (the CONVERT_TO_CARBOMB button, its hunt, a player's enter): if canEnterObject lets it (its own checks, then
//   the collide's wish) it sets out for the vehicle (a boarding); AIEnterState checks again every tick: when it may no
//   longer enter, an enemy it can shoot is attacked instead (CMD source kept), else it idles.
// - On touching its goal object (PhysicsBehavior::onCollide, here the boarding's arrival; executeCrateBehavior needs
//   the vehicle to be the AI's goal): the vehicle takes its CARBOMB weapon set flag, FXList plays on it, it defects to
//   the terrorist's player's default team at once (defect(team, 0)), takes the terrorist's script name, vision and
//   shroud clearing range, IS_CARBOMB and veterancy level; ExecuteFX plays on it and the terrorist is gone
//   (destroyObject). A booby trap on the vehicle goes off first (the conversion waits for its blast); the radar
//   refresh is the presentation's.
export namespace generalszh::gameplay
{
namespace car_bomb_detail
{
namespace gp = engine::gameplay;

inline const content::CrateCollideContent *CollideOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? game.templates.CarBombOf(ref->index) : nullptr;
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline std::uint64_t CarBombStatus() noexcept { return std::uint64_t{1} << content::ObjectStatusBit("IS_CARBOMB"); }
}

// Whether it has a ConvertToCarBombCrateCollide.
inline bool CarBomber(const GameWorld &game, ecs::Entity unit) { return car_bomb_detail::CollideOf(game, unit) != nullptr; }

// ConvertToCarBombCrateCollide::isValidToExecute (its wouldLikeToCollideWith).
inline bool WouldConvertToCarBomb(GameWorld &game, ecs::Entity terrorist, ecs::Entity vehicle)
{
	using namespace car_bomb_detail;
	const content::CrateCollideContent *collide = CollideOf(game, terrorist);
	const content::ObjectDefinition *target = DefinitionOf(game, vehicle);
	if (collide == nullptr || target == nullptr || !CrateCollideAllows(game, terrorist, *collide, vehicle) || EffectivelyDead(game, vehicle))
		return false;
	if (target->Is("AIRCRAFT") || target->Is("BOAT"))
		return false;
	if (const auto *flags = game.world.Get<gp::StatusFlags>(vehicle); flags != nullptr && (flags->bits & CarBombStatus()) != 0)
		return false;
	if (!pilot_kill_detail::HasCarBombSet(*target))
		return false;
	const std::uint32_t carBomb = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
	const auto *loadout = game.world.Get<gp::Loadout>(vehicle);
	return loadout == nullptr || (loadout->weaponFlags & carBomb) == 0;
}

// ActionManager::canConvertObjectToCarBomb.
inline bool CanConvertToCarBomb(GameWorld &game, ecs::Entity terrorist, ecs::Entity vehicle, bool fromScript)
{
	return !EffectivelyDead(game, vehicle) && !ShroudedForAction(game, terrorist, vehicle, fromScript) && WouldConvertToCarBomb(game, terrorist, vehicle);
}

// ActionManager::canEnterObject for a car bomber at a vehicle: not itself, not dead, not fogged to a human player's
// order, neither under construction, not sold, neither IGNORED_IN_GUI nor the terrorist a MOB_NEXUS, not subdued, the
// terrorist neither STRUCTURE nor IMMOBILE; then the collide's wish (an unmanned vehicle is taken over first).
inline bool MayEnterAsCarBomb(GameWorld &game, ecs::Entity terrorist, ecs::Entity vehicle, bool fromScript)
{
	using namespace car_bomb_detail;
	const content::ObjectDefinition *self = DefinitionOf(game, terrorist);
	const content::ObjectDefinition *target = DefinitionOf(game, vehicle);
	auto &world = game.world;
	if (self == nullptr || target == nullptr || terrorist == vehicle || EffectivelyDead(game, vehicle) || ShroudedForAction(game, terrorist, vehicle, fromScript))
		return false;
	if (world.Has<gp::UnderConstruction>(terrorist) || world.Has<gp::UnderConstruction>(vehicle) || world.Has<gp::Sale>(vehicle))
		return false;
	if (self->Is("IGNORED_IN_GUI") || self->Is("MOB_NEXUS") || target->Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(vehicle); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	if (self->Is("STRUCTURE") || self->Is("IMMOBILE"))
		return false;
	return WouldConvertToCarBomb(game, terrorist, vehicle);
}

// AIUpdateInterface::privateEnter by a car bomber: mobile and allowed in (canEnterObject), it sets out for the vehicle
// (`commanded`: from a player or a script, else CMD_FROM_AI). False: not a car bomber's enter.
inline bool OrderConvertToCarBomb(GameWorld &game, ecs::Entity terrorist, ecs::Entity vehicle, bool commanded, bool fromScript)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!CarBomber(game, terrorist) || !world.IsAlive(vehicle) || world.Has<gp::Transport>(vehicle))
		return false;
	if (!world.Has<gp::MoveOrder>(terrorist) || world.Has<gp::Passenger>(terrorist) || !MayEnterAsCarBomb(game, terrorist, vehicle, fromScript))
		return true;
	if (commanded)
		Commanded(game, terrorist);
	else
		AiCommanded(game, terrorist);
	if (!world.Has<gp::Boarding>(terrorist))
		world.Add<gp::Boarding>(terrorist);
	*world.Get<gp::Boarding>(terrorist) = {vehicle};
	return true;
}

// ConvertToCarBombCrateCollide::executeCrateBehavior, then CrateCollide::onCollide's ExecuteFX and destroyObject.
inline void ConvertToCarBomb(GameWorld &game, ecs::Entity terrorist, ecs::Entity vehicle)
{
	using namespace car_bomb_detail;
	auto &world = game.world;
	const content::CrateCollideContent &collide = *CollideOf(game, terrorist);
	auto *loadout = world.Get<gp::Loadout>(vehicle);
	if (loadout != nullptr)
		loadout->weaponFlags |= content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
	// (Made without weapons: armed now by its car bomb set.)
	ArmFromLoadout(game, vehicle);
	const auto &at = world.Get<gp::Transform>(vehicle)->position;
	const std::uint32_t player = world.Get<gp::Owner>(terrorist)->player;
	auto &pickups = world.Resource<CratePickups>().list;
	if (!collide.fxList.empty())
		pickups.push_back({vehicle, CratePickup::Kind::CarBomb, 0, player, at, collide.fxList});
	const std::uint32_t team = game.roster.DefaultTeam(player).value_or(world.Get<gp::TeamMember>(terrorist)->team);
	Defect(game, vehicle, team, 0);
	if (const auto name = game.names.NameOf(terrorist))
	{
		game.names.Forget(vehicle);
		game.names.Assign(*name, vehicle);
	}
	// setVisionRange / setShroudClearingRange: the terrorist's.
	if (auto *aggression = world.Get<gp::Aggression>(vehicle))
	{
		const auto *theirs = world.Get<gp::Aggression>(terrorist);
		const auto *definition = world.Get<gp::DefinitionRef>(terrorist);
		aggression->vision = theirs != nullptr ? theirs->vision : game.templates.DefinitionAt(definition->index).visionRange;
		Engine::Math::Fixed reach;
		if (const auto *armament = world.Get<gp::Armament>(vehicle); armament != nullptr && armament->weapon != gp::WeaponCatalog::None)
			reach = game.templates.weapons.At(armament->weapon).attackRange;
		aggression->scanRange = std::max(aggression->vision, reach);
	}
	if (auto *vision = world.Get<gp::Vision>(vehicle))
		if (const auto *theirs = world.Get<gp::Vision>(terrorist))
			vision->clearingRange = theirs->clearingRange;
	if (!world.Has<gp::StatusFlags>(vehicle))
		world.Add<gp::StatusFlags>(vehicle);
	world.Get<gp::StatusFlags>(vehicle)->bits |= CarBombStatus();
	// ExperienceTracker::setVeterancyLevel: the terrorist's level, trainable or not.
	if (world.Has<gp::Experience>(vehicle))
		if (const auto *theirs = world.Get<gp::Experience>(terrorist))
			PlaceAtVeterancy(game, vehicle, theirs->level, true);
	if (!collide.executeFX.empty())
		pickups.push_back({vehicle, CratePickup::Kind::CarBomb, 0, player, at, collide.executeFX});
	RetireNow(game, {terrorist});
}

// The tick's car bomber boardings (a vehicle neither a transport nor unmanned): AIEnterState::update's canEnterObject
// failing, an enemy it can shoot is attacked (its last command source kept), else it idles; one at its vehicle
// converts it.
inline void ApplyCarBombs(GameWorld &game)
{
	using namespace car_bomb_detail;
	auto &world = game.world;
	const auto *requests = world.FindResource<gp::BoardRequests>();
	if (requests == nullptr)
		return;
	std::vector<gp::BoardRequest> entering;
	requests->ForEach([&](const gp::BoardRequest &request) {
		if (request.transport == ecs::Entity{} || !world.IsAlive(request.transport) || world.Has<gp::Transport>(request.transport) || !CarBomber(game, request.passenger))
			return;
		if (const auto *off = world.Get<gp::Disabled>(request.transport); off != nullptr && (off->mask & gp::disabled_type::Unmanned) != 0)
			return;
		entering.push_back(request);
	});
	for (const gp::BoardRequest &request : entering)
	{
		const ecs::Entity terrorist = request.passenger, vehicle = request.transport;
		if (!world.IsAlive(terrorist) || EffectivelyDead(game, terrorist))
			continue;
		// (Arrived, the cargo transfer has already ended its boarding.)
		if (!MayEnterAsCarBomb(game, terrorist, vehicle, false))
		{
			if (world.Has<gp::Boarding>(terrorist))
				world.Remove<gp::Boarding>(terrorist);
			const auto *activity = world.Get<gp::AiActivity>(terrorist);
			const bool commanded = activity != nullptr && activity->commanded != 0;
			const auto *mine = world.Get<gp::Owner>(terrorist);
			const auto *theirs = world.Get<gp::Owner>(vehicle);
			const auto *relationships = world.FindResource<gp::Relationships>();
			const auto *armament = world.Get<gp::Armament>(terrorist);
			const auto *targetable = world.Get<gp::Targetable>(vehicle);
			const bool enemy = mine != nullptr && theirs != nullptr && relationships != nullptr && relationships->Enemies(mine->player, theirs->player);
			if (enemy && armament != nullptr && armament->weapon != gp::WeaponCatalog::None && targetable != nullptr &&
				gp::CanTarget(game.templates.weapons.At(armament->weapon), targetable->classes) && !EffectivelyDead(game, vehicle))
			{
				OrderAttack(game, terrorist, vehicle);
				if (!commanded)
					AiCommanded(game, terrorist);
			}
			else
				AiIdle(game, terrorist);
			continue;
		}
		if (!request.arrived)
			continue;
		// checkAndDetonateBoobyTrap: the vehicle's trap goes off at the terrorist; the conversion waits for its blast (it
		// lands with the next tick's): the terrorist arrives again, and converts it unless one of them is then dead.
		if (CheckAndDetonateBoobyTrap(game, vehicle, terrorist))
		{
			if (!world.Has<gp::Boarding>(terrorist))
				world.Add<gp::Boarding>(terrorist);
			*world.Get<gp::Boarding>(terrorist) = {vehicle};
			continue;
		}
		ConvertToCarBomb(game, terrorist, vehicle);
	}
}
}
