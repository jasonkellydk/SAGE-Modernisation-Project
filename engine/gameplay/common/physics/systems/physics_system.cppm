export module engine.gameplay.common.physics.systems.physics_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.physics.algorithms.forces;
export import engine.gameplay.common.physics.resources.fall_damage;
export import engine.gameplay.common.physics.resources.landings;
export import engine.gameplay.common.physics.components.bounce_sound;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.status.components.disabled;

// Steps every physics body one tick, in parallel per chunk (bodies do not
// touch each other yet: collisions come with the collision domain). A body
// that is killed when it comes to rest gets a life that ends now; one that
// lands hard from a steep fall is hurt (falling damage, killing as the
// game's fall death), which joins the tick's incoming damage in the falling
// damage pass. A body on a deck (SurfaceLayer) comes to rest on the deck where it is over it (PhysicsBehavior's
// getLayerHeight for its layer).
export namespace engine::gameplay
{
struct PhysicsSystem
{
	using Query = ecs::Query<ecs::Write<Transform>, ecs::Write<PhysicsBody>, ecs::OptionalWrite<Attitude>, ecs::Optional<Health>, ecs::Exclude<OffMap>,
		ecs::Optional<Disabled>, ecs::Optional<BounceSound>, ecs::Optional<SurfaceLayer>>;
	using Resources = ecs::Resources<ecs::Read<PhysicsSettings>, ecs::Read<GroundHeight>, ecs::Read<DeckSurfaces>, ecs::Write<FallDamage>, ecs::Write<Landings>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<FallDamage>().Reset(query.PreparedChunkCount());
		context.Write<Landings>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const PhysicsSettings &settings = context.Read<PhysicsSettings>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const DeckSurfaces &decks = context.Read<DeckSurfaces>();
		const auto layers = chunk.Get<SurfaceLayer>();
		auto &falls = context.Write<FallDamage>().Slot(context);
		auto &landings = context.Write<Landings>().Slot(context);
		const auto sounds = chunk.Get<BounceSound>();
		auto transforms = chunk.Get<Transform>();
		auto bodies = chunk.Get<PhysicsBody>();
		auto attitudes = chunk.Get<Attitude>();
		const auto healths = chunk.Get<Health>();
		const auto disabled = chunk.Get<Disabled>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < bodies.size(); ++row)
		{
			PhysicsBody &body = bodies[row];
			const Disabled *off = disabled.empty() ? nullptr : &disabled[row];
			// Its locomotor holds it while its AI runs.
			// (Pushed by a shock wave, physics has it until it rests.)
			if (body.Has(physics_flag::Locomotive) && !body.Has(physics_flag::Pushed) && !body.Has(physics_flag::PhysicsDriven) &&
				(off == nullptr || RunsWhileDisabled(*off, disabled_type::Held)))
			{
				// Held (a rider hanging from its chute), its forces still clear each frame (PhysicsBehavior::update:
				// m_accel.zero() whether or not it is HELD): a push given it as it was put in is spent, not kept.
				if (off != nullptr && (off->mask & disabled_type::Held) != 0)
					body.acceleration = {};
				continue;
			}
			const StepResult step = StepBody(body, transforms[row], attitudes.empty() ? nullptr : &attitudes[row], settings,
				[&](Engine::Math::FixedVector2 at) { return layers.empty() ? ground.At(at) : LayerHeight(decks, ground, at, layers[row].layer); },
				off != nullptr && (off->mask & disabled_type::Held) != 0,
				context.Tick() < body.motiveUntil);
			// Set down again (the original's stun is relieved at once; its locomotor drives on under the landing body).
			if ((step.landed || step.resting) && body.Has(physics_flag::Pushed))
			{
				body.Set(physics_flag::Pushed, false);
				body.velocity = {};
			}
			// Down (on the ground): its locomotor has it again.
			if ((step.landed || step.resting) && body.Has(physics_flag::PhysicsDriven))
			{
				body.Set(physics_flag::PhysicsDriven, false);
				body.velocity = {};
			}
			// PhysicsBehavior::doBounceSound: each landing, its bounce sound where it landed.
			if (step.landed && !sounds.empty())
				landings.push_back({entities[row], sounds[row].sound, transforms[row].position});
			if (step.fall > Engine::Math::Fixed{} && !healths.empty() && !IsDead(healths[row]))
				falls.push_back({entities[row], entities[row], step.fall * body.mass * body.fallDamageFactor, settings.fallDamageType, settings.fallDeathType});
			// A drone at rest splats only dead or unmanned (the original's special case for crashing drones).
			const bool dead = !healths.empty() && IsDead(healths[row]);
			const bool unmanned = off != nullptr && (off->mask & disabled_type::Unmanned) != 0;
			if (step.resting && body.Has(physics_flag::KillWhenResting) && (!body.Has(physics_flag::Drone) || dead || unmanned))
			{
				body.Set(physics_flag::KillWhenResting, false);
				context.Commands().Set<Lifetime>(entities[row], Lifetime{context.Tick()});
			}
		}
	}
};

// Hard landings join the tick's incoming damage (after the tick's impacts
// fill it, before it is applied).
struct FallingDamageSystem
{
	using Query = ecs::Query<ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<FallDamage>, ecs::Read<LocomotorFalls>, ecs::Write<IncomingDamage>>;

	void Execute(ecs::SystemContext &context) const
	{
		const FallDamage &falls = context.Read<FallDamage>();
		// And the falls of bodies their locomotor stepped (flyers), after the physics step's.
		const LocomotorFalls *flown = context.Find<LocomotorFalls>();
		if (falls.Size() == 0 && (flown == nullptr || flown->Size() == 0))
			return;
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		falls.ForEach([&](const DamageRecord &record) { incoming.Add(record); });
		if (flown != nullptr)
			flown->ForEach([&](const DamageRecord &record) { incoming.Add(record); });
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PhysicsSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.physics";
	// Before the tick's queries; the composition orders it before its spatial index.
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::FallingDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.falling_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's impacts.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<>;
};
}
