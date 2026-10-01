export module games.generalszh.gameplay.combat.algorithms.neutron_blasts;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.combat.algorithms.unmanned_vehicles;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.world.resources.deselections;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.gameplay.rts.lifecycle.resources.kill_requests;

// NeutronBlastBehavior::onDie (the Nuke General's neutron shells, the neutron missile's blast), after the step, for the
// tick's deaths of things that carry it (its shells among them: DetonateCallsKill kills them where they go off): everything alive within BlastRadius of it (flat, centre to centre; on the map), not itself, airborne or an
// aircraft only when AffectAirborne, an ally of it only when AffectAllies:
// - infantry is killed;
// - whatever it holds, all killed (killAllContained);
// - a vehicle not a drone: a cliff jumper is killed; else it loses its crew (setDisabled(DISABLED_UNMANNED): a car bomb
//   blows, anything else drops its veterancy), idles, is deselected and goes to the neutral player.
export namespace generalszh::gameplay
{
namespace neutron_blast_detail
{
namespace gp = engine::gameplay;

inline void Blast(GameWorld &game, const NeutronBlastConfig &blast, Engine::Math::FixedVector2 centre, ecs::Entity self, std::uint32_t player,
	std::uint32_t team)
{
	auto &world = game.world;
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	if (spatial == nullptr || relationships == nullptr)
		return;
	std::vector<gp::SpatialEntry> caught;
	spatial->ForEachWithin(centre, blast.radius, [&](const gp::SpatialEntry &entry) {
		if (Engine::Math::DistanceSquared(entry.position.XY(), centre) <= blast.radius * blast.radius)
			caught.push_back(entry);
	});
	auto &kills = world.Resource<gp::KillRequests>().entities;
	constexpr std::uint32_t Airborne = gp::target_class::AirborneVehicle | gp::target_class::AirborneInfantry;
	for (const gp::SpatialEntry &entry : caught)
	{
		const ecs::Entity victim = entry.entity;
		const auto *ref = world.IsAlive(victim) ? world.Get<gp::DefinitionRef>(victim) : nullptr;
		if (victim == self || ref == nullptr || EffectivelyDead(game, victim))
			continue;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
		if (!blast.affectAirborne && (kind.Is("AIRCRAFT") || (entry.classes & Airborne) != 0))
			continue;
		if (!blast.affectAllies && relationships->Between(team, player, entry.team, entry.player) == gp::Relationship::Allies)
			continue;
		if (kind.Is("INFANTRY"))
			kills.push_back(victim);
		for (const ecs::Entity rider : game.manifest.Aboard(victim))
			kills.push_back(rider);
		if (!kind.Is("VEHICLE") || kind.Is("DRONE"))
			continue;
		if (kind.Is("CLIFF_JUMPER"))
		{
			kills.push_back(victim);
			continue;
		}
		unmanned_detail::SetDisabledFlag(game, victim, gp::disabled_type::Unmanned, true);
		if (pilot_kill_detail::HasCarBombSet(kind))
		{
			kills.push_back(victim);
			continue;
		}
		unmanned_detail::ClearExperience(game, victim);
		AiIdle(game, victim);
		if (auto *deselections = world.FindResource<Deselections>())
			deselections->list.push_back(victim);
		if (const auto neutral = game.roster.FindTeam("team"))
			ChangeTeam(game, victim, *neutral);
	}
}
}

inline void ApplyNeutronBlasts(GameWorld &game, const engine::gameplay::Casualties &casualties, const engine::gameplay::Detonations &detonations)
{
	namespace gp = engine::gameplay;
	for (const gp::Casualty &casualty : casualties.list)
		if (casualty.departure == gp::Departure::Killed)
			if (const NeutronBlastConfig *blast = game.templates.NeutronBlastOf(casualty.definition))
			{
				const std::uint32_t player = casualty.team < game.roster.TeamCount() ? game.roster.TeamAt(casualty.team).owner : 0u;
				neutron_blast_detail::Blast(game, *blast, casualty.transform.position.XY(), casualty.entity, player, casualty.team);
			}
	// Its shells that go off die then (DetonateCallsKill: the impact kills them), so their blasts come with the deaths above.
	(void)detonations;
}
}
