export module engine.gameplay.rts.movement.systems.locomotor_damage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.resources.movement_penalty;
export import engine.gameplay.common.health.components.health;

// Hurt enough, a body moves on its locomotor's damaged rates (Locomotor::getMaxSpeedForCondition, getMaxTurnRate,
// getMaxAcceleration: SpeedDamaged, TurnRateDamaged, AccelerationDamaged once its damage state is no better than
// GameData's MovementPenaltyDamageState), and on its full rates again once repaired or healed. Before movement, each
// tick, chunk-parallel.
export namespace engine::gameplay
{
struct LocomotorDamageSystem
{
	using Query = ecs::Query<ecs::Write<Locomotion>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<MovementPenalty>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const MovementPenalty &penalty = context.Read<MovementPenalty>();
		auto motions = chunk.Get<Locomotion>();
		const auto healths = chunk.Get<Health>();
		for (std::size_t row = 0; row < motions.size(); ++row)
		{
			LocomotorDefinition &locomotor = motions[row].locomotor;
			const bool damaged = penalty.Applies(healths[row]);
			locomotor.maxSpeed = damaged ? locomotor.speedDamaged : locomotor.speedFull;
			locomotor.acceleration = damaged ? locomotor.accelerationDamaged : locomotor.accelerationFull;
			locomotor.turnRate = damaged ? locomotor.turnRateDamaged : locomotor.turnRateFull;
			locomotor.lift = damaged ? locomotor.liftDamaged : locomotor.liftFull; // getMaxLift
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::LocomotorDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.locomotor_damage";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before movement.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
