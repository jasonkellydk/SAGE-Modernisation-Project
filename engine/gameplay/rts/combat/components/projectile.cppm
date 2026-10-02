export module engine.gameplay.rts.combat.components.projectile;
import std;

export import engine.gameplay.rts.combat.algorithms.projectile_arc;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.ecs.core.component_registry;

// A projectile in flight along its arc (DumbProjectileBehavior's flight
// path): its control points, how many points it walks and the next one,
// the shot it carries (it lands where the projectile detonates, as it runs
// out of points) and how far a tick the arc's end may follow its victim
// (FlightPathAdjustDistPerSecond; zero: never); a tumbling one's spin per
// tick (yaw, pitch, roll: TumbleRandomly's physics rates).
export namespace engine::gameplay
{
struct ProjectileFlight
{
	ArcPoints points{};
	std::uint32_t segments{0};
	std::uint32_t step{0};
	ProjectileArc arc{};
	Shot shot{};
	std::array<Engine::Math::TurnAngle, 3> tumble{};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ProjectileFlight>
{
	static constexpr std::string_view StableName = "engine.gameplay.projectile_flight";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ProjectileFlight &value, StateHasher &hasher) noexcept
	{
		for (const auto &point : value.points)
		{
			hasher.AppendU64(static_cast<std::uint64_t>(point.x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(point.y.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(point.z.Raw()));
		}
		hasher.AppendU64((static_cast<std::uint64_t>(value.segments) << 32) | value.step);
		hasher.AppendU64(value.arc.orientToPath ? 1u : 0u);
		hasher.AppendU64(static_cast<std::uint64_t>(value.arc.firstHeight.Raw()) ^ (static_cast<std::uint64_t>(value.arc.secondHeight.Raw()) << 1));
		hasher.AppendU64(static_cast<std::uint64_t>(value.arc.firstIndent.Raw()) ^ (static_cast<std::uint64_t>(value.arc.secondIndent.Raw()) << 1));
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.source.index) << 32) | value.shot.source.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.target.index) << 32) | value.shot.target.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.weapon) << 32) | value.shot.sourcePlayer);
		hasher.AppendU64(value.shot.fireTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.arc.followPerTick.Raw()));
		hasher.AppendU64(value.arc.maxLifespan | (value.arc.tumble ? 1ull << 63 : 0ull));
		for (const auto &rate : value.tumble)
			hasher.AppendU64(rate.units);
	}
};
}
