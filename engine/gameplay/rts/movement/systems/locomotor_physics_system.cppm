export module engine.gameplay.rts.movement.systems.locomotor_physics_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.rts.movement.components.locomotion;

// Locomotor::setPhysicsOptions (AIUpdateInterface::doLocomotor, every frame, whatever the unit is doing): its current
// locomotor sets its body's extra friction (Extra2DFriction, half a unit more while ultra-accurate), whether ground
// friction still applies in the air (Apply2DFrictionWhenAirborne) and whether it is kept on the ground and never
// tumbled by a shock (StickToGround). Physics reads them whenever it moves the body itself (pushed, thrown, disabled).
// A body its locomotor no longer drives (Locomotive cleared: flung by its death) keeps its own. Before physics, each
// tick, chunk-parallel.
export namespace engine::gameplay
{
inline void SetLocomotorPhysics(PhysicsBody &body, const Locomotion &motion) noexcept
{
	const LocomotorDefinition &locomotor = motion.locomotor;
	body.extraFriction = locomotor.extraFriction + (motion.ultraAccurate != 0 ? Engine::Math::Fixed::FromRatio(1, 2) : Engine::Math::Fixed{});
	body.Set(physics_flag::AirborneFriction, locomotor.airborneFriction != 0);
	body.Set(physics_flag::StickToGround, locomotor.stickToGround != 0);
}

struct LocomotorPhysicsSystem
{
	using Query = ecs::Query<ecs::Read<Locomotion>, ecs::Write<PhysicsBody>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		const auto motions = chunk.Get<Locomotion>();
		auto bodies = chunk.Get<PhysicsBody>();
		for (std::size_t row = 0; row < bodies.size(); ++row)
			if (bodies[row].Has(physics_flag::Locomotive))
				SetLocomotorPhysics(bodies[row], motions[row]);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::LocomotorPhysicsSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.locomotor_physics";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it after what drives bodies before physics, and before physics.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
