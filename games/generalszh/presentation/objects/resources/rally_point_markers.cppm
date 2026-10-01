export module games.generalszh.presentation.objects.resources.rally_point_markers;
import std;

export import games.generalszh.presentation.objects.resources.object_instance;
import engine.ecs.system.system;

// The rally point flag (ControlBar::showRallyPoint's RallyPointMarker drawable): drawn with the objects while the one
// thing selected has a rally point; which definition it is (RallyPointMarker), which way it faces (GameData
// DownwindAngle), since when it has shown (its looping flag animation runs on from there while it stays up) and the
// frame's instance (none: hidden).
export namespace generalszh::presentation
{
struct RallyPointMarkers
{
	static constexpr std::uint32_t NoDefinition = 0xFFFFFFFFu;
	std::uint32_t definition{NoDefinition};
	float downwindAngle{-0.785f};
	bool shown{false};
	double since{0.0};
	std::vector<ObjectInstance> instances;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RallyPointMarkers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rally_point_markers";
};
}
