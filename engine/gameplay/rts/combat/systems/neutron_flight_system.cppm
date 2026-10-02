export module engine.gameplay.rts.combat.systems.neutron_flight_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.neutron_flight;
export import engine.gameplay.rts.combat.algorithms.thrust;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.attitude;

// NeutronMissileUpdate::update, each tick (a batch: few in the air; their kills and effects go in one list):
//   reaching the point above its target (within its bounding sphere, 3D) it is put there and falls straight down at
//   half its speed;
//   launching (doLaunch): its launch effect, its height and tick noted, then armed and attacking (its ignition effect);
//   attacking (doAttack): straight on until it has flown DistanceToTravelBeforeTurning, then turning for the point
//   above its target (reached: the target) by at most MaxTurnRate; pushed along its heading by RelativeSpeed (half,
//   coming down) less ForwardDamping of its velocity; for SpecialSpeedTime after launch it only climbs,
//   SpecialSpeedHeight * (factor * t)^2 / factor over its launch height;
//   the distance flown counts down its straight run. Its update's ground check calls onCollide, and the missile has
//   no collide modules: what ends it is its HeightDieUpdate (below TargetHeight, coming down).
//   Its SpecialJitterDistance shake moves only its drawing (presentation).
export namespace engine::gameplay
{
struct NeutronFlightSystem
{
	using Query = ecs::Query<ecs::Write<NeutronFlight>, ecs::Write<Transform>, ecs::OptionalWrite<Attitude>>;
	using Resources = ecs::Resources<ecs::Read<WeaponCatalog>, ecs::Write<NeutronEffects>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		auto &effects = context.Write<NeutronEffects>().played;
		effects.clear();
		const std::uint64_t now = context.Tick();
		const Fixed half = Fixed::FromRatio(1, 2);
		query.ForEachChunk([&](auto chunk) {
			auto flights = chunk.template Get<NeutronFlight>();
			auto transforms = chunk.template Get<Transform>();
			auto attitudes = chunk.template Get<Attitude>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < flights.size(); ++row)
			{
				NeutronFlight &flight = flights[row];
				if (flight.state == NeutronState::Dead)
					continue;
				const NeutronMissileDefinition &d = weapons.At(flight.weapon).neutron;
				Transform &transform = transforms[row];
				FixedVector3 &pos = transform.position;
				if (flight.reached == 0)
				{
					const FixedVector3 apart = pos - flight.intermediate;
					if (Engine::Math::LengthSquared(apart) <= d.boundingRadius * d.boundingRadius)
					{
						flight.reached = 1;
						pos = flight.intermediate;
						flight.velocity = {Fixed{}, Fixed{}, -Engine::Math::Length(flight.velocity) * half};
					}
				}
				const FixedVector3 before = pos;
				const bool attacking = flight.state == NeutronState::Attack;
				if (flight.state == NeutronState::Launch)
				{
					if (d.targetFromAbove != Fixed{})
						flight.reached = 0;
					if (d.launchEffect != 0xFFFFFFFFu)
						effects.push_back({d.launchEffect, entities[row], pos});
					flight.heightAtLaunch = pos.z;
					flight.launchTick = now;
					pos = pos + flight.velocity;
					if (d.ignitionEffect != 0xFFFFFFFFu)
						effects.push_back({d.ignitionEffect, entities[row], pos});
					flight.state = NeutronState::Attack;
				}
				else
				{
					Fixed speed = d.relativeSpeed;
					if (d.targetFromAbove != Fixed{} && flight.reached != 0)
						speed = speed * half;
					if (flight.noTurnLeft <= Fixed{})
					{
						// calcTransform: toward its goal, by at most MaxTurnRate.
						const FixedVector3 goal = flight.reached != 0 ? flight.target : flight.intermediate;
						const FixedVector3 toward = Normalized(goal - pos);
						if (Engine::Math::LengthSquared(toward) > Fixed{})
						{
							const std::int64_t angle = AngleBetween(flight.forward, toward);
							if (angle < static_cast<std::int64_t>(d.maxTurnRate.units))
								flight.forward = toward;
							else
							{
								const FixedVector3 axis = Normalized(Engine::Math::Cross(flight.forward, toward));
								if (Engine::Math::LengthSquared(axis) > Fixed{})
									flight.forward = Normalized(RotateAbout(flight.forward, axis, d.maxTurnRate));
							}
						}
					}
					const FixedVector3 accel = flight.forward * speed - flight.velocity * d.forwardDamping;
					flight.velocity = flight.velocity + accel;
					if (d.specialSpeedTicks > 0 && now <= flight.launchTick + d.specialSpeedTicks)
					{
						const std::uint64_t elapsed = now - flight.launchTick;
						if (elapsed < d.specialSpeedTicks)
						{
							const Fixed frac = Fixed::FromInt(static_cast<std::int64_t>(elapsed)) / Fixed::FromInt(static_cast<std::int64_t>(d.specialSpeedTicks));
							const Fixed factor = std::max(d.specialAccelFactor, Fixed::FromRatio(1, 100));
							const Fixed climb = factor * frac;
							const Fixed height = flight.heightAtLaunch + climb * climb / factor * d.specialSpeedHeight;
							flight.velocity = {Fixed{}, Fixed{}, height - pos.z};
						}
					}
					pos = pos + flight.velocity;
				}
				if (attacking && flight.noTurnLeft > Fixed{})
					flight.noTurnLeft = flight.noTurnLeft - Engine::Math::Length(pos - before);
				transform.facing = Engine::Math::Heading(flight.forward.XY());
				if (!attitudes.empty())
					attitudes[row].pitch = -Engine::Math::Heading({Engine::Math::Length(flight.forward.XY()), flight.forward.z});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::NeutronFlightSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.neutron_flight";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
