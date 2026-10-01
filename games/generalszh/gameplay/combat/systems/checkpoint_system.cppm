export module games.generalszh.gameplay.combat.systems.checkpoint_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat.components.checkpoint;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.teams.resources.team_roster;

// CheckpointUpdate::update / checkForAlliesAndEnemies for every checkpoint, chunk-parallel, every tick: with its
// geometry at its widest it looks within its vision range, bounding circle to bounding circle (FROM_BOUNDINGSPHERE_2D),
// for the closest living enemy on the map (AI::findClosestEnemy with no qualifiers: not hidden to it, stealthed and
// undetected; no building unless a computer player owns the checkpoint, or it is a base defence or a container that may
// attack: PartitionFilterRejectBuildings) and the closest living ally (AI::findClosestAlly: never a building). The gate
// is open while an ally is near and no enemy is: when either finding changed, the model turns to DOOR_1_OPENING (open)
// or DOOR_1_CLOSING; each tick an open gate's minor radius falls 0.333 while above zero and a shut one's rises 0.333
// while below its widest (so its bounding circle, Object::setGeometryInfo re-registering it with the partition,
// follows; the pathfinding map keeps the footprint it was stamped with).
export namespace generalszh::gameplay
{
struct CheckpointSystem
{
	using Query = ecs::Query<ecs::Write<Checkpoint>, ecs::Write<engine::gameplay::Targetable>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::TeamMember>,
		ecs::Optional<engine::gameplay::Aggression>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::Transport>,
		ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::Sale>, ecs::Read<engine::gameplay::Disabled>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::TeamRoster>>;

	// GeometryInfo::setMinorRadius's step (0.333f a frame).
	static Engine::Math::Fixed Step() noexcept { return Engine::Math::Fixed::FromRatio(333, 1000); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const auto lookup = context.Lookup<Lookup>();
		auto gates = chunk.Get<Checkpoint>();
		auto targets = chunk.Get<gp::Targetable>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto aggressions = chunk.Get<gp::Aggression>();
		const auto entities = chunk.Entities();
		// PartitionFilterRejectBuildings's exceptions: a base defence, or a container that may attack (not being built,
		// sold or subdued).
		const auto takesBuilding = [&](ecs::Entity building) {
			if (const auto *ref = lookup.Get<gp::DefinitionRef>(building); ref != nullptr && templates.DefinitionAt(ref->index).Is("FS_BASE_DEFENSE"))
				return true;
			if (lookup.Get<gp::Transport>(building) == nullptr || lookup.Get<gp::UnderConstruction>(building) != nullptr || lookup.Get<gp::Sale>(building) != nullptr)
				return false;
			const gp::Disabled *off = lookup.Get<gp::Disabled>(building);
			return off == nullptr || (off->mask & gp::disabled_type::Subdued) == 0;
		};
		for (std::size_t row = 0; row < gates.size(); ++row)
		{
			Checkpoint &gate = gates[row];
			const content::ObjectDefinition &self = templates.DefinitionAt(refs[row].index);
			const Fixed vision = aggressions.empty() ? self.visionRange : aggressions[row].vision;
			content::Geometry widest = self.geometry;
			widest.minorRadius = gate.maxMinorRadius;
			const Fixed radius = content::BoundingCircleRadius(widest);
			const Engine::Math::FixedVector2 at = transforms[row].position.XY();
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = members.empty() ? gp::Relationships::NoTeam : members[row].team;
			const bool computer = player < roster.PlayerCount() && !roster.PlayerAt(player).human;
			bool ally = false, enemy = false;
			spatial.ForEachWithin(at, vision + radius + Fixed::FromInt(250), [&](const gp::SpatialEntry &entry) {
				if ((ally && enemy) || entry.entity == entities[row] || lookup.Get<gp::Dying>(entry.entity) != nullptr)
					return;
				const Fixed reach = vision + radius + entry.radius;
				if (Engine::Math::DistanceSquared(entry.position.XY(), at) > reach * reach)
					return;
				const gp::Relationship relation = relationships.Between(team, player, entry.team, entry.player);
				const bool building = (entry.classes & gp::target_class::Structure) != 0;
				if (relation == gp::Relationship::Allies && !building)
					ally = true;
				else if (relation == gp::Relationship::Enemies && (entry.classes & gp::target_class::Hidden) == 0 &&
					(!building || computer || takesBuilding(entry.entity)))
					enemy = true;
			});
			const bool changed = (gate.allyNear != 0) != ally || (gate.enemyNear != 0) != enemy;
			gate.allyNear = ally ? 1u : 0u;
			gate.enemyNear = enemy ? 1u : 0u;
			const bool open = !enemy && ally;
			if (changed)
				gate.gate = open ? CheckpointGate::Opening : CheckpointGate::Closing;
			if (open && gate.minorRadius > Fixed{})
				gate.minorRadius -= Step();
			else if (!open && gate.minorRadius < gate.maxMinorRadius)
				gate.minorRadius += Step();
			content::Geometry now = self.geometry;
			now.minorRadius = gate.minorRadius;
			targets[row].radius = content::BoundingCircleRadius(now);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::CheckpointSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.checkpoint";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
