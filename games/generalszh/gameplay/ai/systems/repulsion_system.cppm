export module games.generalszh.gameplay.ai.systems.repulsion_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.repulsion;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.combat.systems.targeting_system;
import Engine.Core.Math.FixedRandom;

// Each runner's looks for a repulsor, chunk-parallel (AIIdleState, AIWanderState, AIPanicState, AIWanderInPlaceState,
// AIMoveAwayFromRepulsorsState):
// - Wandering, panicking or wandering in place: every waitFrames (the first at once) it looks; a repulsor fails the
//   state into running away from it.
// - Idle (and not busy): it looks as it goes idle, then every idle look period (the first longer by a random offset up
//   to that period); a repulsor: it runs away from it.
// - Running away: when it gets where it was running to, it wanders in place; another order ends the run.
// The repulsor is AI::findClosestRepulsor within its vision range (bounding circles apart): nearest of those marked
// repulsors (OBJECT_STATUS_REPULSOR) but itself, else of its enemies alive that can attack (a structure only if it
// can), none hidden by stealth.
export namespace generalszh::gameplay
{
struct RepulsionSystem
{
	using Query = ecs::Query<ecs::Write<Repulsable>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::TeamMember>, ecs::Optional<engine::gameplay::AiActivity>>;
	using Lookup = ecs::Lookup<ecs::Read<RepulsorMark>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::Health>,
		ecs::Read<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<RepulsionRules>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<RepulsionEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<RepulsionEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const RepulsionRules &rules = context.Read<RepulsionRules>();
		if (!rules.enabled)
			return;
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const auto lookup = context.Lookup<Lookup>();
		auto &events = context.Write<RepulsionEvents>().Slot(context);
		auto runners = chunk.Get<Repulsable>();
		const auto orders = chunk.Get<gp::MoveOrder>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto activities = chunk.Get<gp::AiActivity>();
		const auto entities = chunk.Entities();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < runners.size(); ++row)
		{
			Repulsable &runner = runners[row];
			const gp::MoveMode mode = orders[row].mode;
			const ecs::Entity self = entities[row];
			if (runner.fleeing != 0)
			{
				if (mode == gp::MoveMode::Point)
					continue;
				runner.fleeing = 0;
				if (mode == gp::MoveMode::Idle)
					events.push_back({self, {}, RepulsionEvent::Kind::WanderInPlace});
				runner.lastMode = static_cast<std::uint8_t>(mode);
				continue;
			}
			if (static_cast<std::uint8_t>(mode) != runner.lastMode)
			{
				runner.lastMode = static_cast<std::uint8_t>(mode);
				runner.timer = 0;
				if (mode == gp::MoveMode::Idle)
				{
					runner.nextIdleLook = now;
					runner.firstIdleLook = 1;
				}
			}
			bool look = false;
			if (gp::Wandering(mode) || mode == gp::MoveMode::WanderInPlace)
			{
				if (--runner.timer < 0)
				{
					runner.timer = runner.waitFrames;
					look = true;
				}
			}
			else if (mode == gp::MoveMode::Idle && (activities.empty() || activities[row].busy == 0) && now >= runner.nextIdleLook)
			{
				std::uint64_t wait = rules.idleTicks;
				if (runner.firstIdleLook != 0)
				{
					auto random = Engine::Math::Stream(seed, {now, self.index, self.generation, 0x4E70u});
					wait += static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(rules.idleTicks)));
					runner.firstIdleLook = 0;
				}
				runner.nextIdleLook = now + wait;
				look = true;
			}
			if (!look)
				continue;
			const std::uint32_t team = members.empty() ? gp::Relationships::NoTeam : members[row].team;
			const ecs::Entity threat = Closest(spatial, relationships, lookup, self, transforms[row].position.XY(), runner.vision, team, owners[row].player, now);
			if (threat != ecs::Entity{})
				events.push_back({self, threat, RepulsionEvent::Kind::Flee});
		}
	}

	template<typename LookupT>
	static ecs::Entity Closest(const engine::gameplay::SpatialIndex &spatial, const engine::gameplay::Relationships &relationships, const LookupT &lookup,
		ecs::Entity self, Engine::Math::FixedVector2 at, Engine::Math::Fixed range, std::uint32_t team, std::uint32_t player, std::uint64_t now)
	{
		namespace gp = engine::gameplay;
		const gp::SpatialEntry *mine = spatial.Find(self);
		const Engine::Math::Fixed radius = mine != nullptr ? mine->radius : Engine::Math::Fixed{};
		ecs::Entity best;
		Engine::Math::Fixed bestGap;
		spatial.ForEachWithin(at, range + radius, [&](const gp::SpatialEntry &entry) {
			if (entry.entity == self || (entry.classes & gp::target_class::Hidden) != 0)
				return;
			const RepulsorMark *mark = lookup.template Get<RepulsorMark>(entry.entity);
			if (mark == nullptr || now >= mark->until)
			{
				const gp::Health *health = lookup.template Get<gp::Health>(entry.entity);
				if (lookup.template Get<gp::Dying>(entry.entity) != nullptr || (health != nullptr && gp::IsDead(*health)))
					return;
				if (!relationships.Enemies(team, player, entry.team, entry.player))
					return;
				const gp::Armament *armament = lookup.template Get<gp::Armament>(entry.entity);
				if (armament == nullptr || armament->weapon == gp::WeaponCatalog::None)
					return;
			}
			const Engine::Math::Fixed gap = Engine::Math::Distance(entry.position.XY(), at) - radius - entry.radius;
			if (best == ecs::Entity{} || gap < bestGap)
			{
				best = entry.entity;
				bestGap = gap;
			}
		});
		return best;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RepulsionSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.repulsion";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
