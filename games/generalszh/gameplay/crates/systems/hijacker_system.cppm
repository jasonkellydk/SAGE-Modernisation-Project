export module games.generalszh.gameplay.crates.systems.hijacker_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.crates.components.hijacker;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.rts.veterancy.components.experience;

// HijackerUpdate::update for every hijacker, chunk-parallel. Driving a vehicle that is still there: it goes where the
// vehicle is (setPosition), notes whether the vehicle is significantly above the ground and where it is, and both take
// the higher of their veterancy levels. The vehicle gone (destroyed: it "has safely ejected us"): it is let out where
// it stands, by parachute when the vehicle was last aloft, and stops updating. Each hijacker writes only itself; the
// rest goes out as its chunk's HijackerEvents (ApplyHijackerEvents).
export namespace generalszh::gameplay
{
struct HijackerEvent
{
	enum class Kind : std::uint8_t
	{
		Follow,    // `at`: the vehicle's position; `level`: the higher veterancy level for both
		Release,   // out of the vehicle; `at`: where it was last; `airborne`: by parachute
	};
	ecs::Entity hijacker;
	ecs::Entity vehicle;
	Engine::Math::FixedVector3 at;
	Kind kind{Kind::Follow};
	std::uint8_t level{0};
	std::uint8_t airborne{0};
	std::uint8_t reserved[5]{};
};

struct HijackerEvents : ecs::ChunkOutputs<HijackerEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::HijackerEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hijacker_events";
};
}

export namespace generalszh::gameplay
{
struct HijackerSystem
{
	using Query = ecs::Query<ecs::Write<Hijacker>, ecs::Optional<engine::gameplay::Experience>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Experience>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::GroundHeight>, ecs::Read<engine::gameplay::PhysicsSettings>, ecs::Write<HijackerEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<HijackerEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto &ground = context.Read<gp::GroundHeight>();
		const auto &physics = context.Read<gp::PhysicsSettings>();
		auto &events = context.Write<HijackerEvents>().Slot(context);
		const auto lookup = context.Lookup<Lookup>();
		auto hijackers = chunk.Get<Hijacker>();
		const auto experience = chunk.Get<gp::Experience>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < hijackers.size(); ++row)
		{
			Hijacker &hijacker = hijackers[row];
			if (hijacker.update == 0)
				continue;
			if (hijacker.inVehicle == 0)
			{
				hijacker.wasTargetAirborne = 0;
				continue;
			}
			const gp::Transform *vehicle = lookup.IsAlive(hijacker.target) ? lookup.Get<gp::Transform>(hijacker.target) : nullptr;
			if (vehicle != nullptr)
			{
				hijacker.wasTargetAirborne = vehicle->position.z - ground.At(vehicle->position.XY()) > physics.SignificantHeight() ? 1 : 0;
				hijacker.ejectPosition = vehicle->position;
				std::uint8_t level = experience.empty() ? 0 : experience[row].level;
				if (const gp::Experience *theirs = lookup.Get<gp::Experience>(hijacker.target))
					level = std::max(level, theirs->level);
				events.push_back({entities[row], hijacker.target, vehicle->position, HijackerEvent::Kind::Follow, level});
				continue;
			}
			events.push_back({entities[row], {}, hijacker.ejectPosition, HijackerEvent::Kind::Release, 0, hijacker.wasTargetAirborne});
			hijacker.target = {};
			hijacker.inVehicle = 0;
			hijacker.update = 0;
			hijacker.wasTargetAirborne = 0;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::HijackerSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hijacker";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
