export module games.generalszh.gameplay.combat.systems.firestorm_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.combat.components.firestorm;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.dynamic_geometry;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.systems.dynamic_geometry_system;

// FirestormDynamicGeometryInfoUpdate::update (after its geometry's step, DynamicGeometrySystem), chunk-parallel: nothing
// while its geometry has not started; then once its effects (FXList on it: its particle systems are the presentation's,
// sized to its radius each frame); once it has turned to shrink, its scorch (SCORCH_1 of ScorchSize); and every
// DelayBetweenDamageFrames (from frame 0: at once) a damage scan within its bounding circle (FirestormEvents: the game
// deals it after the step).
export namespace generalszh::gameplay
{
struct FirestormEvent
{
	enum class Kind : std::uint8_t
	{
		Effects,
		Scorch,
		Damage,
	};
	ecs::Entity firestorm;
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed radius; // Scorch: its size; Damage: its bounding circle
	std::uint32_t definition{0};
	Kind kind{Kind::Effects};
	std::uint8_t reserved[3]{};
};

struct FirestormEvents : ecs::ChunkOutputs<FirestormEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::FirestormEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.firestorm_events";
};
}

export namespace generalszh::gameplay
{
struct FirestormSystem
{
	using Query = ecs::Query<ecs::Write<Firestorm>, ecs::Read<engine::gameplay::DynamicGeometry>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<FirestormEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<FirestormEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Kind = FirestormEvent::Kind;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto &out = context.Write<FirestormEvents>().Slot(context);
		auto storms = chunk.Get<Firestorm>();
		const auto geometries = chunk.Get<engine::gameplay::DynamicGeometry>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < storms.size(); ++row)
		{
			const FirestormConfig *config = templates.FirestormOf(refs[row].index);
			if (config == nullptr || geometries[row].started == 0)
				continue;
			Firestorm &storm = storms[row];
			const auto at = transforms[row].position;
			if (storm.effectsFired == 0)
			{
				out.push_back({entities[row], at, {}, refs[row].index, Kind::Effects});
				storm.effectsFired = 1;
			}
			if (geometries[row].switched != 0 && storm.scorchPlaced == 0)
			{
				out.push_back({entities[row], at, config->scorchSize, refs[row].index, Kind::Scorch});
				storm.scorchPlaced = 1;
			}
			// getFrame() - m_lastDamageFrame >= m_delayBetweenDamageFrames (a real number of frames).
			if (Fixed::FromInt(static_cast<std::int64_t>(now - storm.lastDamageTick)) >= config->damageDelay)
			{
				out.push_back({entities[row], at, geometries[row].CircleRadius(), refs[row].index, Kind::Damage});
				storm.lastDamageTick = now;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::FirestormSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.firestorm";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	// Its geometry's update first (the base class's, in its own update).
	using After = SystemTypeList<engine::gameplay::DynamicGeometrySystem>;
};
}
