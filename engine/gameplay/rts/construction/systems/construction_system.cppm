export module engine.gameplay.rts.construction.systems.construction_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.construction.components.builder;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.construction_progress;
export import engine.gameplay.rts.construction.resources.sales;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.economy.resources.player_energy;
export import engine.gameplay.common.healing.components.healing;
export import engine.gameplay.common.healing.resources.heal_pulses;

// DozerActionDoActionState for a build task, once a tick in entity order: a
// builder whose structure is gone, finished or no longer its player's gives
// up; one stopped at its dock (within its reach) starts building it (the
// structure no longer awaits construction) and then, each tick, adds 100 /
// framesToBuild % and max health / framesToBuild health to it
// (internalChangeHealth), framesToBuild being its build ticks stretched by
// its player's power shortage (calcTimeToBuild: truncated, as the original's
// Int); at 100% it stands (the game finishes it), its base regeneration no
// longer waits, and the builder that finished it moves off to its end dock
// point. A repairing builder (DOZER_TASK_REPAIR) at its dock heals
// its structure max health * RepairHealthPercentPerSecond / 30 a tick as its
// sole healer for this tick and the next (attemptHealingFromSoleBenefactor);
// if another holds it, or it is whole (or gone, or no longer standing), it
// stops.
export namespace engine::gameplay
{
struct ConstructionSystem
{
	using Query = ecs::Query<ecs::Read<Builder>, ecs::Read<Transform>, ecs::Optional<MoveOrder>, ecs::Optional<Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<UnderConstruction>, ecs::Read<ConstructionProgress>, ecs::Read<Health>, ecs::Read<Owner>, ecs::Read<SelfHealing>,
		ecs::Read<HealLock>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<PlayerEnergy>, ecs::Read<EnergySettings>, ecs::Write<ConstructionsDone>, ecs::Write<HealPulses>>;

	// calcTimeToBuild's power part: the build ticks over the production speed, truncated (at least 1).
	static std::uint64_t FramesToBuild(std::uint64_t buildTicks, Engine::Math::Fixed speed) noexcept
	{
		const std::int64_t frames = (Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(buildTicks)) / speed).Floor();
		return static_cast<std::uint64_t>(std::max<std::int64_t>(frames, 1));
	}

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const PlayerEnergy &energy = context.Read<PlayerEnergy>();
		const EnergySettings &settings = context.Read<EnergySettings>();
		auto &done = context.Write<ConstructionsDone>().list;
		done.clear();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		// What this tick's builders did to each structure (several may share one).
		struct Work
		{
			ecs::Entity structure;
			ConstructionProgress progress;
			Health health;
			UnderConstruction state;
			ecs::Entity builder;
			std::int64_t frames{1};
			Engine::Math::FixedVector2 leave;
			bool canMove{false};
		};
		std::vector<Work> work;
		query.ForEachChunk([&](auto chunk) {
			const auto builders = chunk.template Get<Builder>();
			const auto transforms = chunk.template Get<Transform>();
			const auto orders = chunk.template Get<MoveOrder>();
			const auto owners = chunk.template Get<Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < builders.size(); ++row)
			{
				const Builder &builder = builders[row];
				const ecs::Entity target = builder.target;
				if (builder.repair != 0)
				{
					const Health *body = lookup.IsAlive(target) ? lookup.Get<Health>(target) : nullptr;
					if (body == nullptr || IsDead(*body) || body->current >= body->maximum || lookup.Get<UnderConstruction>(target) != nullptr)
					{
						commands.Remove<Builder>(entities[row]);
						continue;
					}
					if (builder.atWork == 0)
					{
						const bool stopped = orders.empty() || orders[row].mode == MoveMode::Idle;
						if (!stopped || Engine::Math::DistanceSquared(transforms[row].position.XY(), builder.dock) > builder.reach * builder.reach)
							continue;
						Builder working = builder;
						working.atWork = 1;
						commands.Set<Builder>(entities[row], working);
					}
					// Another healing it holds it: this one gives up.
					if (const HealLock *lock = lookup.Get<HealLock>(target); lock != nullptr && tick <= lock->until && lock->healer != entities[row])
					{
						commands.Remove<Builder>(entities[row]);
						continue;
					}
					const Transform *where = lookup.Get<Transform>(target);
					context.Write<HealPulses>().Add({target, entities[row], body->maximum * builder.repairShare, 2, where != nullptr ? where->position : Engine::Math::FixedVector3{}});
					continue;
				}
				const UnderConstruction *site = lookup.IsAlive(target) ? lookup.Get<UnderConstruction>(target) : nullptr;
				const Owner *theirs = site != nullptr ? lookup.Get<Owner>(target) : nullptr;
				const bool mine = theirs != nullptr && !owners.empty() && owners[row].player == theirs->player;
				if (site == nullptr || !mine || lookup.Get<ConstructionProgress>(target) == nullptr || lookup.Get<Health>(target) == nullptr)
				{
					commands.Remove<Builder>(entities[row]);
					continue;
				}
				if (builder.atWork == 0)
				{
					const bool stopped = orders.empty() || orders[row].mode == MoveMode::Idle;
					if (!stopped || Engine::Math::DistanceSquared(transforms[row].position.XY(), builder.dock) > builder.reach * builder.reach)
						continue;
					Builder working = builder;
					working.atWork = 1;
					commands.Set<Builder>(entities[row], working);
				}
				auto found = std::find_if(work.begin(), work.end(), [&](const Work &item) { return item.structure == target; });
				if (found == work.end())
				{
					work.push_back({target, *lookup.Get<ConstructionProgress>(target), *lookup.Get<Health>(target), *site, entities[row], 1, {}, false});
					found = work.end() - 1;
				}
				const Engine::Math::Fixed speed = settings.ProductionSpeed(energy.SupplyRatio(theirs->player));
				const auto frames = static_cast<std::int64_t>(FramesToBuild(found->state.buildTicks, speed));
				found->state.started = 1;
				found->state.workedTick = tick;
				found->progress.percent += Engine::Math::Fixed::FromInt(100) / Engine::Math::Fixed::FromInt(frames);
				found->frames = frames;
				found->health.current = std::min(found->health.maximum, found->health.current + found->health.maximum / Engine::Math::Fixed::FromInt(frames));
				found->builder = entities[row];
				found->leave = builder.leave;
				found->canMove = !orders.empty();
			}
		});
		for (Work &item : work)
		{
			commands.Set<Health>(item.structure, item.health);
			// Done at 100%. Fixed point truncates each step of 100 / frames where the original's float step rounds (up, for
			// 100 / 90), so frames steps may fall short by up to a raw unit each: that much is forgiven.
			if (item.progress.percent + Engine::Math::Fixed::FromRaw(item.frames) >= Engine::Math::Fixed::FromInt(100))
			{
				commands.Set<ConstructionProgress>(item.structure, ConstructionProgress{});
				commands.Remove<UnderConstruction>(item.structure);
				if (const SelfHealing *regen = lookup.Get<SelfHealing>(item.structure); regen != nullptr && regen->waiting != 0)
				{
					SelfHealing standing = *regen;
					standing.waiting = 0;
					commands.Set<SelfHealing>(item.structure, standing);
				}
				done.push_back({item.structure, item.builder, item.progress.rebuild != 0});
				// Off to its end dock point (aiMoveToPosition).
				if (item.canMove)
					commands.Set<MoveOrder>(item.builder, MoveToPoint(item.leave));
				continue;
			}
			commands.Set<ConstructionProgress>(item.structure, item.progress);
			commands.Set<UnderConstruction>(item.structure, item.state);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ConstructionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.construction";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
