export module games.generalszh.presentation.objects.resources.placement_ghosts;
import std;

export import games.generalszh.presentation.objects.resources.object_instance;
import engine.ecs.system.system;

// The frame's placement ghosts (a structure being placed: InGameUI's m_placeIcon), drawn with the objects, and how see
// through they are (GameData ObjectPlacementOpacity).
export namespace generalszh::presentation
{
struct PlacementGhosts
{
	std::vector<ObjectInstance> instances;
	float opacity{0.45f};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::PlacementGhosts>
{
	static constexpr std::string_view StableName = "generalszh.presentation.placement_ghosts";
};
}
