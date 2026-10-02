export module engine.gameplay.rts.combat.systems.countermeasures_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.rts.combat.systems.projectile_launch_system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.movement.components.locomotion;

// CountermeasuresBehavior::update for everything with countermeasures, each tick after the tick's launches:
// - the missiles reported at it this tick (reportMissileForCountermeasures): counted; one to be diverted, with no flares
//   out and no reaction due, sets its reaction volley ReactionLaunchLatency from now;
// - dead, or its upgrade not come: nothing more;
// - its flares gone are forgotten (one fewer out each);
// - airborne with flares left: on its reaction tick a volley, the next DelayBetweenVolleys later; on that tick another,
//   and so on while flares last;
// - none left and a ReloadTime: reloaded that long after it ran out (at an airfield instead: MustReloadAtAirfield).
// launchVolley: VolleySize flares spread evenly across twice VolleyArcAngle about its heading, going at its speed times
// VolleyVelocityFactor (slower than 1 a tick: -10, backwards), on top of its own motion; the game puts them out after
// the step (FlareLaunches) and they join its list.
export namespace engine::gameplay
{
struct CountermeasuresSystem
{
	using Query = ecs::Query<ecs::Write<Countermeasures>, ecs::Read<Transform>, ecs::Optional<Targetable>, ecs::Optional<Health>,
		ecs::Optional<Locomotion>>;
	using Resources = ecs::Resources<ecs::Read<MissileReports>, ecs::Write<FlareLaunches>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<FlareLaunches>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector3;
		const MissileReports &reports = context.Read<MissileReports>();
		auto &out = context.Write<FlareLaunches>().Slot(context);
		auto decoys = chunk.Get<Countermeasures>();
		const auto transforms = chunk.Get<Transform>();
		const auto targetables = chunk.Get<Targetable>();
		const auto healths = chunk.Get<Health>();
		const auto motions = chunk.Get<Locomotion>();
		const auto entities = chunk.Entities();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < decoys.size(); ++row)
		{
			Countermeasures &c = decoys[row];
			reports.ForEach([&](const MissileReport &report) {
				if (report.victim != entities[row])
					return;
				++c.incoming;
				if (report.diverted != 0)
				{
					++c.diverted;
					if (c.active == 0 && c.reactionTick == 0)
						c.reactionTick = now + c.reactionTicks;
				}
			});
			if ((!healths.empty() && IsDead(healths[row])) || c.upgraded == 0)
				continue;
			// Its flares gone: forgotten, one fewer out each.
			std::uint8_t kept = 0;
			for (std::uint8_t index = 0; index < c.flareCount; ++index)
				if (lookup.IsAlive(c.flares[index]))
					c.flares[kept++] = c.flares[index];
				else if (c.active > 0)
					--c.active;
			for (std::uint8_t index = kept; index < c.flareCount; ++index)
				c.flares[index] = {};
			c.flareCount = kept;
			const bool airborne = !targetables.empty() && (targetables[row].classes & target_class::AirborneVehicle) != 0;
			const auto volley = [&] {
				const Transform &at = transforms[row];
				Fixed speed = motions.empty() ? Fixed{} : motions[row].speed;
				if (speed < Fixed::One())
					speed = Fixed::FromInt(-10);
				const Fixed size = Fixed::FromInt(static_cast<std::int64_t>(c.volleySize));
				for (std::uint32_t index = 0; index < c.volleySize; ++index)
				{
					Fixed ratio;
					if (c.volleySize != 1)
						ratio = Fixed::FromInt(index) / (size - Fixed::One()) * 2 - Fixed::One();
					const Engine::Math::TurnAngle angle{static_cast<std::uint32_t>(static_cast<std::int64_t>(
						(Fixed::FromInt(static_cast<std::int32_t>(c.arc.units)) * ratio).Floor()))};
					const auto direction = Engine::Math::Direction(at.facing + angle);
					out.push_back({entities[row], FixedVector3{direction.x, direction.y, Fixed{}} * (speed * c.velocityFactor), c.flareDefinition});
					++c.active;
					--c.available;
				}
			};
			if (airborne && c.available != 0)
			{
				if (c.reactionTick != 0 && c.reactionTick == now)
				{
					volley();
					c.nextVolleyTick = now + c.volleyTicks;
					c.reactionTick = 0;
				}
				if (c.nextVolleyTick == now && c.available != 0)
				{
					volley();
					c.nextVolleyTick = now + c.volleyTicks;
				}
			}
			if (c.available == 0 && c.reloadTicks != 0)
			{
				if (c.reloadTick != 0)
				{
					if (c.reloadTick <= now)
						c.Reload();
				}
				else
					c.reloadTick = now + c.reloadTicks;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::CountermeasuresSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.countermeasures";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::ProjectileLaunchSystem>;
};
}
