export module games.generalszh.gameplay.combat.systems.battle_bus_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.gameplay.combat.components.battle_bus;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.containment.components.transport;

// BattleBusSlowDeathBehavior::update for every battle bus in its first death, chunk-parallel (one really dying is its
// slow death's: onDie ends the first death):
// - thrown: once past its ground check tick and not above the ground, it lands (BattleBusEvents: FXHitGround,
//   OCLHitGround, SECOND_LIFE, idle, still, HELD);
// - a hulk: the update after landing and every 15 ticks after, empty it starts counting EmptyHulkDestructionDelay (anyone
//   in stops the count), and past it, still empty, it is killed with PENALTY damage, EXTRA_4 (a kill request: it dies with
//   this tick's deaths). With no delay it never checks.
export namespace generalszh::gameplay
{
struct BattleBusEvent
{
	enum class Kind : std::uint8_t
	{
		Landed,
	};
	ecs::Entity bus;
	Kind kind{Kind::Landed};
	std::uint8_t reserved[7]{};
};

// The tick's landings (a batch system's output, spent by ApplyBattleBusEvents).
struct BattleBusEvents
{
	std::vector<BattleBusEvent> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BattleBusEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_bus_events";
};
}

export namespace generalszh::gameplay
{
struct BattleBusSystem
{
	using Query = ecs::Query<ecs::Write<BattleBus>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::Dying>, ecs::Optional<engine::gameplay::Transport>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::GroundHeight>, ecs::Write<BattleBusEvents>,
		ecs::Write<engine::gameplay::KillRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto &ground = context.Read<gp::GroundHeight>();
		auto &events = context.Write<BattleBusEvents>().list;
		auto &kills = context.Write<gp::KillRequests>();
		static const std::uint32_t penalty = content::DamageTypeIndex("PENALTY").value_or(0);
		static const std::uint32_t extra4 = content::DeathTypeIndex("EXTRA_4").value_or(0);
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto buses = chunk.template Get<BattleBus>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto refs = chunk.template Get<gp::DefinitionRef>();
			const bool dying = !chunk.template Get<gp::Dying>().empty();
			const auto transports = chunk.template Get<gp::Transport>();
			const auto entities = chunk.Entities();
			if (dying)
				return;
			for (std::size_t row = 0; row < buses.size(); ++row)
			{
				BattleBus &bus = buses[row];
				const BattleBusConfig *config = templates.BattleBusOf(refs[row].index);
				if (config == nullptr)
					continue;
				if (bus.phase == BusPhase::Thrown)
				{
					const auto &at = transforms[row].position;
					if (bus.groundCheckTick < now && at.z - ground.At(at.XY()) <= Engine::Math::Fixed{})
					{
						events.push_back({entities[row], BattleBusEvent::Kind::Landed});
						bus.phase = BusPhase::Hulk;
						bus.nextCheck = config->hulkDelayTicks == 0 ? 0 : now + 1;
					}
					continue;
				}
				if (bus.phase != BusPhase::Hulk || bus.nextCheck == 0 || now < bus.nextCheck)
					continue;
				if (transports.empty())
				{
					bus.nextCheck = 0;
					continue;
				}
				const std::uint32_t inside = transports[row].occupied;
				bus.nextCheck = now + BusHulkCheckDelay;
				if (bus.penaltyTick != 0)
				{
					if (inside > 0)
						bus.penaltyTick = 0;
					else if (now > bus.penaltyTick)
					{
						kills.typed.push_back({entities[row], penalty, extra4});
						bus.nextCheck = 0;
					}
				}
				else if (inside == 0)
					bus.penaltyTick = now + config->hulkDelayTicks;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::BattleBusSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_bus";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
