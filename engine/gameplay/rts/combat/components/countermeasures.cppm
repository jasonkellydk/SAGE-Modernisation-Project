export module engine.gameplay.rts.combat.components.countermeasures;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;

// Decoy flares against missiles (the original's CountermeasuresBehavior, an upgrade mux), as data: the flares it has
// launched still flying (m_counterMeasures, oldest first), the ticks of its reaction volley (m_reactionFrame), next volley
// (m_nextVolleyFrame) and reload (m_reloadFrame), its counts (m_availableCountermeasures, m_activeCountermeasures,
// m_divertedMissiles, m_incomingMissiles), whether its upgrade has come, and its module's data (VolleySize,
// NumberOfVolleys, DelayBetweenVolleys, ReloadTime, MissileDecoyDelay, ReactionLaunchLatency, EvasionRate,
// VolleyArcAngle, VolleyVelocityFactor, MustReloadAtAirfield; ticks, the game's flare definition).
// MissileReports: the tick's missiles launched at it (reportMissileForCountermeasures), `diverted` when one will go for a
// flare. FlareLaunches: the tick's flares to put out (the game makes them). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct Countermeasures
{
	static constexpr std::size_t MaxFlares = 16;
	std::array<ecs::Entity, MaxFlares> flares{};
	std::uint64_t reactionTick{0};
	std::uint64_t nextVolleyTick{0};
	std::uint64_t reloadTick{0};
	std::uint64_t volleyTicks{0};
	std::uint64_t reloadTicks{0};
	std::uint64_t decoyTicks{0};
	std::uint64_t reactionTicks{0};
	Engine::Math::Fixed evasionRate;
	Engine::Math::Fixed velocityFactor;
	Engine::Math::TurnAngle arc;
	std::uint32_t flareDefinition{0xFFFFFFFFu};
	std::uint32_t available{0};
	std::uint32_t active{0};
	std::uint32_t diverted{0};
	std::uint32_t incoming{0};
	std::uint32_t volleySize{0};
	std::uint32_t volleys{0};
	std::uint8_t flareCount{0};
	std::uint8_t upgraded{0};
	std::uint8_t mustReloadAtAirfield{0};
	std::uint8_t reserved{0};
	std::uint32_t reserved2{0};

	// reloadCountermeasures.
	void Reload() noexcept
	{
		available = volleySize * volleys;
		reloadTick = 0;
	}
};

struct MissileReport
{
	ecs::Entity victim;
	std::uint8_t diverted{0};
	std::uint8_t reserved[7]{};
};

struct MissileReports : ecs::ChunkOutputs<MissileReport>
{
};

// A flare to put out: from `owner`, `index` of its volley, going along `velocity` (on top of the owner's own).
struct FlareLaunch
{
	ecs::Entity owner;
	Engine::Math::FixedVector3 velocity;
	std::uint32_t definition{0};
	std::uint32_t reserved{0};
};

struct FlareLaunches : ecs::ChunkOutputs<FlareLaunch>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Countermeasures>
{
	static constexpr std::string_view StableName = "engine.gameplay.countermeasures";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Countermeasures &value, StateHasher &hasher) noexcept
	{
		for (std::size_t index = 0; index < value.flareCount; ++index)
			hasher.AppendU64((std::uint64_t{value.flares[index].index} << 32) | value.flares[index].generation);
		hasher.AppendU64(value.reactionTick);
		hasher.AppendU64(value.nextVolleyTick);
		hasher.AppendU64(value.reloadTick);
		hasher.AppendU64((std::uint64_t{value.available} << 32) | value.active);
		hasher.AppendU64((std::uint64_t{value.diverted} << 32) | value.incoming);
		hasher.AppendU64(std::uint64_t{value.flareCount} | (std::uint64_t{value.upgraded} << 8));
	}
};

template<>
struct ResourceTraits<engine::gameplay::MissileReports>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_reports";
};

template<>
struct ResourceTraits<engine::gameplay::FlareLaunches>
{
	static constexpr std::string_view StableName = "engine.gameplay.flare_launches";
};
}
