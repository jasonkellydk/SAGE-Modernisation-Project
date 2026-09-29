export module games.generalszh.presentation.objects.systems.placement_ghost_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.placement_ghosts;
export import games.generalszh.presentation.interaction.resources.build_placement;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import engine.gameplay.common.identity.components.owner;

// The structure being placed, drawn where it would go (InGameUI::placeBuildAvailable / handleBuildPlacements): its
// look in its plain state, in the local player's colour, turned to the placement's facing, seen through at
// ObjectPlacementOpacity (0.45) and tinted red where it may not be built (IllegalBuildColor); nothing while the
// pointer is off the ground.
export namespace generalszh::presentation
{

struct PlacementGhostSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Read<BuildPlacement>, ecs::Read<LookCatalog>, ecs::Read<LocalPlayer>, ecs::Write<PlacementGhosts>>;

	void Execute(ecs::SystemContext &context) const
	{
		const BuildPlacement &placement = context.Read<BuildPlacement>();
		PlacementGhosts &ghosts = context.Write<PlacementGhosts>();
		ghosts.instances.clear();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		if (!placement.active || !placement.onGround || placement.definition >= catalog.byDefinition.size())
			return;
		const DefinitionLooks &looks = catalog.byDefinition[placement.definition];
		if (looks.stateLooks.empty())
			return;
		const std::size_t state = looks.states.Empty() ? 0 : content::SelectModelState(looks.states, {});
		const float c = std::cos(placement.facing) * looks.scale, s = std::sin(placement.facing) * looks.scale;
		ObjectInstance ghost;
		ghost.look = looks.stateLooks[std::min(state, looks.stateLooks.size() - 1)];
		ghost.world = {c, -s, 0, placement.at[0], s, c, 0, placement.at[1], 0, 0, looks.scale, placement.at[2], 0, 0, 0, 1};
		const LocalPlayer &local = context.Read<LocalPlayer>();
		ghost.teamColor = local.valid ? catalog.ColorOf(local.player) : std::array<float, 4>{1, 1, 1, 0};
		ghost.opacity = ghosts.opacity;
		ghost.tint = placement.legal ? std::array<float, 3>{0, 0, 0} : std::array<float, 3>{1, 0, 0};
		ghosts.instances.push_back(ghost);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::PlacementGhostSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.placement_ghost";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
