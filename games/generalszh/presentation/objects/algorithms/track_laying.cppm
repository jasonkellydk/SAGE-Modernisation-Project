export module games.generalszh.presentation.objects.algorithms.track_laying;
import std;

export import games.generalszh.presentation.objects.components.track_marks;

// TerrainTracksRenderObjClass's edges, on presentation time (seconds):
//   AddTrackEdge (addEdgeToTrack): the first point only anchors (the track
//   is capped: nothing drawn yet); after that, once a track length from the
//   anchor, an edge across the track at the point (its end points half the
//   track width either side along the travel direction crossed with the
//   ground's normal, raised 0.2 cells), texture rows alternating edge to
//   edge, opaque unless the track restarts (just airborne, or its first
//   edges); a full ring drops its oldest;
//   CapTrack (addCapEdgeToTrack): ends the track with a transparent edge
//   (or makes the last one transparent when too near), so it resumes with a
//   fresh anchor;
//   FadeTrack (TerrainTracksRenderObjClassSystem::update): each opaque edge
//   fades over the fade delay from when it was laid; edges gone to nothing
//   leave from the oldest end.
export namespace generalszh::presentation
{
namespace track_laying_detail
{
inline void Lay(TrackMarks &track, const std::array<float, 3> &at, const std::array<float, 3> &normal, std::uint32_t maxEdges, double now, float alpha)
{
	if (track.count >= maxEdges)
	{
		++track.bottom;
		--track.count;
		if (track.bottom >= maxEdges)
			track.bottom = 0;
	}
	if (track.top >= maxEdges)
		track.top = 0;
	float dx = at[0] - track.anchor[0], dy = at[1] - track.anchor[1];
	const float length = std::sqrt(dx * dx + dy * dy);
	if (length > 0.0f)
	{
		dx /= length;
		dy /= length;
	}
	// vX = vDir x vZ.
	const std::array<float, 3> across{dy * normal[2], -dx * normal[2], dx * normal[1] - dy * normal[0]};
	TrackEdge &edge = track.edges[track.top];
	const float half = track.width * 0.5f;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		edge.ends[0][axis] = at[axis] - half * across[axis];
		edge.ends[1][axis] = at[axis] + half * across[axis];
	}
	edge.ends[0][2] += 0.2f * 10.0f;
	edge.ends[1][2] += 0.2f * 10.0f;
	const float row = (track.total & 1u) != 0 ? 0.0f : 1.0f;
	edge.uv = {{{0.0f, row}, {1.0f, row}}};
	edge.laid = now;
	edge.alpha = alpha;
	track.anchor = at;
	++track.count;
	++track.total;
	++track.top;
}
}

inline void AddTrackEdge(TrackMarks &track, const std::array<float, 3> &at, const std::array<float, 3> &normal, std::uint32_t maxEdges, double now)
{
	if (track.haveAnchor == 0)
	{
		track.anchor = at;
		track.haveAnchor = 1;
		track.airborne = 1;
		track.haveCap = 1; // single segment tracks are always capped because nothing is drawn
		return;
	}
	track.haveCap = 0;
	const float dx = at[0] - track.anchor[0], dy = at[1] - track.anchor[1], dz = at[2] - track.anchor[2];
	if (dx * dx + dy * dy + dz * dz < track.length * track.length)
		return;
	const float alpha = track.airborne != 0 || track.count <= 1 ? 0.0f : 1.0f;
	track.airborne = 0;
	track_laying_detail::Lay(track, at, normal, maxEdges, now, alpha);
}

inline void CapTrack(TrackMarks &track, const std::array<float, 3> &at, const std::array<float, 3> &normal, std::uint32_t maxEdges, double now)
{
	if (track.haveCap != 0)
		return;
	if (track.count == 1)
	{
		track.haveCap = 1;
		track.haveAnchor = 0;
		return;
	}
	const float dx = at[0] - track.anchor[0], dy = at[1] - track.anchor[1], dz = at[2] - track.anchor[2];
	if (dx * dx + dy * dy + dz * dz < track.length * track.length)
	{
		const std::uint32_t last = track.top == 0 ? maxEdges - 1 : track.top - 1;
		track.edges[last].alpha = 0.0f;
		track.haveCap = 1;
		track.haveAnchor = 0;
		return;
	}
	track_laying_detail::Lay(track, at, normal, maxEdges, now, 0.0f);
	track.haveCap = 1;
	track.haveAnchor = 0;
}

inline void FadeTrack(TrackMarks &track, std::uint32_t maxEdges, double fadeSeconds, double now)
{
	std::uint32_t index = track.bottom;
	for (std::uint32_t i = 0; i < track.count; ++i, ++index)
	{
		if (index >= maxEdges)
			index = 0;
		TrackEdge &edge = track.edges[index];
		float left = fadeSeconds > 0.0 ? static_cast<float>(1.0 - (now - edge.laid) / fadeSeconds) : 0.0f;
		if (left < 0.0f)
			left = 0.0f;
		if (edge.alpha > 0.0f)
			edge.alpha = left;
		if (left == 0.0f)
		{
			++track.bottom;
			--track.count;
			if (track.bottom >= maxEdges)
				track.bottom = 0;
		}
	}
}
}
