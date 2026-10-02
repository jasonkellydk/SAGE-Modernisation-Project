export module games.generalszh.presentation.objects.components.track_marks;
import std;

export import engine.ecs.core.component_registry;

// The tracks a vehicle leaves on the terrain (the original's
// TerrainTracksRenderObjClass): a ring of edges as columns (structure of
// arrays), each edge's two end points across the track, its texture row, the
// presentation time it was laid and how opaque it still is; the anchor the next edge must be a
// track length from; whether it has an anchor, is capped (stopped: resumes
// with a fresh anchor), and was just airborne (the next edge starts
// transparent); how wide its tracks are (computeTrackSpacing) and which
// texture they use (an index of the look catalog's track textures).
export namespace generalszh::presentation
{
inline constexpr std::size_t MaxTrackEdges = 100; // GameLOD's largest MaxTankTrackEdges

struct TrackMarks
{
	std::array<std::array<float, 3>, MaxTrackEdges> left{};
	std::array<std::array<float, 3>, MaxTrackEdges> right{};
	std::array<float, MaxTrackEdges> v{};
	std::array<double, MaxTrackEdges> laid{};
	std::array<float, MaxTrackEdges> alpha{};
	std::array<float, 3> anchor{0.0f, 1.0f, 2.25f};
	float width{0.0f};
	float length{10.0f}; // bindTrack: one cell (1.0 * MAP_XY_FACTOR) between edges
	std::uint32_t texture{0};
	std::uint32_t top{0};
	std::uint32_t bottom{0};
	std::uint32_t count{0};
	std::uint32_t total{0};
	std::uint8_t haveAnchor{0};
	std::uint8_t haveCap{1};
	std::uint8_t airborne{0};
	std::uint8_t reserved{0};
};

// One track as drawn: its texture, its edges oldest first (as columns), and the limits it draws by (the detail level's
// MaxTankTrackEdges and MaxTankTrackOpaqueEdges).
struct TrackView
{
	std::string texture;
	std::vector<std::array<float, 3>> left;
	std::vector<std::array<float, 3>> right;
	std::vector<float> v;
	std::vector<float> alpha;
	std::uint32_t maxEdges{100};
	std::uint32_t maxOpaqueEdges{25};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::TrackMarks>
{
	static constexpr std::string_view StableName = "generalszh.presentation.track_marks";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
