export module games.generalszh.gameplay.creation.systems.ocl_timer_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.creation.components.ocl_timer;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.teams.resources.team_roster;
import Engine.Core.Math.FixedRandom;

// OCLUpdate::update for everything with a creation timer, chunk-parallel: held while disabled once its timer runs
// (the fork's fix: the retail game also pushed back a timer never started); a faction-triggered one waits for a
// playable owner and restarts with each new one; built and due, it runs its list (OclTimerEvents: ApplyOclTimers makes
// it) and restarts, its first due tick only starting it.
export namespace generalszh::gameplay
{
struct OclTimerEvent
{
	ecs::Entity source;
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
	std::uint32_t definition{0};
	std::uint32_t player{0};
	std::uint32_t team{0xFFFFFFFFu};
};

struct OclTimerEvents : ecs::ChunkOutputs<OclTimerEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::OclTimerEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ocl_timer_events";
};
}

export namespace generalszh::gameplay
{
struct OclTimerSystem
{
	using Query = ecs::Query<ecs::Write<OclTimer>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::TeamMember>, ecs::Optional<engine::gameplay::Disabled>,
		ecs::Optional<engine::gameplay::UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<engine::gameplay::RandomSeed>,
		ecs::Write<OclTimerEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<OclTimerEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const engine::gameplay::TeamRoster &roster = context.Read<engine::gameplay::TeamRoster>();
		auto &out = context.Write<OclTimerEvents>().Slot(context);
		const std::uint64_t seed = context.Read<engine::gameplay::RandomSeed>().value ^ 0x0C1u;
		const std::uint64_t now = context.Tick();
		auto timers = chunk.Get<OclTimer>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto owners = chunk.Get<engine::gameplay::Owner>();
		const auto members = chunk.Get<engine::gameplay::TeamMember>();
		const auto disabled = chunk.Get<engine::gameplay::Disabled>();
		const auto building = chunk.Get<engine::gameplay::UnderConstruction>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < timers.size(); ++row)
		{
			const OclTimerConfig *config = templates.OclTimerOf(refs[row].index);
			if (config == nullptr)
				continue;
			OclTimer &timer = timers[row];
			// Object::isDisabled: held (its timer pushed back a tick).
			if (timer.startedTick > 0 && !disabled.empty() && (disabled[row].mask & engine::gameplay::disabled_type::All) != 0)
			{
				++timer.nextTick;
				++timer.startedTick;
				continue;
			}
			auto random = Engine::Math::Stream(seed, {now, entities[row].index, entities[row].generation, 0x0C1u});
			// setNextCreationFrame.
			const auto restart = [&] {
				timer.startedTick = now;
				timer.nextTick = now + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(config->minDelay),
					static_cast<std::int64_t>(std::max(config->minDelay, config->maxDelay))));
			};
			const std::uint32_t player = owners[row].player;
			if (config->factionTriggered)
			{
				// Player::isPlayableSide; a new owner (a new colour) restarts its timer.
				const bool playable = player < roster.PlayerCount() && roster.PlayerAt(player).playable;
				if (timer.neutral != 0)
				{
					if (playable)
					{
						timer.player = player;
						timer.neutral = 0;
						restart();
					}
				}
				else if (!playable)
					timer.neutral = 1;
				else if (player != timer.player)
				{
					timer.player = player;
					restart();
				}
				if (timer.neutral != 0)
					continue;
			}
			// shouldCreate: due and built.
			if (now < timer.nextTick || !building.empty())
				continue;
			if (timer.nextTick == 0)
			{
				restart();
				continue;
			}
			restart();
			out.push_back({entities[row], transforms[row].position, transforms[row].facing, refs[row].index, player, members.empty() ? 0xFFFFFFFFu : members[row].team});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::OclTimerSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ocl_timer";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
