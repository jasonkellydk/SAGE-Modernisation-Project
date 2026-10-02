export module games.generalszh.gameplay.bridges.systems.bridge_damage_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.bridges.components.bridge;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.death.components.dying;

// BridgeBehavior for every bridge, chunk-parallel: its body's damage state (ActiveBody::calcDamageState: above
// UnitDamagedThreshold of its maximum pristine, above UnitReallyDamagedThreshold damaged, above none really damaged,
// else rubble) against the one it last heard; a change is its onBodyDamageStateChange (anything but rubble clears its
// death); and while it is dead, its BridgeDieFX / BridgeDieOCL whose delay has come (update: deathTime == delay; those
// of no delay went off as it died). Each bridge writes only itself; what reaches beyond it goes out as its chunk's
// BridgeEvents, for the session to carry out once the systems have run (ApplyBridgeEvents).
export namespace generalszh::gameplay
{
struct BridgeEvent
{
	enum class Kind : std::uint8_t
	{
		StateChange, // onBodyDamageStateChange(from, to)
		DieEffect,   // its `effect`-th BridgeDieFX / BridgeDieOCL goes off
	};
	ecs::Entity bridge;
	std::uint32_t effect{0};
	Kind kind{Kind::StateChange};
	std::uint8_t from{0};
	std::uint8_t to{0};
	std::uint8_t reserved{0};
};

struct BridgeEvents : ecs::ChunkOutputs<BridgeEvent>
{
};

// ActiveBody::calcDamageState.
inline std::uint8_t BodyStateOf(Engine::Math::Fixed health, Engine::Math::Fixed maximum, Engine::Math::Fixed damaged, Engine::Math::Fixed reallyDamaged)
{
	if (maximum <= Engine::Math::Fixed{})
		return health > Engine::Math::Fixed{} ? body_state::Pristine : body_state::Rubble;
	const Engine::Math::Fixed ratio = health / maximum;
	if (ratio > damaged)
		return body_state::Pristine;
	if (ratio > reallyDamaged)
		return body_state::Damaged;
	if (ratio > Engine::Math::Fixed{})
		return body_state::ReallyDamaged;
	return body_state::Rubble;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BridgeEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bridge_events";
};
}

export namespace generalszh::gameplay
{
struct BridgeDamageSystem
{
	using Query = ecs::Query<ecs::Write<Bridge>, ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<BridgeEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<BridgeEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto &gameData = templates.Content().gameData;
		auto &events = context.Write<BridgeEvents>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto bridges = chunk.Get<Bridge>();
		const auto healths = chunk.Get<engine::gameplay::Health>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		// Dead (killed outright too: Object::kill is damage past its health) is rubble.
		const bool dying = !chunk.Get<engine::gameplay::Dying>().empty();
		for (std::size_t row = 0; row < bridges.size(); ++row)
		{
			Bridge &bridge = bridges[row];
			const std::uint8_t state =
				dying ? body_state::Rubble : BodyStateOf(healths[row].current, healths[row].maximum, gameData.unitDamaged, gameData.unitReallyDamaged);
			if (state != bridge.bodyState)
			{
				events.push_back({entities[row], 0, BridgeEvent::Kind::StateChange, bridge.bodyState, state});
				bridge.bodyState = state;
				if (state != body_state::Rubble)
					bridge.deathTick = 0;
			}
			if (bridge.deathTick == 0 || tick <= bridge.deathTick)
				continue;
			const auto found = templates.Content().bridgeDieEffects.find(templates.DefinitionAt(refs[row].index).name);
			if (found == templates.Content().bridgeDieEffects.end())
				continue;
			const std::uint64_t since = tick - bridge.deathTick;
			for (std::uint32_t index = 0; index < found->second.size(); ++index)
				if (found->second[index].delayTicks == since)
					events.push_back({entities[row], index, BridgeEvent::Kind::DieEffect});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::BridgeDamageSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bridge_damage";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
