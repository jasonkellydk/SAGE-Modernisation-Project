export module games.generalszh.gameplay.death.algorithms.death_aftermath;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.ecs.query.query;
import engine.ecs.core.world;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.subdual;
import engine.gameplay.common.status.components.disabled_until;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.rts.death.definitions.death_definition;
import engine.gameplay.rts.death.resources.death_events;
import engine.gameplay.rts.lifecycle.resources.casualties;
import games.generalszh.content.objects.object_definition;
import games.generalszh.gameplay.construction.algorithms.rebuild_holes;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.scripts.resources.script_records;
import games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;

// What this tick's deaths leave behind, after its systems, in their deterministic order: EVA's losses, creation lists'
// objects (taking the dead's health: TransferPreviousHealth), rubble, rebuild holes, crates and death weapons.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// The tick's deaths' effects in order: the deaths', then the dying's and the toppled structures'.
inline std::vector<gp::DeathEvent> AllDeathEvents(GameWorld &game)
{
	std::vector<gp::DeathEvent> events = game.world.Resource<gp::DeathEvents>().events;
	game.world.Resource<gp::DyingEvents>().ForEach([&](const gp::DeathEvent &event) { events.push_back(event); });
	game.world.Resource<gp::StructureToppleEvents>().ForEach([&](const gp::DeathEvent &event) { events.push_back(event); });
	return events;
}

// CreateObjectDie TransferPreviousHealth: what the dead made takes its subdual damage first (SUBDUAL_UNRESISTABLE: a
// body that can be subdued adds it up to its cap, subdued at once when it reaches its maximum health, its helper
// woken), then its damage (UNRESISTABLE, from its last attacker) and those attacking it (AIUpdateInterface::transferAttack).
inline void TransferPreviousHealth(GameWorld &game, const gp::DeathEvent &event, ecs::Entity made)
{
	if (gp::Subdual *subdual = game.world.Get<gp::Subdual>(made); subdual != nullptr && event.transferSubdual > Engine::Math::Fixed{})
	{
		subdual->damage = std::clamp(subdual->damage + event.transferSubdual, Engine::Math::Fixed{}, subdual->cap);
		subdual->touchTick = game.tick;
		subdual->hitTick = game.tick;
		subdual->awake = 1;
		subdual->gaining = 1;
		subdual->countdown = subdual->healTicks;
		if (const gp::Health *health = game.world.Get<gp::Health>(made); health != nullptr && health->maximum <= subdual->damage && subdual->subdued == 0)
		{
			subdual->subdued = 1;
			game.world.Resource<gp::DisableRequests>().list.push_back({made, gp::disabled_type::Subdued, 0, gp::DisabledForever});
		}
	}
	if (gp::Health *health = game.world.Get<gp::Health>(made); health != nullptr && event.transferDamage > Engine::Math::Fixed{})
	{
		health->current -= event.transferDamage;
		health->lastAttacker = event.transferSource;
		health->lastDamageTick = game.tick;
		if (gp::IsDead(*health))
		{
			health->current = {};
			game.kills.entities.push_back(made);
		}
	}
	ecs::Query<ecs::Write<gp::AttackTarget>> attackers(game.world);
	attackers.ForEachChunk([&](auto chunk) {
		for (gp::AttackTarget &target : chunk.template Get<gp::AttackTarget>())
			if (target.target == event.entity)
				target.target = made;
	});
}

// What this tick's deaths leave behind, in their deterministic order:
// objects from creation lists (wrecks, debris, pilots) and rubble now, and death
// weapons going off where they died next tick.
inline void CarryOutDeathAftermath(GameWorld &game)
{
	// Object::onDie: a victory-counting structure or an infantry or vehicle lost, not by its own hand, is EVA's.
	for (const gp::Casualty &casualty : game.casualties.list)
	{
		if (casualty.departure != gp::Departure::Killed || casualty.killer == casualty.entity || casualty.team >= game.roster.TeamCount() ||
			casualty.definition >= game.templates.DefinitionCount())
			continue;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(casualty.definition);
		const std::uint32_t owner = game.roster.TeamAt(casualty.team).owner;
		if (kind.Is("STRUCTURE") && kind.Is("MP_COUNT_FOR_VICTORY"))
			game.world.Resource<EvaNotices>().list.push_back({EvaCue::BuildingLost, EvaWeapon::None, owner});
		else if (kind.Is("INFANTRY") || kind.Is("VEHICLE"))
			game.world.Resource<EvaNotices>().list.push_back({EvaCue::UnitLost, EvaWeapon::None, owner});
	}
	for (const gp::DeathEvent &event : AllDeathEvents(game))
	{
		const std::string_view name = game.templates.DeathEffectName(event.kind, event.id);
		if (event.kind == gp::DeathEffectKind::Objects)
		{
			const ecs::Entity made = RunCreationList(game, name, {event.position, event.facing, event.team, event.entity, event.veterancy});
			if (event.transfer && game.world.IsAlive(made))
				TransferPreviousHealth(game, event, made);
		}
		else if (event.kind == gp::DeathEffectKind::Replace)
			ExposeRebuildHole(game, event.entity, event.definition, event.underConstruction, std::string(name), event.position, event.facing,
				event.team, game.names.Released(event.entity));
		else if (event.kind == gp::DeathEffectKind::Notice)
		{
			// SpecialPowerCompletionDie::notifyScriptEngine: with a creator, its player's scripts hear the power completed.
			if (event.credit != ecs::Entity{} && event.team < game.roster.TeamCount())
				game.world.Resource<ScriptRecords>().CompletedPower(game.roster.TeamAt(event.team).owner, std::string(name), event.credit);
		}
		else if (event.kind == gp::DeathEffectKind::Release)
			RemoveObjectUpgrade(game, event.credit, name); // UpgradeDie::onDie: its producer's
		else if (event.kind == gp::DeathEffectKind::Loot)
			DropCrate(game, name, event.killer, event.team, event.veterancy, event.position);
		else if (event.kind == gp::DeathEffectKind::Spawn)
			SpawnObject(game, std::string(name), event.position.XY(), event.facing, event.team == gp::NoTeam ? 0u : event.team, "");
		else if (event.kind == gp::DeathEffectKind::Weapon)
		{
			const std::uint32_t weapon = game.templates.Weapon(std::string(name));
			if (weapon == gp::WeaponCatalog::None)
				continue;
			const std::uint32_t player = event.team < game.roster.TeamCount() ? game.roster.TeamAt(event.team).owner : 0u;
			// The dying object is gone as it lands: its producer and kind go with the shot (whom its blast spares).
			gp::Shot shot{event.entity, {}, weapon, player, event.position, event.position, game.tick, game.tick + 1};
			shot.producer = event.credit;
			shot.kind = event.definition; // none: gp::Shot::NoKind
			game.world.Resource<gp::ShotQueue>().Add(shot);
		}
	}
}
}
