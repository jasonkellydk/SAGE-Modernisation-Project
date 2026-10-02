export module games.generalszh.presentation.objects.systems.tree_breeze_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.algorithms.tree_breeze_sway;

// TreeBreezeSystem, each frame (W3DTreeBuffer::prepareFrame): at a new
// breeze the tree sway is rolled again (updateSway, even for trees out of
// sight), then every sway type steps by this frame's logic frames.
export namespace generalszh::presentation
{
struct TreeBreezeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<Breeze>, ecs::Read<PresentationFrame>, ecs::Write<TreeBreeze>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const Breeze &breeze = context.Read<Breeze>();
		TreeBreeze &trees = context.Write<TreeBreeze>();
		if (trees.version != breeze.version)
			RollTreeBreeze(trees, breeze, [&](std::size_t type, std::uint32_t which) {
				return tree_breeze_sway_detail::Unit((static_cast<std::uint64_t>(static_cast<std::uint32_t>(breeze.version)) << 32) ^ (type << 8) ^ which);
			});
		// getActualLogicTimeScaleOverFpsRatio: the logic frames (1/30 s of game time) this frame covers.
		StepTreeBreeze(trees, static_cast<float>(context.Read<PresentationFrame>().seconds) * 30.0f);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::TreeBreezeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_breeze";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
