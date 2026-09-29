export module games.generalszh.presentation.interaction.systems.build_placement_system;
import std;

export import games.generalszh.presentation.interaction.resources.build_placement;
export import games.generalszh.presentation.interaction.systems.interaction_systems;

// Placing a structure, once a frame before the pointer's other uses (InGameUI's MOUSEMODE_BUILD_PLACE): the ghost
// follows the ground under the pointer; the left button anchors it where pressed and, dragged, turns it to face along
// the drag (getPlacementPoints); released, it orders the build there if the place is legal (DozerAIUpdate::construct
// through the command bus) and placing ends; the right button gives up. While placing, the pointer's clicks go to it,
// not to selecting or ordering.
export namespace generalszh::presentation
{
struct BuildPlacementSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Write<PointerInput>, ecs::Read<InteractionView>, ecs::Write<BuildPlacement>, ecs::Write<PlayerOrders>,
		ecs::Read<engine::gameplay::GroundHeight>, ecs::Read<LocalPlayer>>;

	void Execute(ecs::SystemContext &context) const
	{
		BuildPlacement &placement = context.Write<BuildPlacement>();
		if (!placement.active)
			return;
		PointerInput &pointer = context.Write<PointerInput>();
		const InteractionView &view = context.Read<InteractionView>();
		if (!view.valid || !context.Read<LocalPlayer>().valid)
			return;
		const auto &ground = context.Read<engine::gameplay::GroundHeight>();
		const bool leftPressed = (pointer.pressed & 1u) != 0 && !pointer.overInterface;
		const bool leftReleased = (pointer.released & 1u) != 0;
		const bool rightPressed = (pointer.pressed & 2u) != 0 && !pointer.overInterface;
		if (rightPressed)
		{
			placement = BuildPlacement{};
			pointer.pressed = pointer.released = 0;
			return;
		}
		if (leftPressed)
		{
			placement.anchored = true;
			placement.anchorScreen = {pointer.x, pointer.y};
		}
		if (placement.anchored)
		{
			// The ghost stays at the anchor; dragged, it faces from the anchor towards the pointer on the ground.
			const auto start = PointerInteractionSystem::GroundUnder(view, ground, placement.anchorScreen[0], placement.anchorScreen[1]);
			if (start)
			{
				placement.at = *start;
				placement.onGround = true;
			}
			if (pointer.x != placement.anchorScreen[0] || pointer.y != placement.anchorScreen[1])
				if (const auto end = PointerInteractionSystem::GroundUnder(view, ground, pointer.x, pointer.y); start && end)
					placement.facing = std::atan2((*end)[1] - (*start)[1], (*end)[0] - (*start)[0]);
		}
		else if (const auto under = PointerInteractionSystem::GroundUnder(view, ground, pointer.x, pointer.y))
		{
			placement.at = *under;
			placement.onGround = true;
		}
		else
			placement.onGround = false;
		if (leftReleased && placement.anchored)
		{
			if (placement.legal && placement.onGround)
			{
				using interaction_detail::ToFixed;
				const float turns = placement.facing / (2.0f * std::numbers::pi_v<float>);
				const auto facing = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::llround(static_cast<double>(turns) * 4294967296.0)));
				context.Write<PlayerOrders>().pending.push_back(
					commands::BuildStructure{placement.builder, placement.structure, {ToFixed(placement.at[0]), ToFixed(placement.at[1])}, facing});
				placement = BuildPlacement{};
			}
			else
				placement.anchored = false;
		}
		pointer.pressed = pointer.released = 0;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::BuildPlacementSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.build_placement";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<generalszh::presentation::PointerInteractionSystem>;
	using After = SystemTypeList<>;
};
}
