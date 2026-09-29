export module games.generalszh.gameplay.crates.algorithms.hijacking;
import std;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.crates.components.hijacker;
export import games.generalszh.gameplay.crates.systems.hijacker_system;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.objects.object_status;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.containment.algorithms.parachuting;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import games.generalszh.gameplay.eva.resources.eva_notices;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.weapons.components.armament;

// Hijackers taking enemy vehicles (ConvertToHijackedVehicleCrateCollide: the hijacker is the crate, the vehicle it runs
// into collects it; HijackerUpdate: it rides hidden in what it took):
// - WouldHijack (isValidToExecute): CrateCollide's checks on the vehicle (RequiredKindOf VEHICLE), then not dead, not
//   IMMUNE_TO_CAPTURE, AIRCRAFT, BOAT or DRONE, not already HIJACKED, an enemy, and not a transport with anyone in it.
// - CanHijack (ActionManager::canHijackVehicle): not dead, not fogged to the order, an enemy VEHICLE that is neither
//   AIRCRAFT nor DRONE, and the collide would take it.
// - OrderHijack (aiEnter: the HIJACK_VEHICLE button, a player's enter): it sets out to touch the vehicle; AIEnterState
//   checks each tick, idling it when it may no longer.
// - Hijack (executeCrateBehavior, on touching its goal): the vehicle's player hears of an infiltration (and, its own
//   watcher, VehicleStolen); it joins the hijacker's player's default team HIJACKED, its AI stopped where it is and
//   idle; the HijackDriver sound; the hijacker's script name passes to it; both take the higher veterancy level. A
//   vehicle without an EjectPilotDie keeps no driver: the hijacker is gone. Else the hijacker rides in it
//   (NO_COLLISIONS, MASKED, UNSELECTABLE, off the map, idle) and the vehicle takes its vision and shroud clearing range.
export namespace generalszh::gameplay
{
namespace hijack_detail
{
namespace gp = engine::gameplay;

inline const content::CrateCollideContent *CollideOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? game.templates.HijackOf(ref->index) : nullptr;
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline std::uint64_t Status(std::string_view name) noexcept { return std::uint64_t{1} << content::ObjectStatusBit(name); }

inline bool HasStatus(const GameWorld &game, ecs::Entity entity, std::uint64_t bits)
{
	const auto *flags = game.world.Get<gp::StatusFlags>(entity);
	return flags != nullptr && (flags->bits & bits) != 0;
}

inline void SetStatus(GameWorld &game, ecs::Entity entity, std::uint64_t bits, bool on)
{
	if (!game.world.Has<gp::StatusFlags>(entity))
		game.world.Add<gp::StatusFlags>(entity);
	auto &flags = game.world.Get<gp::StatusFlags>(entity)->bits;
	flags = on ? flags | bits : flags & ~bits;
}

inline bool Enemies(const GameWorld &game, ecs::Entity a, ecs::Entity b)
{
	const auto *mine = game.world.Get<gp::Owner>(a);
	const auto *theirs = game.world.Get<gp::Owner>(b);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	return mine != nullptr && theirs != nullptr && relationships != nullptr && relationships->Enemies(mine->player, theirs->player);
}

// EjectPilotDie among its modules (getEjectPilotDieInterface), active or not.
inline bool CanEject(const content::ObjectDefinition &kind)
{
	return std::any_of(kind.modules.begin(), kind.modules.end(), [](const content::ModuleEntry &module) { return module.type == "EjectPilotDie"; });
}
}

// Whether it has a ConvertToHijackedVehicleCrateCollide.
inline bool IsHijacker(const GameWorld &game, ecs::Entity unit) { return hijack_detail::CollideOf(game, unit) != nullptr; }

inline bool WouldHijack(GameWorld &game, ecs::Entity hijacker, ecs::Entity vehicle)
{
	using namespace hijack_detail;
	const content::CrateCollideContent *collide = CollideOf(game, hijacker);
	const content::ObjectDefinition *target = DefinitionOf(game, vehicle);
	if (collide == nullptr || target == nullptr || !CrateCollideAllows(game, hijacker, *collide, vehicle) || EffectivelyDead(game, vehicle))
		return false;
	if (target->Is("IMMUNE_TO_CAPTURE") || target->Is("AIRCRAFT") || target->Is("BOAT") || target->Is("DRONE"))
		return false;
	if (HasStatus(game, vehicle, Status("HIJACKED")) || !Enemies(game, hijacker, vehicle))
		return false;
	if (target->Is("TRANSPORT") && game.world.Has<gp::Transport>(vehicle) && game.manifest.Count(vehicle) > 0)
		return false;
	return true;
}

inline bool CanHijack(GameWorld &game, ecs::Entity hijacker, ecs::Entity vehicle, bool fromScript)
{
	const content::ObjectDefinition *target = hijack_detail::DefinitionOf(game, vehicle);
	if (target == nullptr || EffectivelyDead(game, vehicle) || ShroudedForAction(game, hijacker, vehicle, fromScript))
		return false;
	if (!hijack_detail::Enemies(game, hijacker, vehicle) || !target->Is("VEHICLE") || target->Is("AIRCRAFT") || target->Is("DRONE"))
		return false;
	return WouldHijack(game, hijacker, vehicle);
}

// ActionManager::canEnterObject for a hijacker at a vehicle (as for a car bomber: its own checks, then the collide's).
inline bool MayEnterToHijack(GameWorld &game, ecs::Entity hijacker, ecs::Entity vehicle, bool fromScript)
{
	namespace gp = engine::gameplay;
	using namespace hijack_detail;
	const content::ObjectDefinition *self = DefinitionOf(game, hijacker);
	const content::ObjectDefinition *target = DefinitionOf(game, vehicle);
	auto &world = game.world;
	if (self == nullptr || target == nullptr || hijacker == vehicle || EffectivelyDead(game, vehicle) || ShroudedForAction(game, hijacker, vehicle, fromScript))
		return false;
	if (world.Has<gp::UnderConstruction>(hijacker) || world.Has<gp::UnderConstruction>(vehicle) || world.Has<gp::Sale>(vehicle))
		return false;
	if (self->Is("IGNORED_IN_GUI") || self->Is("MOB_NEXUS") || target->Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(vehicle); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	if (self->Is("STRUCTURE") || self->Is("IMMOBILE"))
		return false;
	return WouldHijack(game, hijacker, vehicle);
}

// AIUpdateInterface::privateEnter by a hijacker: mobile and allowed (canEnterObject), it sets out to touch the vehicle.
// False: not a hijacker's enter.
inline bool OrderHijack(GameWorld &game, ecs::Entity hijacker, ecs::Entity vehicle, bool commanded, bool fromScript)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!IsHijacker(game, hijacker) || !world.IsAlive(vehicle) || !hijack_detail::Enemies(game, hijacker, vehicle))
		return false;
	if (!world.Has<gp::MoveOrder>(hijacker) || world.Has<gp::Passenger>(hijacker) || !MayEnterToHijack(game, hijacker, vehicle, fromScript))
		return true;
	if (commanded)
		Commanded(game, hijacker);
	else
		AiCommanded(game, hijacker);
	if (!world.Has<gp::Boarding>(hijacker))
		world.Add<gp::Boarding>(hijacker);
	*world.Get<gp::Boarding>(hijacker) = {vehicle, 1};
	return true;
}

inline void Hijack(GameWorld &game, ecs::Entity hijacker, ecs::Entity vehicle)
{
	namespace gp = engine::gameplay;
	using namespace hijack_detail;
	auto &world = game.world;
	const std::uint32_t victim = world.Get<gp::Owner>(vehicle)->player;
	const gp::Transform where = *world.Get<gp::Transform>(vehicle);
	// Radar::tryInfiltrationEvent, and EVA's VehicleStolen for the vehicle's own watcher.
	if (auto *notices = world.FindResource<InfiltrationNotices>())
		notices->list.push_back({victim, 0u, where.position});
	if (auto *eva = world.FindResource<EvaNotices>())
		eva->list.push_back({EvaCue::VehicleStolen, EvaWeapon::None, victim});
	const std::uint32_t player = world.Get<gp::Owner>(hijacker)->player;
	const std::uint32_t team = game.roster.DefaultTeam(player).value_or(world.Get<gp::TeamMember>(hijacker)->team);
	ChangeTeam(game, vehicle, team);
	SetStatus(game, vehicle, Status("HIJACKED"), true);
	// aiMoveToPosition(where it is), then aiIdle (a dozer's tasks cancelled with its AI).
	OrderMove(game, vehicle, where.position.XY(), false, false);
	AiIdle(game, vehicle);
	if (auto *notices = world.FindResource<AbilityNotices>())
		notices->hijacks.push_back(hijacker);
	if (const auto name = game.names.NameOf(hijacker))
	{
		game.names.Forget(vehicle);
		game.names.Assign(*name, vehicle);
	}
	const auto *theirs = world.Get<gp::Experience>(vehicle);
	const auto *mine = world.Get<gp::Experience>(hijacker);
	if (theirs != nullptr && mine != nullptr)
	{
		const std::uint8_t level = std::max(theirs->level, mine->level);
		PlaceAtVeterancy(game, hijacker, level, true);
		PlaceAtVeterancy(game, vehicle, level, true);
	}
	const content::ObjectDefinition *kind = DefinitionOf(game, vehicle);
	if (kind == nullptr || !CanEject(*kind))
	{
		RetireNow(game, {hijacker});
		return;
	}
	if (!world.Has<Hijacker>(hijacker))
		world.Add<Hijacker>(hijacker);
	auto &state = *world.Get<Hijacker>(hijacker);
	state.target = vehicle;
	state.inVehicle = 1;
	state.update = 1;
	SetStatus(game, hijacker, Status("NO_COLLISIONS") | Status("MASKED") | Status("UNSELECTABLE"), true);
	AiIdle(game, hijacker);
	// setVisionRange / setShroudClearingRange: the hijacker's.
	if (auto *aggression = world.Get<gp::Aggression>(vehicle))
		{
			const auto *own = world.Get<gp::Aggression>(hijacker);
			aggression->vision = own != nullptr ? own->vision : game.templates.DefinitionAt(world.Get<gp::DefinitionRef>(hijacker)->index).visionRange;
			Engine::Math::Fixed reach;
			if (const auto *armament = world.Get<gp::Armament>(vehicle); armament != nullptr && armament->weapon != gp::WeaponCatalog::None)
				reach = game.templates.weapons.At(armament->weapon).attackRange;
			aggression->scanRange = std::max(aggression->vision, reach);
		}
	if (auto *vision = world.Get<gp::Vision>(vehicle))
		if (const auto *own = world.Get<gp::Vision>(hijacker))
			vision->clearingRange = own->clearingRange;
	// unRegisterObject and setDrawableHidden: off the map, riding with the vehicle.
	if (!world.Has<gp::OffMap>(hijacker))
		world.Add<gp::OffMap>(hijacker);
	*world.Get<gp::OffMap>(hijacker) = gp::OffMap{3, false, {}, vehicle};
	if (auto *move = world.Get<gp::MoveOrder>(hijacker))
		*move = {};
}

// The tick's hijacker approaches (Boarding touchOnly): one that may no longer enter idles (AIEnterState's failure);
// one touching its vehicle hijacks it.
inline void ApplyHijacks(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *requests = world.FindResource<gp::BoardRequests>();
	if (requests == nullptr)
		return;
	std::vector<gp::BoardRequest> touching;
	requests->ForEach([&](const gp::BoardRequest &request) {
		if (request.touchOnly && IsHijacker(game, request.passenger))
			touching.push_back(request);
	});
	for (const gp::BoardRequest &request : touching)
	{
		const ecs::Entity hijacker = request.passenger, vehicle = request.transport;
		if (!world.IsAlive(hijacker) || EffectivelyDead(game, hijacker) || !world.Has<gp::Boarding>(hijacker))
			continue;
		if (!world.IsAlive(vehicle) || !MayEnterToHijack(game, hijacker, vehicle, false))
		{
			world.Remove<gp::Boarding>(hijacker);
			AiIdle(game, hijacker);
			continue;
		}
		if (!request.arrived)
			continue;
		world.Remove<gp::Boarding>(hijacker);
		Hijack(game, hijacker, vehicle);
	}
}

// The tick's HijackerEvents: riding, it is where the vehicle is and both take the higher level; let out, it is back on
// the map (collisions, unmasked, selectable) and idle, by parachute (ParachuteName) where the vehicle last was when that
// was aloft.
inline void ApplyHijackerEvents(GameWorld &game, std::span<const HijackerEvent> events)
{
	namespace gp = engine::gameplay;
	using namespace hijack_detail;
	auto &world = game.world;
	for (const HijackerEvent &event : events)
	{
		if (!world.IsAlive(event.hijacker))
			continue;
		if (event.kind == HijackerEvent::Kind::Follow)
		{
			world.Get<gp::Transform>(event.hijacker)->position = event.at;
			for (const ecs::Entity entity : {event.hijacker, event.vehicle})
				if (const auto *experience = world.IsAlive(entity) ? world.Get<gp::Experience>(entity) : nullptr; experience != nullptr && experience->level < event.level)
					PlaceAtVeterancy(game, entity, event.level, true);
			continue;
		}
		if (world.Has<gp::OffMap>(event.hijacker))
			world.Remove<gp::OffMap>(event.hijacker);
		SetStatus(game, event.hijacker, Status("NO_COLLISIONS") | Status("MASKED") | Status("UNSELECTABLE"), false);
		AiIdle(game, event.hijacker);
		if (event.airborne == 0)
			continue;
		const content::ObjectDefinition *kind = DefinitionOf(game, event.hijacker);
		const std::string parachute = kind != nullptr ? game.templates.HijackerParachuteOf(world.Get<gp::DefinitionRef>(event.hijacker)->index) : std::string{};
		if (parachute.empty())
			continue;
		const ecs::Entity chute = SpawnObject(game, parachute, event.at.XY(), {}, world.Get<gp::TeamMember>(event.hijacker)->team, {});
		if (!world.IsAlive(chute))
			continue;
		world.Get<gp::Transform>(chute)->position = event.at;
		PutInParachute(game, chute, event.hijacker);
	}
}
}
