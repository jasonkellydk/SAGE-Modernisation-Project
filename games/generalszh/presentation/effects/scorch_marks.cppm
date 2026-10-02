export module games.generalszh.presentation.effects.scorch_marks;
import std;

export import engine.ecs.system.system;

// Scorch marks on the terrain (the original's W3DScorch, the dynamic ones FX
// lists' TerrainScorch nuggets leave): where, how big, which of the scorch
// texture's marks (0..8, three a row). Oldest first; past MaxScorchMarks
// (500) the oldest goes. A mark of the same type within a quarter of its
// radius of one already there (position and radius) is not added again
// (W3DScorch::isDuplicate: the dynamic list deduplicates). `version` changes
// whenever the list does, for the renderer to rebuild its geometry.
export namespace generalszh::presentation
{
inline constexpr std::size_t MaxScorchMarks = 500;
inline constexpr std::uint32_t ScorchMarksInTexture = 9;

struct ScorchMark
{
	std::array<float, 3> at{};
	float radius{0.0f};
	std::uint32_t type{0};
};

struct ScorchMarks
{
	std::deque<ScorchMark> marks;
	std::uint64_t version{0};
};

// W3DScorch::addScorch: an out-of-range type is the first; the dynamic list deduplicates, the map's static one
// (W3DScorch(false): BaseHeightMapRenderObjClass::addStaticScorch) does not.
inline void AddScorch(ScorchMarks &scorches, ScorchMark mark, bool deduplicate = true)
{
	if (mark.type >= ScorchMarksInTexture)
		mark.type = 0;
	const float limit = mark.radius / 4.0f;
	if (deduplicate)
		for (const ScorchMark &other : scorches.marks)
			if (other.type == mark.type && std::abs(mark.at[0] - other.at[0]) < limit && std::abs(mark.at[1] - other.at[1]) < limit &&
				std::abs(mark.radius - other.radius) < limit)
				return;
	if (scorches.marks.size() >= MaxScorchMarks)
		scorches.marks.pop_front();
	scorches.marks.push_back(mark);
	++scorches.version;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::ScorchMarks>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scorch_marks";
};
}
