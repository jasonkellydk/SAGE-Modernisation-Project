export module games.generalszh.gameplay.combat.systems.enemy_near_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat.components.enemy_near;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.combat.components.aggression;

// EnemyNearUpdate::update / checkForEnemies for everything that watches for enemies, chunk-parallel: every
// ScanDelayTime + 1 ticks it looks (AI::findClosestEnemy with CAN_SEE) within its vision range, bounding circle to
// bounding circle, for a living enemy on the map that is no building (ATTACK_BUILDINGS not asked), not hidden to it
// (stealthed and undetected) and in its line of sight over the terrain (top of it to top of them); ENEMYNEAR shows while
// one was (the appearance reads `near`). Not ported: Pathfinder::isViewBlockedByObstacle (view blocked by a structure;
// the modern navigation has no view check yet).
export namespace generalszh::gameplay
{
struct EnemyNearSystem
{
	using Query = ecs::Query<ecs::Write<EnemyNear>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::TeamMember>, ecs::Optional<engine::gameplay::Aggression>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::GroundHeight>>;

	static Engine::Math::Fixed Top(const content::ObjectDefinition &kind)
	{
		return kind.geometry.shape == content::GeometryShape::Sphere ? kind.geometry.majorRadius : kind.geometry.height;
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const auto lookup = context.Lookup<Lookup>();
		auto watchers = chunk.Get<EnemyNear>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto aggressions = chunk.Get<gp::Aggression>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < watchers.size(); ++row)
		{
			EnemyNear &watcher = watchers[row];
			const std::uint32_t *delay = templates.EnemyNearOf(refs[row].index);
			if (delay == nullptr)
				continue;
			if (watcher.scanDelay != 0)
			{
				--watcher.scanDelay;
				continue;
			}
			watcher.scanDelay = *delay;
			const content::ObjectDefinition &self = templates.DefinitionAt(refs[row].index);
			const Fixed vision = aggressions.empty() ? self.visionRange : aggressions[row].vision;
			const Fixed radius = content::BoundingCircleRadius(self.geometry);
			const Engine::Math::FixedVector2 at = transforms[row].position.XY();
			Engine::Math::FixedVector3 eye = transforms[row].position;
			eye.z += Top(self);
			const std::uint32_t team = members.empty() ? gp::Relationships::NoTeam : members[row].team;
			bool found = false;
			// The index finds by centre: widened by the largest footprint a looker may meet, then measured exactly.
			spatial.ForEachWithin(at, vision + radius + Fixed::FromInt(250), [&](const gp::SpatialEntry &entry) {
				if (found || entry.entity == entities[row])
					return;
				if ((entry.classes & (gp::target_class::Structure | gp::target_class::Hidden)) != 0)
					return;
				if (relationships.Between(team, owners[row].player, entry.team, entry.player) != gp::Relationship::Enemies)
					return;
				const Fixed reach = vision + radius + entry.radius;
				if (Engine::Math::DistanceSquared(entry.position.XY(), at) > reach * reach)
					return;
				const auto *ref = lookup.Get<gp::DefinitionRef>(entry.entity);
				Engine::Math::FixedVector3 them = entry.position;
				if (ref != nullptr)
					them.z += Top(templates.DefinitionAt(ref->index));
				if (!ground.ClearLineOfSight(eye, them))
					return;
				found = true;
			});
			watcher.near = found ? 1u : 0u;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::EnemyNearSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.enemy_near";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
