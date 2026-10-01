export module engine.gameplay.common.healing.systems.area_healing_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.common.healing.components.healing;
export import engine.gameplay.common.healing.resources.heal_pulses;
export import engine.gameplay.common.healing.systems.self_healing_system;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;

// Area healers, in parallel per chunk: on their tick each offers its heal to
// every ally in range (or to all of its own player's), filtered by target
// class, found in this tick's spatial index (never by reading other
// entities). The offers are gathered by target for the healing pass.
export namespace engine::gameplay
{
struct AreaHealingSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Write<AreaHealing>, ecs::Optional<Health>, ecs::Exclude<OffMap>, ecs::Optional<Disabled>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Write<HealOffers>, ecs::Write<HealPulses>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<HealOffers>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		auto &offers = context.Write<HealOffers>().Slot(context);
		const std::uint64_t tick = context.Tick();
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto teamRows = chunk.Get<TeamMember>();
		auto healers = chunk.Get<AreaHealing>();
		const auto healths = chunk.Get<Health>();
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < healers.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			if (!healths.empty() && IsDead(healths[row]))
				continue;
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			AreaHealing &healing = healers[row];
			for (std::uint32_t index = 0; index < healing.count; ++index)
			{
				AreaHealProgram &healer = healing.programs[index];
				if (tick < healer.nextTick || (healer.flags & (area_healing::Spent | area_healing::Dormant)) != 0)
					continue;
				const bool wholePlayer = (healer.flags & area_healing::WholePlayer) != 0;
				const auto offer = [&](const SpatialEntry &entry) {
					if ((entry.classes & healer.classes) == 0 || (entry.classes & healer.forbiddenClasses) != 0)
						return;
					if ((healer.flags & area_healing::SkipSelf) != 0 && entry.entity == entities[row])
						return;
					if (wholePlayer ? entry.player != player : relationships.Between(team, player, entry.team, entry.player) != Relationship::Allies)
						return;
					// The whole-player heal takes from anyone; the area heal locks its target.
					offers.push_back({entry.entity, entities[row], healer.amount, wholePlayer ? 0 : healer.delay, entry.position});
				};
				if (wholePlayer)
					for (const SpatialEntry &entry : spatial.Entries())
						offer(entry);
				else
					spatial.ForEachWithin(transforms[row].position.XY(), healer.radius, offer);
				healer.nextTick = tick + healer.delay;
				if ((healer.flags & area_healing::SingleBurst) != 0)
					healer.flags |= area_healing::Spent;
			}
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const { context.Write<HealPulses>().Gather(context.Write<HealOffers>()); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AreaHealingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_healing";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's damage (dead healers do not heal) and self healing.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::HealthSystem, engine::gameplay::SelfHealingSystem>;
};
}
