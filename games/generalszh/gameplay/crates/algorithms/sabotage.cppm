export module games.generalszh.gameplay.crates.algorithms.sabotage;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.gameplay.crates.resources.crates;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.powers.components.spy_vision;
import games.generalszh.gameplay.powers.resources.cash_notices;
import games.generalszh.gameplay.hacking.algorithms.hack_effects;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.creation.components.ocl_timer;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.disabled_until;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.pending_damage;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.powers.components.special_power_timers;
import Engine.Core.Math.FixedRandom;
import engine.ecs.query.query;

// The Saboteur (its Sabotage*CrateCollide modules: the saboteur is the crate, the enemy building it enters collects it):
// - isValidToExecute, per module: CrateCollide's own checks on the building (BuildingPickup), not dead, of that module's
//   kinds (a power plant; a supply drop zone; a superweapon or strategy center; a command center; a supply center; a
//   barracks, war factory or airfield that is no aircraft carrier; a fake; an internet center), not under construction or
//   sold (the fork's fix), an enemy. canSabotageBuilding: alive, not fogged to a human player's order, an enemy, and one of
//   its modules would take it.
// - aiEnter (the SABOTAGE_BUILDING button, a player's enter): if canEnterObject lets it it sets out to touch the building;
//   AIEnterState checks again every tick: may it no longer, it idles.
// - On touching its goal (the first module, in order, that would take it; once it has, the saboteur is destroyed and no
//   other module collects it): Radar::tryInfiltrationEvent, doSabotageFeedbackFX (its sound on the building, a flash),
//   then the module's sabotage:
//   power plant: its player's power sabotaged for SabotagePowerDuration (onPowerBrownOutChange);
//   drop zone: its OCLUpdate timer restarted, then StealCashAmount (or what there is) stolen;
//   superweapon, command center: every special power on it starts recharging;
//   supply center: StealCashAmount (or what there is) stolen;
//   military factory: DISABLED_HACKED for SabotageDuration;
//   fake building: killed (its maximum health, UNRESISTABLE, DETONATED, from the saboteur);
//   internet center: every internet center of its player's spy vision off until SabotageDuration from now, it and the
//   hackers inside DISABLED_HACKED until then.
//   EVA's BuildingSabotaged for the building's player (CashStolen instead when cash was stolen; a supply center says
//   nothing unless cash was stolen or there was none). ExecuteFX on the building; the saboteur is gone (destroyObject).
export namespace generalszh::gameplay
{
namespace sabotage_detail
{
namespace gp = engine::gameplay;
using content::SabotageKind;

inline std::span<const content::SabotageCollideContent> CollidesOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? game.templates.SabotagesOf(ref->index) : std::span<const content::SabotageCollideContent>{};
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline bool Enemies(const GameWorld &game, ecs::Entity a, ecs::Entity b)
{
	const auto *mine = game.world.Get<gp::Owner>(a);
	const auto *theirs = game.world.Get<gp::Owner>(b);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	return mine != nullptr && theirs != nullptr && relationships != nullptr && relationships->Enemies(mine->player, theirs->player);
}

inline bool OfKind(const content::ObjectDefinition &building, SabotageKind kind)
{
	switch (kind)
	{
	case SabotageKind::PowerPlant: return building.Is("FS_POWER");
	case SabotageKind::SupplyDropzone: return building.Is("FS_SUPPLY_DROPZONE");
	case SabotageKind::Superweapon: return building.Is("FS_SUPERWEAPON") || building.Is("FS_STRATEGY_CENTER");
	case SabotageKind::CommandCenter: return building.Is("COMMANDCENTER");
	case SabotageKind::SupplyCenter: return building.Is("FS_SUPPLY_CENTER");
	case SabotageKind::MilitaryFactory:
		return !building.Is("AIRCRAFT_CARRIER") && (building.Is("FS_BARRACKS") || building.Is("FS_WARFACTORY") || building.Is("FS_AIRFIELD"));
	case SabotageKind::FakeBuilding: return building.Is("FS_FAKE");
	case SabotageKind::InternetCenter: return building.Is("FS_INTERNET_CENTER");
	}
	return false;
}

// Sabotage*CrateCollide::isValidToExecute.
inline bool Valid(GameWorld &game, ecs::Entity saboteur, const content::SabotageCollideContent &collide, ecs::Entity building)
{
	const content::ObjectDefinition *target = DefinitionOf(game, building);
	if (target == nullptr || !CrateCollideAllows(game, saboteur, collide.crate, building) || EffectivelyDead(game, building))
		return false;
	if (!OfKind(*target, collide.kind))
		return false;
	if (game.world.Has<gp::UnderConstruction>(building) || game.world.Has<gp::Sale>(building))
		return false;
	return Enemies(game, saboteur, building);
}

inline const content::SabotageCollideContent *FirstValid(GameWorld &game, ecs::Entity saboteur, ecs::Entity building)
{
	for (const content::SabotageCollideContent &collide : CollidesOf(game, saboteur))
		if (Valid(game, saboteur, collide, building))
			return &collide;
	return nullptr;
}

inline void Eva(GameWorld &game, EvaCue cue, std::uint32_t player)
{
	if (auto *eva = game.world.FindResource<EvaNotices>())
		eva->list.push_back({cue, EvaWeapon::None, player});
}

}

// Whether it has a Sabotage*CrateCollide.
inline bool IsSaboteur(const GameWorld &game, ecs::Entity unit) { return !sabotage_detail::CollidesOf(game, unit).empty(); }

// Its wouldLikeToCollideWith: one of its modules would take the building.
inline bool WouldSabotage(GameWorld &game, ecs::Entity saboteur, ecs::Entity building)
{
	return sabotage_detail::FirstValid(game, saboteur, building) != nullptr;
}

// ActionManager::canSabotageBuilding.
inline bool CanSabotageBuilding(GameWorld &game, ecs::Entity saboteur, ecs::Entity building, bool fromScript)
{
	return game.world.IsAlive(building) && !EffectivelyDead(game, building) && !ShroudedForAction(game, saboteur, building, fromScript) &&
		sabotage_detail::Enemies(game, saboteur, building) && WouldSabotage(game, saboteur, building);
}

// ActionManager::canEnterObject for a saboteur at a building: not itself, not dead, not fogged to a human player's order,
// neither under construction, not sold, neither IGNORED_IN_GUI nor the saboteur a MOB_NEXUS, not subdued, the saboteur
// neither STRUCTURE nor IMMOBILE; then its modules' wish.
inline bool MayEnterToSabotage(GameWorld &game, ecs::Entity saboteur, ecs::Entity building, bool fromScript)
{
	namespace gp = engine::gameplay;
	using namespace sabotage_detail;
	const content::ObjectDefinition *self = DefinitionOf(game, saboteur);
	const content::ObjectDefinition *target = DefinitionOf(game, building);
	auto &world = game.world;
	if (self == nullptr || target == nullptr || saboteur == building || EffectivelyDead(game, building) || ShroudedForAction(game, saboteur, building, fromScript))
		return false;
	if (world.Has<gp::UnderConstruction>(saboteur) || world.Has<gp::UnderConstruction>(building) || world.Has<gp::Sale>(building))
		return false;
	if (self->Is("IGNORED_IN_GUI") || self->Is("MOB_NEXUS") || target->Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(building); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	if (self->Is("STRUCTURE") || self->Is("IMMOBILE"))
		return false;
	return WouldSabotage(game, saboteur, building);
}

// AIUpdateInterface::privateEnter by a saboteur: mobile and allowed in, it sets out to touch the building (`commanded`:
// from a player or a script, else CMD_FROM_AI). False: not a saboteur's enter.
inline bool OrderSabotage(GameWorld &game, ecs::Entity saboteur, ecs::Entity building, bool commanded, bool fromScript)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!IsSaboteur(game, saboteur) || !world.IsAlive(building) || !sabotage_detail::Enemies(game, saboteur, building))
		return false;
	if (!world.Has<gp::MoveOrder>(saboteur) || world.Has<gp::Passenger>(saboteur) || !MayEnterToSabotage(game, saboteur, building, fromScript))
		return true;
	if (commanded)
		Commanded(game, saboteur);
	else
		AiCommanded(game, saboteur);
	if (!world.Has<gp::Boarding>(saboteur))
		world.Add<gp::Boarding>(saboteur);
	*world.Get<gp::Boarding>(saboteur) = {building, 1, fromScript ? 1u : 0u};
	return true;
}

// CrateCollide::onCollide for the first of its modules that takes the building: its executeCrateBehavior, ExecuteFX on the
// building, and the saboteur destroyed.
inline void Sabotage(GameWorld &game, ecs::Entity saboteur, ecs::Entity building)
{
	namespace gp = engine::gameplay;
	using namespace sabotage_detail;
	using Sound = AbilityNotices::Sabotage::Sound;
	const content::SabotageCollideContent *collide = FirstValid(game, saboteur, building);
	if (collide == nullptr)
		return;
	auto &world = game.world;
	const std::uint32_t victim = world.Get<gp::Owner>(building)->player;
	const Engine::Math::FixedVector3 at = world.Get<gp::Transform>(building)->position;
	const std::uint64_t now = game.tick;
	if (auto *notices = world.FindResource<InfiltrationNotices>())
		notices->list.push_back({victim, 0u, at});
	const Sound sound = collide->kind == SabotageKind::FakeBuilding ? Sound::None
		: collide->kind == SabotageKind::CommandCenter || collide->kind == SabotageKind::Superweapon ? Sound::ResetTimer
		: collide->kind == SabotageKind::SupplyDropzone || collide->kind == SabotageKind::SupplyCenter ? Sound::Withdraw : Sound::ShutDown;
	if (sound != Sound::None)
		if (auto *notices = world.FindResource<AbilityNotices>())
			notices->sabotages.push_back({building, sound});
	switch (collide->kind)
	{
	case SabotageKind::PowerPlant:
		Eva(game, EvaCue::BuildingSabotaged, victim);
		world.Resource<gp::PlayerEnergy>().Sabotage(victim, now + collide->durationTicks);
		break;
	case SabotageKind::SupplyDropzone:
	case SabotageKind::SupplyCenter:
	{
		if (collide->kind == SabotageKind::SupplyDropzone)
			if (auto *timer = world.Get<OclTimer>(building))
				if (const auto *definition = world.Get<gp::DefinitionRef>(building))
					if (const OclTimerConfig *config = game.templates.OclTimerOf(definition->index))
					{
						// OCLUpdate::resetTimer (setNextCreationFrame).
						timer->startedTick = now;
						timer->nextTick = now + static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, static_cast<std::int64_t>(config->minDelay),
							static_cast<std::int64_t>(std::max(config->minDelay, config->maxDelay))));
					}
		Eva(game, StealCash(game, saboteur, building, collide->stealCash) > 0 ? EvaCue::CashStolen : EvaCue::BuildingSabotaged, victim);
		break;
	}
	case SabotageKind::Superweapon:
	case SabotageKind::CommandCenter:
		Eva(game, EvaCue::BuildingSabotaged, victim);
		if (auto *timers = world.Get<gp::SpecialPowerTimers>(building))
		{
			const gp::PowerClock clock = ClockFor(game, victim);
			for (std::uint32_t index = 0; index < timers->count; ++index)
				gp::StartPowerRecharge(timers->timers[index], clock);
		}
		break;
	case SabotageKind::MilitaryFactory:
		Eva(game, EvaCue::BuildingSabotaged, victim);
		DisableHacked(game, building, now + collide->durationTicks);
		break;
	case SabotageKind::FakeBuilding:
		Eva(game, EvaCue::BuildingSabotaged, victim);
		if (const auto *health = world.Get<gp::Health>(building))
		{
			if (!world.Has<gp::PendingDamage>(building))
				world.Add<gp::PendingDamage>(building);
			*world.Get<gp::PendingDamage>(building) = gp::PendingDamage{saboteur, health->maximum, content::DamageTypeIndex("UNRESISTABLE").value_or(0),
				content::DeathTypeIndex("DETONATED").value_or(0)};
		}
		break;
	case SabotageKind::InternetCenter:
	{
		Eva(game, EvaCue::BuildingSabotaged, victim);
		const std::uint64_t until = now + collide->durationTicks;
		// disableInternetCenterSpyVision over its player's objects.
		ecs::Query<ecs::Write<SpyVision>, ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> spies(world);
		spies.ForEachChunk([&](auto chunk) {
			auto visions = chunk.template Get<SpyVision>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto refs = chunk.template Get<gp::DefinitionRef>();
			for (std::size_t row = 0; row < visions.size(); ++row)
				if (owners[row].player == victim && game.templates.DefinitionAt(refs[row].index).Is("FS_INTERNET_CENTER"))
					visions[row].disableUntil = until;
		});
		DisableHacked(game, building, until);
		const auto aboard = game.manifest.Aboard(building);
		for (const ecs::Entity hacker : std::vector<ecs::Entity>(aboard.begin(), aboard.end()))
			DisableHacked(game, hacker, until);
		break;
	}
	}
	const Engine::Math::FixedVector3 saboteurAt = world.Get<gp::Transform>(saboteur)->position;
	const std::uint32_t player = world.Get<gp::Owner>(saboteur)->player;
	if (!collide->crate.executeFX.empty() || !collide->crate.executeAnimation.empty())
	{
		CratePickup pickup{building, CratePickup::Kind::Sabotage, 0, player, saboteurAt, collide->crate.executeFX};
		pickup.animation = collide->crate.executeAnimation;
		pickup.animationSeconds = collide->crate.executeAnimationSeconds;
		pickup.animationRise = collide->crate.executeAnimationRise;
		pickup.animationFades = collide->crate.executeAnimationFades;
		world.Resource<CratePickups>().list.push_back(std::move(pickup));
	}
	RetireNow(game, {saboteur});
}

// The tick's saboteur approaches (Boarding touchOnly): one that may no longer enter idles (AIEnterState's failure); one
// touching its building sabotages it.
inline void ApplySabotages(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *requests = world.FindResource<gp::BoardRequests>();
	if (requests == nullptr)
		return;
	std::vector<gp::BoardRequest> touching;
	requests->ForEach([&](const gp::BoardRequest &request) {
		if (request.touchOnly && IsSaboteur(game, request.passenger))
			touching.push_back(request);
	});
	for (const gp::BoardRequest &request : touching)
	{
		const ecs::Entity saboteur = request.passenger, building = request.transport;
		if (!world.IsAlive(saboteur) || EffectivelyDead(game, saboteur) || !world.Has<gp::Boarding>(saboteur))
			continue;
		// AIEnterState: canEnterObject from its last command source.
		if (!world.IsAlive(building) || !MayEnterToSabotage(game, saboteur, building, world.Get<gp::Boarding>(saboteur)->fromScript != 0))
		{
			world.Remove<gp::Boarding>(saboteur);
			AiIdle(game, saboteur);
			continue;
		}
		if (!request.arrived)
			continue;
		world.Remove<gp::Boarding>(saboteur);
		Sabotage(game, saboteur, building);
	}
}
}
