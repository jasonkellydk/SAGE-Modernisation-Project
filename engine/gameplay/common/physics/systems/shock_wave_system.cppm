export module engine.gameplay.common.physics.systems.shock_wave_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.physics.algorithms.forces;
export import engine.gameplay.common.physics.resources.shock_waves;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// Object::attemptDamage's shock wave, for each of the tick's shocks on a thing with physics that is neither airborne
// nor a projectile, in the order dealt, chunk-parallel:
//   the push: along the shock's vector (its source to the thing), its amount times 1 less the taper (the distance over
//   the shock's radius, at most 1, times 1 less its TaperOff), and as much again upward (the push's own length); less
//   its ShockResistance's share (applyShock);
//   applyRandomRotation, unless it sticks to the ground: it may bounce, and each turn rate gains up to its ShockMax
//   (a random share from -1 to 1, the logic's random stream);
//   setStunned: its locomotor lets go (Pushed) until it lands again (physics carries its hop). (The original's STUNNED_FLAILING
//   and STUNNED model conditions are shown by no shipped art; the stun itself ends in the update after the shock,
//   before any bounce could kill it: testStunnedUnitForDestruction never finds it stunned.)
export namespace engine::gameplay
{
struct ShockWaveSystem
{
	using Query = ecs::Query<ecs::Write<PhysicsBody>, ecs::Optional<Targetable>>;
	using Resources = ecs::Resources<ecs::Write<ShockWaves>, ecs::Read<RandomSeed>>;

	void BeforeChunks(Query &, ecs::SystemContext &context) const
	{
		auto &list = context.Write<ShockWaves>().list;
		std::ranges::stable_sort(list, [](const ShockWave &a, const ShockWave &b) {
			return a.victim.index < b.victim.index || (a.victim.index == b.victim.index && a.victim.generation < b.victim.generation);
		});
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector3;
		const auto &list = context.Write<ShockWaves>().list;
		if (list.empty())
			return;
		const std::uint64_t seed = context.Read<RandomSeed>().value;
		auto bodies = chunk.Get<PhysicsBody>();
		const auto targets = chunk.Get<Targetable>();
		const auto entities = chunk.Entities();
		const auto before = [](const ShockWave &shock, ecs::Entity entity) {
			return shock.victim.index < entity.index || (shock.victim.index == entity.index && shock.victim.generation < entity.generation);
		};
		for (std::size_t row = 0; row < bodies.size(); ++row)
		{
			const ecs::Entity self = entities[row];
			auto first = std::lower_bound(list.begin(), list.end(), self, before);
			if (first == list.end() || first->victim != self)
				continue;
			if (!targets.empty() && (targets[row].classes & (target_class::AirborneVehicle | target_class::AirborneInfantry | target_class::Projectile)) != 0)
				continue;
			PhysicsBody &body = bodies[row];
			std::uint32_t count = 0;
			for (auto shock = first; shock != list.end() && shock->victim == self; ++shock, ++count)
			{
				if (shock->amount <= Fixed{} || shock->radius <= Fixed{})
					continue;
				const Fixed length = Engine::Math::Length(shock->vector);
				const Fixed fromCentre = std::min(Fixed::One(), length / shock->radius);
				const Fixed mult = Fixed::One() - fromCentre * (Fixed::One() - shock->taperOff);
				FixedVector3 force = length > Fixed{} ? shock->vector / length : FixedVector3{Fixed{}, Fixed{}, Fixed::One()};
				force = force * (shock->amount * mult);
				force.z = Engine::Math::Length(force);
				const Fixed resisted = Fixed::One() - std::clamp(body.shockResistance, Fixed{}, Fixed::One());
				ApplyForce(body, force * resisted);
				if (!body.Has(physics_flag::StickToGround))
				{
					body.Set(physics_flag::AllowBouncing, true);
					auto random = Engine::Math::Stream(seed, {context.Tick(), self.index, self.generation, 0x5C0Cu + count});
					const auto spin = [&](std::int32_t most) {
						const Fixed share = Engine::Math::UniformFixed(random, Fixed{} - Fixed::One(), Fixed::One());
						return static_cast<std::int32_t>((static_cast<std::int64_t>(most) * share.Raw()) >> Fixed::FractionBits);
					};
					body.yawRate += spin(body.shockMaxYaw);
					body.pitchRate += spin(body.shockMaxPitch);
					body.rollRate += spin(body.shockMaxRoll);
				}
				if (body.Has(physics_flag::Locomotive))
					body.Set(physics_flag::Pushed, true);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ShockWaveSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.shock_waves";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's impacts.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
