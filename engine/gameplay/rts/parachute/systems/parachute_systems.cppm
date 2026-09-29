export module engine.gameplay.rts.parachute.systems.parachute_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.parachute.algorithms.parachute_rigging;
export import engine.gameplay.rts.parachute.algorithms.parachute_release;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.lifecycle.resources.casualties;
export import engine.gameplay.rts.parachute.resources.parachute_openings;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.definitions.pathfind_cell;

// Parachutes each tick, in the original's order within a frame (its AI's locomotor, its physics with onCollide, then
// ParachuteContain::update):
//   ParachuteSystem, before physics: it drives its body with its locomotor (closed: falling; open: gliding,
//     ultra-accurately for a set landing spot), gliding for its landing spot until close enough, then holding there.
//   ParachuteLandingSystem, after physics: its rider hangs from it; once the chute is no longer above the ground its
//     rider lets go (no longer held, allowed to fall; killed landing in water, off the map, or on a cliff, water or
//     impassable cell) and the chute is killed (its slow death drops it); otherwise it learns where it started (at
//     least twice its opening distance up), opens once it has dropped its opening distance, sways, and is killed
//     without a rider or down in water.
//   ParachuteLossSystem, after the tick's damage: a chute killed lets its rider go where it hangs, in free fall and hurt
//     when that is well above the ground (onDie).
//   FreeFallSystem, after physics: a body in free fall is DISABLED_FREEFALL until it lands.
// A held chute (still in its carrier) and a dying one are otherwise left alone.
export namespace engine::gameplay
{
namespace parachute_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;

// A spring and damper on a turn (ParachuteContain: rate += -k angle - d rate; angle += rate).
inline void Spring(std::int32_t &angle, std::int32_t &rate, Fixed stiffness, Fixed damping) noexcept
{
	const auto scale = [](std::int32_t value, Fixed factor) {
		return static_cast<std::int64_t>((static_cast<std::int64_t>(value) * factor.Raw()) >> Fixed::FractionBits);
	};
	rate = static_cast<std::int32_t>(rate - scale(angle, stiffness) - scale(rate, damping));
	angle = static_cast<std::int32_t>(static_cast<std::int64_t>(angle) + rate);
}

template<typename Rows>
inline bool Held(const Rows &rows, std::size_t row) noexcept
{
	return !rows.empty() && (rows[row].mask & disabled_type::Held) != 0;
}
}

struct ParachuteSystem
{
	using Query = ecs::Query<ecs::Write<Parachute>, ecs::Write<Transform>, ecs::Write<PhysicsBody>, ecs::Optional<Disabled>, ecs::Exclude<OffMap>,
		ecs::Exclude<Dying>>;
	using Resources = ecs::Resources<ecs::Read<ParachuteCatalog>, ecs::Read<GroundHeight>, ecs::Read<PhysicsSettings>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace parachute_detail;
		const ParachuteCatalog &catalog = context.Read<ParachuteCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const Fixed gravity = context.Read<PhysicsSettings>().gravity;
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto chutes = chunk.template Get<Parachute>();
			auto transforms = chunk.template Get<Transform>();
			auto bodies = chunk.template Get<PhysicsBody>();
			const auto disabledRows = chunk.template Get<Disabled>();
			for (std::size_t row = 0; row < chutes.size(); ++row)
			{
				if (Held(disabledRows, row))
					continue;
				Parachute &chute = chutes[row];
				Transform &transform = transforms[row];
				PhysicsBody &body = bodies[row];
				const ParachuteDefinition &definition = catalog.At(chute.definition);
				const bool opened = chute.Has(parachute_flag::Opened);
				const HoverLocomotor &locomotor = opened ? definition.open : definition.freeFall;
				const bool ultra = opened && chute.Has(parachute_flag::Override);
				SetHoverPhysics(body, locomotor, ultra);
				const Fixed surface = ground.Surface(transform.position.XY());
				if (chute.Has(parachute_flag::Moving))
				{
					// Its move ends once close enough (a set spot: within 10 across; else its locomotor's, in 3D if it says).
					const bool threeD = locomotor.closeEnough3D && !ultra;
					const Fixed closeEnough = ultra ? Fixed::FromInt(10) : locomotor.closeEnough;
					const FixedVector3 to = chute.landing - transform.position;
					const Fixed distance = Engine::Math::Sqrt(to.x * to.x + to.y * to.y + (threeD ? to.z * to.z : Fixed{}));
					if (distance <= closeEnough)
						chute.Set(parachute_flag::Moving, false);
					else
					{
						chute.Set(parachute_flag::MaintainValid, false);
						HoverMoveTowards(body, transform, chute.landing, distance, locomotor.maxSpeed, locomotor, ultra, surface, gravity, tick);
					}
				}
				if (!chute.Has(parachute_flag::Moving))
				{
					if (!chute.Has(parachute_flag::MaintainValid))
					{
						chute.maintain = transform.position;
						chute.Set(parachute_flag::MaintainValid, true);
					}
					HoverMaintain(body, transform, locomotor, ultra, surface, gravity, tick);
				}
			}
		});
	}
};

struct ParachuteLandingSystem
{
	using Query = ecs::Query<ecs::Write<Parachute>, ecs::Read<Transform>, ecs::Read<PhysicsBody>, ecs::Optional<Disabled>, ecs::Exclude<OffMap>,
		ecs::Exclude<Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Disabled>, ecs::Read<PhysicsBody>, ecs::Read<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<ParachuteCatalog>, ecs::Read<GroundHeight>, ecs::Read<NavigationGrid>, ecs::Write<KillRequests>,
		ecs::Write<ParachuteOpenings>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace parachute_detail;
		const ParachuteCatalog &catalog = context.Read<ParachuteCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		KillRequests &kills = context.Write<KillRequests>();
		ParachuteOpenings &openings = context.Write<ParachuteOpenings>();
		openings.opened.clear();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto chutes = chunk.template Get<Parachute>();
			const auto transforms = chunk.template Get<Transform>();
			const auto bodies = chunk.template Get<PhysicsBody>();
			const auto disabledRows = chunk.template Get<Disabled>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < chutes.size(); ++row)
			{
				if (Held(disabledRows, row))
					continue;
				Parachute &chute = chutes[row];
				const Transform &transform = transforms[row];
				const ParachuteDefinition &definition = catalog.At(chute.definition);
				const Transform *riderAt = lookup.IsAlive(chute.rider) ? lookup.template Get<Transform>(chute.rider) : nullptr;
				// Out of its carrier: its rider with it.
				if (riderAt != nullptr && lookup.template Get<OffMap>(chute.rider) != nullptr)
					commands.Remove<OffMap>(chute.rider);
				const bool landed = !bodies[row].Has(physics_flag::WasAirborne);
				if (riderAt != nullptr)
				{
					// ParachuteContain::positionRider: at its harness, never below the ground, facing as the chute.
					const ParachuteOffsets offsets = RigParachute(definition, chute, transform.facing);
					Transform rider{transform.position + offsets.riderAttach, transform.facing};
					const Fixed floor = ground.At(rider.position.XY());
					if (rider.position.z < floor)
						rider.position.z = floor;
					commands.Set<Transform>(chute.rider, rider);
					// onCollide with the ground (removeAllContained, onRemoving): no longer held, allowed to fall; dead in
					// water (within its slop), off the map, or on a cliff, water or impassable cell.
					if (landed)
					{
						ReleaseRider(chute.rider, rider.position, definition, lookup, commands, ground, grid, kills, false);
						chute.rider = {};
					}
				}
				// Down: killed, so its slow death drops it.
				if (landed)
				{
					kills.entities.push_back(entities[row]);
					continue;
				}
				// ParachuteContain::update: where it started, at least twice its opening distance up ...
				if (!chute.Has(parachute_flag::Started))
				{
					chute.Set(parachute_flag::Started, true);
					chute.startZ = transform.position.z;
					const Fixed floor = ground.At(transform.position.XY());
					if (chute.startZ - floor < definition.openDistance * Fixed::FromInt(2))
						chute.startZ = floor + definition.openDistance * Fixed::FromInt(2);
				}
				// ... opening once it has dropped its opening distance: its rider hangs open (and hears it), and it glides for
				// its landing spot (the set one, else where it opened).
				if (!chute.Has(parachute_flag::Opened))
				{
					const Fixed dropped = chute.startZ - transform.position.z;
					if ((dropped < Fixed{} ? Fixed{} - dropped : dropped) >= definition.openDistance)
					{
						chute.Set(parachute_flag::Opened, true);
						chute.openedTick = tick;
						if (riderAt != nullptr)
							openings.opened.push_back({entities[row], chute.rider, riderAt->position});
						if (!chute.Has(parachute_flag::Override))
							chute.landing = {transform.position.x, transform.position.y, ground.At(transform.position.XY())};
						chute.Set(parachute_flag::Moving, true);
						chute.Set(parachute_flag::MaintainValid, false);
					}
				}
				// Open, it sways on its spring (its open locomotor's), damped more with its rider within 20 of the ground.
				if (chute.Has(parachute_flag::Opened))
				{
					Fixed extra{};
					if (riderAt != nullptr && riderAt->position.z - ground.At(riderAt->position.XY()) <= Fixed::FromInt(20))
						extra = definition.lowAltitudeDamping;
					Spring(chute.pitch, chute.pitchRate, definition.open.pitchStiffness, definition.open.pitchDamping + extra);
					Spring(chute.roll, chute.rollRate, definition.open.rollStiffness, definition.open.rollDamping + extra);
				}
				// Lost its rider: it goes early. Down in water: so does it.
				Fixed water;
				if (riderAt == nullptr || (ground.Water(transform.position.XY(), water) && transform.position.z - water < definition.waterSlop))
					kills.entities.push_back(entities[row]);
			}
		});
	}
};
// A parachute killed (ParachuteContain::onDie, then OpenContain::onDie's removeAllContained), among the tick's deaths:
// its rider is let go where it hangs (ReleaseRider); lost significantly above the ground (isSignificantlyAboveTerrain),
// the rider falls helplessly and takes the chute's FreeFallDamagePercent of its maximum from the chute's killer.
struct ParachuteLossSystem
{
	using Query = ecs::Query<ecs::Read<Parachute>>;
	using Lookup = ecs::Lookup<ecs::Read<Parachute>, ecs::Read<Transform>, ecs::Read<Health>, ecs::Read<Disabled>, ecs::Read<PhysicsBody>>;
	using Resources = ecs::Resources<ecs::Read<Casualties>, ecs::Read<ParachuteCatalog>, ecs::Read<GroundHeight>, ecs::Read<NavigationGrid>,
		ecs::Read<PhysicsSettings>, ecs::Write<KillRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const ParachuteCatalog &catalog = context.Read<ParachuteCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		const Engine::Math::Fixed significant = context.Read<PhysicsSettings>().SignificantHeight();
		KillRequests &kills = context.Write<KillRequests>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		for (const Casualty &death : context.Read<Casualties>().list)
		{
			const Parachute *chute = lookup.template Get<Parachute>(death.entity);
			if (death.departure != Departure::Killed || chute == nullptr || !lookup.IsAlive(chute->rider))
				continue;
			const Transform *chuteAt = lookup.template Get<Transform>(death.entity);
			const Transform *riderAt = lookup.template Get<Transform>(chute->rider);
			if (chuteAt == nullptr || riderAt == nullptr)
				continue;
			const ParachuteDefinition &definition = catalog.At(chute->definition);
			const bool aloft = chuteAt->position.z - ground.At(chuteAt->position.XY()) > significant;
			std::optional<RiderLoss> loss;
			if (const Health *health = lookup.template Get<Health>(chute->rider); aloft && health != nullptr && definition.freeFallDamage > Engine::Math::Fixed{})
				loss = RiderLoss{death.killer, health->maximum * definition.freeFallDamage, definition.fallDamageType, definition.fallDeathType};
			ReleaseRider(chute->rider, riderAt->position, definition, lookup, commands, ground, grid, kills, aloft, loss);
			// removeAllContained: the chute holds no one now.
			Parachute emptied = *chute;
			emptied.rider = {};
			commands.Set<Parachute>(death.entity, emptied);
		}
	}
};

// Free fall (PhysicsBehavior::update): while above the ground a body in free fall is DISABLED_FREEFALL; once down it no
// longer is, nor in free fall.
struct FreeFallSystem
{
	using Query = ecs::Query<ecs::Write<PhysicsBody>, ecs::OptionalWrite<Disabled>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		auto bodies = chunk.Get<PhysicsBody>();
		auto disabled = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < bodies.size(); ++row)
		{
			PhysicsBody &body = bodies[row];
			const bool falling = body.Has(physics_flag::InFreeFall);
			if (!falling && (disabled.empty() || (disabled[row].mask & disabled_type::Freefall) == 0))
				continue;
			const bool aloft = falling && body.Has(physics_flag::WasAirborne);
			if (!aloft)
				body.Set(physics_flag::InFreeFall, false);
			if (!disabled.empty())
				disabled[row].mask = aloft ? (disabled[row].mask | disabled_type::Freefall) : (disabled[row].mask & ~disabled_type::Freefall);
		}
	}
};
}


export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ParachuteLossSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_loss";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The composition orders it after the tick's deaths.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::FreeFallSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.free_fall";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it after physics.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::ParachuteSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it before physics.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::ParachuteLandingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_landing";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it after physics, before the spatial index.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
