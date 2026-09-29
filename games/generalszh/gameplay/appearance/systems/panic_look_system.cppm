export module games.generalszh.gameplay.appearance.systems.panic_look_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.rts.movement.components.move_order;
export import games.generalszh.gameplay.ai.components.repulsion;
import games.generalszh.content.objects.model_conditions;

// AIPanicState's and AIMoveAwayFromRepulsorsState's look, each tick in parallel: PANICKING while it panics or runs from
// a repulsor (set onEnter, cleared onExit: whatever order comes next, or the end of its path or run).
export namespace generalszh::gameplay
{
struct PanicLookSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::Appearance>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Optional<Repulsable>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		constexpr std::uint32_t panicking = content::ModelConditionBit("PANICKING");
		auto appearances = chunk.Get<engine::gameplay::Appearance>();
		const auto orders = chunk.Get<engine::gameplay::MoveOrder>();
		const auto runners = chunk.Get<Repulsable>();
		for (std::size_t row = 0; row < appearances.size(); ++row)
			appearances[row].Set(panicking, orders[row].mode == engine::gameplay::MoveMode::Panic ||
				(!runners.empty() && runners[row].fleeing != 0 && orders[row].mode == engine::gameplay::MoveMode::Point));
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::PanicLookSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.panic_look";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
