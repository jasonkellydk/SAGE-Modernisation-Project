export module engine.gameplay.rts.movement.systems.wander_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.wanderer;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.health.components.health;
import Engine.Core.Math.FixedRandom;

// WanderAIUpdate::update, each tick before movement, chunk-parallel: a wanderer standing still (isIdle) sets off for
// where it is plus 5 to 50 along each axis (GameLogicRandomValue, whole units; aiMoveToPosition).
export namespace engine::gameplay
{
struct WanderSystem
{
	using Query = ecs::Query<ecs::Read<Wanderer>, ecs::Read<Transform>, ecs::Write<MoveOrder>, ecs::Optional<Health>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x3A4Du;
		const std::uint64_t tick = context.Tick();
		const auto transforms = chunk.Get<Transform>();
		auto orders = chunk.Get<MoveOrder>();
		const auto healths = chunk.Get<Health>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < orders.size(); ++row)
		{
			// The dead do not run about (their AI stops).
			if (orders[row].mode != MoveMode::Idle || (!healths.empty() && IsDead(healths[row])))
				continue;
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			const auto dx = Engine::Math::UniformInt(random, 5, 50);
			const auto dy = Engine::Math::UniformInt(random, 5, 50);
			const auto &at = transforms[row].position;
			orders[row] = MoveToPoint({at.x + Engine::Math::Fixed::FromInt(dx), at.y + Engine::Math::Fixed::FromInt(dy)});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::WanderSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.wander";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before movement.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
