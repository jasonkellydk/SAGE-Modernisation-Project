export module games.generalszh.gameplay.powers.algorithms.leaflet_drops;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.bounding_volume;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.status.algorithms.disable_now;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.ecs.query.query;

// LeafletDropBehavior (EMPUpdate.cpp; the leaflet drop's container), after the step:
// - its first update: LeafletFXParticleSystem started riding on it. (Then, still before Delay, it sleeps for good: the
//   retail update never runs again, nothing wakes it; its disabling comes only with its death. Kept.)
// - onDie -> doDisableAttack: every enemy (their view of it) infantry or vehicle within AffectRadius of it, point to
//   bounding sphere in 3D, is DISABLED_EMP until DisabledDuration from now.
export namespace generalszh::gameplay
{
inline void ApplyLeafletDrops(GameWorld &game, const engine::gameplay::Casualties &casualties)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	// First updates: the leaflets.
	std::vector<ecs::Entity> fresh;
	ecs::Query<ecs::Write<LeafletDrop>> drops(world);
	drops.ForEachChunk([&](auto chunk) {
		auto rows = chunk.template Get<LeafletDrop>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
			if (rows[row].fxFired == 0)
			{
				rows[row].fxFired = 1;
				fresh.push_back(entities[row]);
			}
	});
	for (const ecs::Entity drop : fresh)
		if (const auto *config = game.templates.LeafletDropOf(world.Get<gp::DefinitionRef>(drop)->index); config != nullptr && !config->particles.empty())
			if (auto *cues = world.FindResource<EffectCues>())
				cues->list.push_back({config->particles, world.Get<gp::Transform>(drop)->position, drop, true});
	// Deaths: doDisableAttack.
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	if (spatial == nullptr || relationships == nullptr)
		return;
	for (const gp::Casualty &casualty : casualties.list)
	{
		if (casualty.departure != gp::Departure::Killed)
			continue;
		const LeafletDropConfig *config = game.templates.LeafletDropOf(casualty.definition);
		if (config == nullptr || config->radius <= Engine::Math::Fixed{})
			continue;
		const Engine::Math::FixedVector3 at = casualty.transform.position;
		const std::uint32_t player = casualty.team < game.roster.TeamCount() ? game.roster.TeamAt(casualty.team).owner : 0u;
		std::vector<gp::SpatialEntry> near;
		// The index finds by centre: widened by the largest bounding sphere, then measured exactly.
		spatial->ForEachWithin(at.XY(), config->radius + Engine::Math::Fixed::FromInt(250), [&](const gp::SpatialEntry &entry) { near.push_back(entry); });
		for (const gp::SpatialEntry &entry : near)
		{
			const ecs::Entity victim = entry.entity;
			const auto *ref = world.IsAlive(victim) && victim != casualty.entity ? world.Get<gp::DefinitionRef>(victim) : nullptr;
			if (ref == nullptr)
				continue;
			const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
			if (!kind.Is("INFANTRY") && !kind.Is("VEHICLE"))
				continue;
			if (relationships->Between(entry.team, entry.player, casualty.team, player) != gp::Relationship::Enemies)
				continue;
			// FROM_BOUNDINGSPHERE_3D: from the spot to its bounding sphere.
			const auto *volume = world.Get<gp::BoundingVolume>(victim);
			Engine::Math::FixedVector3 centre = entry.position;
			Engine::Math::Fixed sphere;
			if (volume != nullptr)
			{
				centre.z += volume->centerLift;
				sphere = volume->sphereRadius;
			}
			const Engine::Math::Fixed reach = config->radius + sphere;
			if (Engine::Math::LengthSquared(centre - at) > reach * reach)
				continue;
			gp::DisableNow(world, victim, gp::disabled_type::Emp, game.tick + config->durationTicks);
		}
	}
}
}
