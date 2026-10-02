export module games.generalszh.presentation.objects.resources.move_hints;
import std;

export import games.generalszh.presentation.objects.resources.object_instance;
import engine.ecs.system.system;

// Where moves were ordered (InGameUI's m_moveHint ring: createMoveHint on MSG_DO_MOVETO, MSG_DO_ATTACKMOVETO,
// MSG_DO_FORCEMOVETO, MSG_ADD_WAYPOINT), each shown for 40 client frames from when it was ordered (W3DInGameUI::
// drawMoveHints), and the frame's hint models.
export namespace generalszh::presentation
{
struct MoveHint
{
	std::array<float, 3> at{};
	double since{0.0}; // presentation clock seconds
	bool live{false};
};

struct MoveHints
{
	static constexpr std::size_t Capacity = 256; // MAX_MOVE_HINTS
	static constexpr double ShownSeconds = 40.0 / 30.0; // 40 client frames
	std::array<MoveHint, Capacity> hints{};
	std::size_t next{0};
	std::vector<ObjectInstance> instances;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::MoveHints>
{
	static constexpr std::string_view StableName = "generalszh.presentation.move_hints";
};
}
