export module engine.gameplay.common.appearance.systems.special_model_state_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.appearance.components.special_model_state;

// ObjectSMCHelper::update: on the tick a special model condition state runs out (m_smcUntil), its bit is taken off
// (clearSpecialModelConditionStates) and it is held no more. Before the tick's other work, as the helper sleeps until then.
export namespace engine::gameplay
{
struct SpecialModelStateSystem
{
	using Query = ecs::Query<ecs::Read<SpecialModelState>, ecs::Write<Appearance>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		auto &commands = context.Commands();
		query.ForEachChunk([&](auto chunk) {
			const auto states = chunk.template Get<SpecialModelState>();
			auto looks = chunk.template Get<Appearance>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < states.size(); ++row)
			{
				if (tick < states[row].until)
					continue;
				looks[row].Set(states[row].bit, false);
				commands.Remove<SpecialModelState>(entities[row]);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SpecialModelStateSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.special_model_states";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
