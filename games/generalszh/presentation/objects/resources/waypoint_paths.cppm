export module games.generalszh.presentation.objects.resources.waypoint_paths;
import std;

export import games.generalszh.presentation.objects.resources.object_instance;
import engine.ecs.system.system;

// W3DWaypointBuffer::drawWaypoints, this frame: in waypoint mode each selected thing's path still to go, else the selected
// factories' rally lines (or a listening outpost's view of an enemy's way), as a line (its
// segments: EXLaser.tga tiled along it, 1.5 wide, (0.25, 0.5, 1) added over what is drawn, never hidden by it) and the
// SCMNode model at each of its points. Presentation state, refilled every frame.
export namespace generalszh::presentation
{
struct WaypointSegment
{
	std::array<float, 3> start{};
	std::array<float, 3> end{};
	float width{1.5f};
	std::array<float, 4> color{0.25f, 0.5f, 1.0f, 1.0f};
	std::string_view texture;
	float uvScale{1.0f};
	float uvOffset{0.0f};
};

struct WaypointPaths
{
	static constexpr std::size_t MaxDisplayNodes = 512; // MAX_DISPLAY_NODES
	static constexpr std::string_view Texture = "EXLaser.tga";
	static constexpr float Width = 1.5f;
	static constexpr std::array<float, 4> Color{0.25f, 0.5f, 1.0f, 1.0f}; // setDefaultLineStyle
	std::vector<WaypointSegment> segments;
	std::vector<ObjectInstance> nodes;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::WaypointPaths>
{
	static constexpr std::string_view StableName = "generalszh.presentation.waypoint_paths";
};
}
