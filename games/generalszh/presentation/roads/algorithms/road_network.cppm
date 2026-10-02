export module games.generalszh.presentation.roads.algorithms.road_network;
import std;

export import games.generalszh.presentation.roads.resources.road_geometry;
export import games.generalszh.content.terrain.road_content;
export import engine.level.model.level;
import Engine.Core.Math.FixedPresentation;

// The map's roads as W3DRoadBuffer builds them (W3DRoadBuffer.cpp, the EA original where the fork differs), in floats as
// the original:
//   BuildRoadNetwork (loadRoads up to the geometry): addMapObjects (each ROAD_POINT1 map object followed by a ROAD_POINT2
//     makes a segment of the Road it names (none found: 8 wide, 1 in its texture, the road type of id 1), its ends' flags
//     angled / join, its corner tight or not; segments joined end to end chained in order, flipped to run on; identical
//     ones dropped; the whole list added again), updateCountsAndFlags (how many segments of the same type share each
//     end), insertTeeIntersections (three ends at a point: a Y when the legs fit one (insertY), else a tee, slanted (H)
//     when its arm is more than 30 degrees off square; four: a four way), insertCurveSegments (chained corners rounded
//     by 30 degree curve pieces of CORNER_RADIUS (TIGHT_CORNER_RADIUS) road widths, or mitred when under 27 degrees or
//     angled), insertCrossTypeJoins (an open join end gets an alpha fade, squared to a road of another type it meets,
//     which is then stacked under it); all within MaxRoadSegments.
//   BuildRoadGeometry (preloadRoadsInVertexAndIndexBuffers, loadFloat4PtSection, loadRoadsInVertexAndIndexBuffers): each
//     piece a quad of the texture (segments 85/512 down the texture, curves, Ys, Hs, tees and four ways, alpha joins at
//     their places in it) laid over the terrain a column per height map cell along it, each column two vertices at the
//     highest cell corner under it plus MAP_HEIGHT_SCALE / 8, columns the terrain lets be skipped skipped; the pieces of
//     each road type gathered in piece kind order; the road types (MaxRoadTypes of them, the last defined first) drawn
//     by stacking order. A piece of more than MAX_SEG_VERTEX (500) vertices or MAX_SEG_INDEX (2000) indices has none.
export namespace generalszh::presentation
{
// INI::parseReal on a road's RoadWidth / RoadWidthInTexture text: atof, to a float (empty: 0).
inline float RoadReal(std::string_view text) noexcept
{
	double value = 0.0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end == text.data())
		return 0.0f;
	return static_cast<float>(value);
}

enum class RoadPiece : std::uint8_t
{
	Segment,
	Curve,
	Tee,
	FourWay,
	ThreeWayY,
	ThreeWayH,
	ThreeWayHFlip,
	AlphaJoin,
	Count,
};

struct RoadVec2
{
	float x{0.0f}, y{0.0f};
	friend RoadVec2 operator+(RoadVec2 a, RoadVec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
	friend RoadVec2 operator-(RoadVec2 a, RoadVec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
	friend RoadVec2 operator*(RoadVec2 a, float k) noexcept { return {a.x * k, a.y * k}; }
	friend RoadVec2 operator*(float k, RoadVec2 a) noexcept { return {a.x * k, a.y * k}; }
	friend RoadVec2 operator/(RoadVec2 a, float k) noexcept { return {a.x / k, a.y / k}; }
	RoadVec2 operator-() const noexcept { return {-x, -y}; }
	RoadVec2 &operator+=(RoadVec2 b) noexcept { x += b.x; y += b.y; return *this; }
	RoadVec2 &operator-=(RoadVec2 b) noexcept { x -= b.x; y -= b.y; return *this; }
	RoadVec2 &operator*=(float k) noexcept { x *= k; y *= k; return *this; }
	bool operator==(const RoadVec2 &) const = default;
	float Dot(RoadVec2 b) const noexcept { return x * b.x + y * b.y; }
	float Length() const noexcept { return std::sqrt(x * x + y * y); }
	// Vector2::Normalize (unchanged when of no length).
	void Normalize() noexcept
	{
		const float squared = x * x + y * y;
		if (squared != 0.0f)
		{
			const float inverse = 1.0f / std::sqrt(squared);
			x *= inverse;
			y *= inverse;
		}
	}
	// Vector2::Rotate(theta).
	void Rotate(float theta) noexcept
	{
		const float s = std::sin(theta), c = std::cos(theta);
		const float nx = x * c + y * -s;
		const float ny = x * s + y * c;
		x = nx;
		y = ny;
	}
};

// TRoadPt.
struct RoadPoint
{
	RoadVec2 loc, top, bottom;
	std::int32_t count{0};
	bool last{false};
	bool multi{false};
	bool isAngled{false};
	bool isJoin{false};
};

// RoadSegment (its drawing data apart).
struct RoadSegment
{
	RoadPoint pt1, pt2;
	float curveRadius{0.0f};
	RoadPiece type{RoadPiece::Segment};
	float scale{1.0f};
	float widthInTexture{1.0f};
	std::int32_t uniqueID{0};
};

// A road type the buffer draws (RoadType): its id, texture and stacking order.
struct RoadTypeSlot
{
	std::int32_t uniqueID{-1};
	std::int32_t stacking{0};
	std::string texture;
};

struct RoadNetwork
{
	std::vector<RoadSegment> roads; // m_roads[0 .. m_numRoads)
	std::vector<RoadTypeSlot> types;
};

namespace road_detail
{
inline constexpr float Pi = 3.14159265359f;
inline constexpr float MapXYFactor = 10.0f;
inline constexpr float MapHeightScale = MapXYFactor / 16.0f;
inline constexpr float CornerRadius = 1.5f;
inline constexpr float TightCornerRadius = 0.5f;
inline constexpr float TeeWidthAdjustment = 1.03f;
inline constexpr float DefaultRoadScale = 8.0f;
inline constexpr float MinRoadSegment = 0.25f;
inline constexpr int MaxSegVertex = 500;
inline constexpr int MaxSegIndex = 2000;
// MapObject flags.
inline constexpr std::uint32_t RoadPoint1 = 0x02;
inline constexpr std::uint32_t RoadPoint2 = 0x04;
inline constexpr std::uint32_t RoadCornerAngled = 0x08;
inline constexpr std::uint32_t RoadCornerTight = 0x40;
inline constexpr std::uint32_t RoadJoin = 0x80;

struct Vec3
{
	float x{0.0f}, y{0.0f}, z{0.0f};
	friend Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
	friend Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	friend Vec3 operator*(Vec3 a, float k) noexcept { return {a.x * k, a.y * k, a.z * k}; }
	friend Vec3 operator*(float k, Vec3 a) noexcept { return {a.x * k, a.y * k, a.z * k}; }
	Vec3 &operator+=(Vec3 b) noexcept { x += b.x; y += b.y; z += b.z; return *this; }
	float Dot(Vec3 b) const noexcept { return x * b.x + y * b.y + z * b.z; }
	float Length() const noexcept { return std::sqrt(x * x + y * y + z * z); }
	void Normalize() noexcept
	{
		const float squared = x * x + y * y + z * z;
		if (squared != 0.0f)
		{
			const float inverse = 1.0f / std::sqrt(squared);
			x *= inverse;
			y *= inverse;
			z *= inverse;
		}
	}
	// Vector3::Rotate_Z(angle).
	void RotateZ(float angle) noexcept
	{
		const float s = std::sin(angle), c = std::cos(angle);
		const float tx = x, ty = y;
		x = c * tx - s * ty;
		y = s * tx + c * ty;
	}
};

inline Vec3 Cross(Vec3 a, Vec3 b) noexcept { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// LineSegClass: its ends, their difference, its direction and length.
struct LineSeg
{
	Vec3 p0, p1, dp, dir;
	float length{0.0f};
	LineSeg(Vec3 from, Vec3 to) noexcept { Set(from, to); }
	void Set(Vec3 from, Vec3 to) noexcept
	{
		p0 = from;
		p1 = to;
		dp = p1 - p0;
		dir = dp;
		dir.Normalize();
		length = dp.Length();
	}
	// Find_Point_Closest_To.
	Vec3 ClosestTo(Vec3 at) const noexcept
	{
		const float along = dir.Dot(at - p0);
		if (along <= 0.0f)
			return p0;
		if (along >= length)
			return p1;
		return p0 + along * dir;
	}
	// Find_Intersection: the closest point on this line to the other (none: parallel).
	bool Intersection(const LineSeg &other, Vec3 &onThis) const noexcept
	{
		const Vec3 cross1 = Cross(dir, other.dir);
		const Vec3 cross2 = Cross(other.p0 - p0, other.dir);
		const float top1 = cross2.Dot(cross1), bottom1 = cross1.Dot(cross1);
		const Vec3 cross3 = Cross(other.dir, dir);
		const float bottom2 = cross3.Dot(cross3);
		if (bottom1 == 0.0f || bottom2 == 0.0f)
			return false;
		onThis = p0 + dir * (top1 / bottom1);
		return true;
	}
};

inline Vec3 To3(RoadVec2 v) noexcept { return {v.x, v.y, 0.0f}; }

// xpSign: the sign of the 2D cross product.
inline int XpSign(RoadVec2 a, RoadVec2 b) noexcept
{
	const float product = a.x * b.y - a.y * b.x;
	if (product < 0)
		return -1;
	if (product > 0)
		return 1;
	return 0;
}
inline float Cross2D(RoadVec2 a, RoadVec2 b) noexcept { return a.x * b.y - a.y * b.x; }

// The road buffer while it is built: m_roads (with one spare slot: the original writes a curve's ends before checking
// for room) and m_numRoads. Slots keep what was last written to them (fields the inserts leave alone stay as they were).
struct Build
{
	std::vector<RoadSegment> roads;
	int count{0};
	int limit{0};
	std::vector<RoadTypeSlot> *types{nullptr};
};

inline void FlipTheRoad(RoadSegment &road) noexcept { std::swap(road.pt1, road.pt2); }

inline void MoveRoadSegTo(Build &b, int from, int to)
{
	if (from < 0 || from >= b.count || to < 0 || to >= b.count || from == to)
		return;
	const RoadSegment moved = b.roads[from];
	if (from < to)
		for (int i = from; i < to; ++i)
			b.roads[i] = b.roads[i + 1];
	else
		for (int i = from; i > to; --i)
			b.roads[i] = b.roads[i - 1];
	b.roads[to] = moved;
}

inline void CheckLinkBefore(Build &b, int ndx)
{
	if (b.roads[ndx].pt2.count != 1)
		return;
	RoadVec2 loc2 = b.roads[ndx].pt2.loc;
	int endOfCurSeg = ndx + 1;
	while (endOfCurSeg < b.count - 1)
	{
		if (b.roads[endOfCurSeg].pt1.loc != b.roads[endOfCurSeg + 1].pt2.loc)
			break;
		++endOfCurSeg;
	}
	int checkNdx = endOfCurSeg + 1;
	while (checkNdx < b.count)
	{
		if (b.roads[checkNdx].pt1.loc == loc2)
		{
			MoveRoadSegTo(b, checkNdx, ndx);
			loc2 = b.roads[ndx].pt2.loc;
			if (b.roads[ndx].pt2.count != 1)
				return;
			++endOfCurSeg;
		}
		else if (b.roads[checkNdx].pt2.loc == loc2)
		{
			FlipTheRoad(b.roads[checkNdx]);
			MoveRoadSegTo(b, checkNdx, ndx);
			loc2 = b.roads[ndx].pt2.loc;
			if (b.roads[ndx].pt2.count != 1)
				return;
			++endOfCurSeg;
		}
		else
			++checkNdx;
		if (checkNdx <= endOfCurSeg)
			checkNdx = endOfCurSeg + 1;
	}
}

inline void CheckLinkAfter(Build &b, int ndx)
{
	if (b.roads[ndx].pt1.count != 1)
		return;
	if (ndx >= b.count - 1)
		return;
	RoadVec2 loc1 = b.roads[ndx].pt1.loc;
	int checkNdx = ndx + 1;
	while (checkNdx < b.count && ndx < b.count - 1)
	{
		if (b.roads[checkNdx].pt2.loc == loc1)
		{
			++ndx;
			MoveRoadSegTo(b, checkNdx, ndx);
			loc1 = b.roads[ndx].pt1.loc;
			if (b.roads[ndx].pt1.count != 1)
				return;
		}
		else if (b.roads[checkNdx].pt1.loc == loc1)
		{
			FlipTheRoad(b.roads[checkNdx]);
			++ndx;
			MoveRoadSegTo(b, checkNdx, ndx);
			loc1 = b.roads[ndx].pt1.loc;
			if (b.roads[ndx].pt1.count != 1)
				return;
		}
		else
			++checkNdx;
	}
}

inline void UpdateCounts(Build &b, RoadSegment &road)
{
	road.pt1.last = road.pt2.last = true;
	road.pt1.multi = road.pt2.multi = false;
	road.pt1.count = road.pt2.count = 0;
	const RoadVec2 loc1 = road.pt1.loc, loc2 = road.pt2.loc;
	for (int i = 0; i < b.count; ++i)
	{
		RoadSegment &other = b.roads[i];
		if (other.uniqueID != road.uniqueID)
			continue;
		if (other.pt1.loc == loc1)
		{
			++other.pt1.count;
			++road.pt1.count;
		}
		if (other.pt1.loc == loc2)
		{
			++other.pt1.count;
			++road.pt2.count;
		}
		if (other.pt2.loc == loc1)
		{
			++other.pt2.count;
			++road.pt1.count;
		}
		if (other.pt2.loc == loc2)
		{
			++other.pt2.count;
			++road.pt2.count;
		}
		other.pt1.multi = other.pt1.count > 1;
		other.pt2.multi = other.pt2.count > 1;
	}
	road.pt1.multi = road.pt1.count > 1;
	road.pt2.multi = road.pt2.count > 1;
}

// addMapObject.
inline void AddMapObject(Build &b, const RoadSegment &road, bool updateTheCounts)
{
	RoadSegment cur = road;
	const RoadVec2 loc1 = cur.pt1.loc, loc2 = cur.pt2.loc;
	RoadVec2 roadNormal{-(loc2.y - loc1.y), loc2.x - loc1.x};
	roadNormal.Normalize();
	roadNormal *= cur.scale * cur.widthInTexture / 2.0f;
	cur.pt1.top = loc1 + roadNormal;
	cur.pt1.bottom = loc1 - roadNormal;
	cur.pt2.top = loc2 + roadNormal;
	cur.pt2.bottom = loc2 - roadNormal;
	if (updateTheCounts)
		UpdateCounts(b, cur);
	if (b.count >= b.limit)
		return;
	int i = 0;
	bool flip = false, addBefore = false, addAfter = false, bothMatch = false;
	for (i = 0; i < b.count; ++i)
	{
		bothMatch = false;
		const RoadSegment &other = b.roads[i];
		if ((other.pt1.loc == loc1 && other.pt2.loc == loc2) || (other.pt1.loc == loc2 && other.pt2.loc == loc1))
		{
			bothMatch = true; // identical segment, so discard
			break;
		}
		if (cur.pt1.count == 1)
		{
			if (other.pt1.loc == loc1)
			{
				flip = true;
				addAfter = true;
			}
			if (other.pt2.loc == loc1)
			{
				flip = false;
				addBefore = true;
			}
		}
		if (cur.pt2.count == 1)
		{
			if (other.pt1.loc == loc2)
			{
				flip = false;
				addAfter = true;
			}
			if (other.pt2.loc == loc2)
			{
				flip = true;
				addBefore = true;
			}
		}
		if (addBefore || addAfter)
			break;
	}
	if (bothMatch)
		return;
	int addIndex = i;
	if (addAfter)
		++addIndex;
	if (addIndex < b.count)
		for (i = b.count; i > addIndex; --i)
			b.roads[i] = b.roads[i - 1];
	b.roads[addIndex] = cur;
	if (flip)
		FlipTheRoad(b.roads[addIndex]);
	++b.count;
	if (addBefore)
		CheckLinkBefore(b, addIndex);
	else if (addAfter)
		CheckLinkAfter(b, addIndex);
}

inline void UpdateCountsAndFlags(Build &b)
{
	for (int i = 0; i < b.count; ++i)
	{
		b.roads[i].pt1.last = b.roads[i].pt2.last = true;
		b.roads[i].pt1.count = b.roads[i].pt2.count = 0;
	}
	for (int j = b.count - 1; j > 0; --j)
	{
		const RoadVec2 loc1 = b.roads[j].pt1.loc, loc2 = b.roads[j].pt2.loc;
		for (int i = 0; i < j; ++i)
		{
			if (b.roads[i].uniqueID != b.roads[j].uniqueID)
				continue;
			if (b.roads[i].pt1.loc == loc1)
			{
				b.roads[i].pt1.last = false;
				++b.roads[i].pt1.count;
				++b.roads[j].pt1.count;
			}
			if (b.roads[i].pt1.loc == loc2)
			{
				b.roads[i].pt1.last = false;
				++b.roads[i].pt1.count;
				++b.roads[j].pt2.count;
			}
			if (b.roads[i].pt2.loc == loc1)
			{
				b.roads[i].pt2.last = false;
				++b.roads[i].pt2.count;
				++b.roads[j].pt1.count;
			}
			if (b.roads[i].pt2.loc == loc2)
			{
				b.roads[i].pt2.last = false;
				++b.roads[i].pt2.count;
				++b.roads[j].pt2.count;
			}
		}
	}
}

// The new joint piece every insert writes: at `loc` towards `to`, of the segment at `like`.
inline void WriteJoint(Build &b, RoadVec2 loc, RoadVec2 to, int like, std::int32_t pt1Count, RoadPiece type, float widthInTexture)
{
	RoadSegment &made = b.roads[b.count];
	made.pt1.loc = loc;
	made.pt2.loc = to;
	made.pt1.last = true;
	made.pt2.last = true;
	made.scale = b.roads[like].scale;
	made.widthInTexture = widthInTexture;
	made.pt1.count = pt1Count;
	made.type = type;
	made.uniqueID = b.roads[like].uniqueID;
	++b.count;
}

inline void Offset3Way(RoadPoint *pc1, RoadPoint *pc2, RoadPoint *pc3, RoadVec2 loc, RoadVec2 upVector, RoadVec2 teeVector, float widthInTexture)
{
	pc1->loc = loc - upVector;
	pc2->loc = loc + upVector;
	pc3->loc = loc + teeVector;
	float xpdct = Cross2D(upVector, teeVector);
	RoadVec2 rightTee = teeVector;
	if (xpdct < 0)
		rightTee = {-teeVector.x, -teeVector.y};
	rightTee *= widthInTexture;
	for (RoadPoint *pc : {pc1, pc2})
	{
		xpdct = Cross2D(upVector, pc->top - pc->loc);
		if (xpdct > 0)
		{
			pc->bottom = pc->loc - rightTee;
			pc->top = pc->loc + rightTee;
		}
		else
		{
			pc->bottom = pc->loc + rightTee;
			pc->top = pc->loc - rightTee;
		}
	}
	upVector *= widthInTexture;
	xpdct = Cross2D(rightTee, pc3->top - pc3->loc);
	if (xpdct < 0)
	{
		pc3->bottom = pc3->loc - upVector;
		pc3->top = pc3->loc + upVector;
	}
	else
	{
		pc3->bottom = pc3->loc + upVector;
		pc3->top = pc3->loc - upVector;
	}
}

inline void OffsetH(RoadPoint *pc1, RoadPoint *pc2, RoadPoint *pc3, RoadVec2 loc, RoadVec2 upVector, RoadVec2 teeVector, bool flip, bool mirror,
	float widthInTexture)
{
	if (flip != mirror)
	{
		pc1->loc = loc - upVector * 2.05f;
		pc2->loc = loc + upVector * .46f;
	}
	else
	{
		pc1->loc = loc - upVector * .46f;
		pc2->loc = loc + upVector * 2.05f;
	}
	float xpdct = Cross2D(upVector, teeVector);
	RoadVec2 rightTee = teeVector;
	if (xpdct < 0)
		rightTee = {-teeVector.x, -teeVector.y};
	rightTee *= widthInTexture;
	for (RoadPoint *pc : {pc1, pc2})
	{
		xpdct = Cross2D(upVector, pc->top - pc->loc);
		if (xpdct > 0)
		{
			pc->bottom = pc->loc - rightTee;
			pc->top = pc->loc + rightTee;
		}
		else
		{
			pc->bottom = pc->loc + rightTee;
			pc->top = pc->loc - rightTee;
		}
	}
	RoadVec2 arm = teeVector;
	arm.Rotate(flip ? Pi / 4 : -Pi / 4);
	RoadVec2 armNormal{-arm.y, arm.x};
	armNormal *= widthInTexture;
	pc3->loc += arm * 2.10f;
	if (XpSign(arm, pc3->top - loc) == 1)
	{
		pc3->top = pc3->loc + armNormal;
		pc3->bottom = pc3->loc - armNormal;
	}
	else
	{
		pc3->top = pc3->loc - armNormal;
		pc3->bottom = pc3->loc + armNormal;
	}
}

inline void OffsetY(RoadPoint *pc1, RoadPoint *pc2, RoadPoint *pc3, RoadVec2 loc, RoadVec2 upVector, float widthInTexture)
{
	pc3->loc += upVector * 0.55f;
	pc3->top += upVector * 0.55f;
	pc3->bottom += upVector * 0.55f;
	RoadVec2 arm = upVector;
	arm.Rotate(3 * Pi / 4);
	RoadVec2 armNormal{-arm.y, arm.x};
	armNormal *= widthInTexture;
	pc2->loc += arm * 1.1f;
	if (XpSign(arm, pc2->top - loc) == 1)
	{
		pc2->top = pc2->loc + armNormal;
		pc2->bottom = pc2->loc - armNormal;
	}
	else
	{
		pc2->top = pc2->loc - armNormal;
		pc2->bottom = pc2->loc + armNormal;
	}
	arm = upVector;
	arm.Rotate(-3 * Pi / 4);
	armNormal = {-arm.y, arm.x};
	armNormal *= widthInTexture;
	pc1->loc += arm * 1.1f;
	if (XpSign(arm, pc1->top - loc) == 1)
	{
		pc1->top = pc1->loc + armNormal;
		pc1->bottom = pc1->loc - armNormal;
	}
	else
	{
		pc1->top = pc1->loc - armNormal;
		pc1->bottom = pc1->loc + armNormal;
	}
}

inline void Offset4Way(RoadPoint *pc1, RoadPoint *pc2, RoadPoint *pc3, RoadPoint *pr3, RoadPoint *pc4, RoadVec2 loc, RoadVec2 alignVector,
	float widthInTexture)
{
	pc1->loc = loc - alignVector;
	pc2->loc = loc + alignVector;
	const RoadVec2 v3 = pr3->loc - loc;
	float angle = Pi / 2;
	if (Cross2D(alignVector, v3) < 0)
		angle = -angle;
	RoadVec2 teeVector = alignVector;
	teeVector.Rotate(angle);
	pc3->loc = loc + teeVector;
	pc4->loc = loc - teeVector;
	RoadVec2 realTee = alignVector;
	realTee.Rotate(Pi / 2);
	realTee *= widthInTexture;
	for (RoadPoint *pc : {pc1, pc2})
	{
		if (Cross2D(alignVector, pc->top - pc->loc) > 0)
		{
			pc->bottom = pc->loc - realTee;
			pc->top = pc->loc + realTee;
		}
		else
		{
			pc->bottom = pc->loc + realTee;
			pc->top = pc->loc - realTee;
		}
	}
	alignVector *= widthInTexture;
	for (RoadPoint *pc : {pc3, pc4})
	{
		if (Cross2D(realTee, pc->top - pc->loc) < 0)
		{
			pc->bottom = pc->loc - alignVector;
			pc->top = pc->loc + alignVector;
		}
		else
		{
			pc->bottom = pc->loc + alignVector;
			pc->top = pc->loc - alignVector;
		}
	}
	for (RoadPoint *pc : {pc1, pc2, pc3, pc4})
	{
		pc->last = true;
		pc->count = 0;
	}
}

// The segments after `index1` with an end at `loc` (their counts there set to -2): the far ends and the ends at loc.
struct Legs
{
	std::array<RoadPoint *, 4> outer{};
	std::array<RoadPoint *, 4> centre{};
	std::size_t found{1};
};

inline Legs FindLegs(Build &b, RoadVec2 loc, int index1, std::size_t maximum)
{
	Legs legs;
	if (b.roads[index1].pt1.loc == loc)
	{
		legs.outer[0] = &b.roads[index1].pt2;
		legs.centre[0] = &b.roads[index1].pt1;
	}
	else
	{
		legs.outer[0] = &b.roads[index1].pt1;
		legs.centre[0] = &b.roads[index1].pt2;
	}
	const auto add = [&](RoadPoint *end, RoadPoint *centre) {
		// The last leg slot is overwritten by any further leg.
		const std::size_t slot = (std::min)(legs.found, maximum - 1);
		legs.outer[slot] = end;
		legs.centre[slot] = centre;
		if (legs.found < maximum)
			++legs.found;
	};
	for (int i = index1 + 1; i < b.count; ++i)
	{
		if (b.roads[i].pt1.loc == loc)
		{
			b.roads[i].pt1.count = -2;
			add(&b.roads[i].pt2, &b.roads[i].pt1);
		}
		if (b.roads[i].pt2.loc == loc)
		{
			b.roads[i].pt2.count = -2;
			add(&b.roads[i].pt1, &b.roads[i].pt2);
		}
	}
	return legs;
}

inline bool InsertY(Build &b, RoadVec2 loc, int index1, float scale)
{
	Legs legs = FindLegs(b, loc, index1, 3);
	if (legs.found < 3)
		return false;
	RoadPoint *pc1 = legs.centre[0], *pc2 = legs.centre[1], *pc3 = legs.centre[2];
	RoadVec2 v1 = legs.outer[0]->loc - loc;
	v1.Normalize();
	RoadVec2 v2 = legs.outer[1]->loc - loc;
	v2.Normalize();
	RoadVec2 v3 = legs.outer[2]->loc - loc;
	v3.Normalize();
	bool do12 = false, do13 = false, do32 = false;
	const float dot12 = v1.Dot(v2), dot13 = v1.Dot(v3), dot32 = v3.Dot(v2);
	float score12 = 2.0f, score13 = 2.0f, score32 = 2.0f;
	constexpr float cos30 = 0.866f;
	constexpr float cos45 = 0.707f;
	if (dot12 < -cos30 || dot13 < -cos30 || dot32 < -cos30)
		return false; // too close to a straight line: a straight side tee
	int s1 = 0;
	int s2 = XpSign(v1, v2);
	int s3 = XpSign(v1, v3);
	if (s2 != s3 && s2 + s3 == 0)
	{
		const RoadVec2 v1_90{-v1.y, v1.x};
		if (XpSign(v1_90, v2) == 1 && XpSign(v1_90, v3) == 1)
		{
			do32 = true;
			score32 = std::fabs(dot12 + cos45) + std::fabs(dot13 + cos45);
		}
	}
	s1 = XpSign(v3, v1);
	s2 = XpSign(v3, v2);
	if (s2 != s1 && s2 + s1 == 0)
	{
		const RoadVec2 v3_90{-v3.y, v3.x};
		if (XpSign(v3_90, v2) == 1 && XpSign(v3_90, v1) == 1)
		{
			do12 = true;
			score12 = std::fabs(dot13 + cos45) + std::fabs(dot32 + cos45);
		}
	}
	s1 = XpSign(v2, v1);
	s3 = XpSign(v2, v3);
	if (s3 != s1 && s3 + s1 == 0)
	{
		const RoadVec2 v2_90{-v2.y, v2.x};
		if (XpSign(v2_90, v3) == 1 && XpSign(v2_90, v1) == 1)
		{
			do13 = true;
			score13 = std::fabs(dot12 + cos45) + std::fabs(dot32 + cos45);
		}
	}
	if (score12 < score13)
	{
		do13 = false;
		if (score12 < score32)
			do32 = false;
		else
			do12 = false;
	}
	else
	{
		do12 = false;
		if (score13 < score32)
			do32 = false;
		else
			do13 = false;
	}
	RoadVec2 upVector;
	if (do12)
		upVector = v3;
	else if (do13)
		upVector = v2;
	else if (do32)
		upVector = v1;
	else
		return false;
	const float angle = -(Pi / 2);
	upVector.Normalize();
	upVector *= 0.5f * scale;
	RoadVec2 teeVector = upVector;
	teeVector.Rotate(angle);
	const float width = b.roads[index1].widthInTexture;
	if (do12)
	{
		if (XpSign(v3, v1) == -1)
			OffsetY(pc1, pc2, pc3, loc, upVector, width);
		else
			OffsetY(pc2, pc1, pc3, loc, upVector, width);
	}
	if (do13)
	{
		if (XpSign(v2, v1) == -1)
			OffsetY(pc1, pc3, pc2, loc, upVector, width);
		else
			OffsetY(pc3, pc1, pc2, loc, upVector, width);
	}
	if (do32)
	{
		if (XpSign(v1, v3) == -1)
			OffsetY(pc3, pc2, pc1, loc, upVector, width);
		else
			OffsetY(pc2, pc3, pc1, loc, upVector, width);
	}
	for (RoadPoint *pc : {pc1, pc2, pc3})
	{
		pc->last = true;
		pc->count = 0;
	}
	if (b.count >= b.limit)
		return false;
	WriteJoint(b, loc, loc + teeVector, index1, -3, RoadPiece::ThreeWayY, width);
	return true;
}

inline void InsertTee(Build &b, RoadVec2 loc, int index1, float scale)
{
	if (InsertY(b, loc, index1, scale))
		return;
	Legs legs = FindLegs(b, loc, index1, 3);
	if (legs.found < 3)
		return;
	RoadPoint *pc1 = legs.centre[0], *pc2 = legs.centre[1], *pc3 = legs.centre[2];
	RoadVec2 v1 = legs.outer[0]->loc - loc;
	v1.Normalize();
	RoadVec2 v2 = legs.outer[1]->loc - loc;
	v2.Normalize();
	RoadVec2 v3 = legs.outer[2]->loc - loc;
	v3.Normalize();
	const float dot12 = v1.Dot(v2), dot13 = v1.Dot(v3), dot32 = v3.Dot(v2);
	bool do12 = false, do13 = false, do32 = false;
	if (dot12 < dot13)
	{
		if (dot12 < dot32)
			do12 = true;
		else
			do32 = true;
	}
	else
	{
		if (dot13 < dot32)
			do13 = true;
		else
			do32 = true;
	}
	RoadVec2 upVector, decider;
	if (do12)
	{
		upVector = v2 - v1;
		decider = v3;
	}
	if (do13)
	{
		upVector = v3 - v1;
		decider = v2;
	}
	if (do32)
	{
		upVector = v2 - v3;
		decider = v1;
	}
	upVector.Normalize();
	const float width = b.roads[index1].widthInTexture;
	constexpr float cos60 = 0.5f;
	const float dot = std::fabs(upVector.Dot(decider));
	if (dot > cos60)
	{
		// The arm of the tee is slanted: a slanted tee.
		float angle = Pi / 2;
		const float xpdct = upVector.x * decider.y - upVector.y * decider.x;
		bool mirror = false;
		if (xpdct < 0)
		{
			angle = -angle;
			mirror = true;
		}
		upVector.Normalize();
		upVector *= 0.5f * scale;
		RoadVec2 teeVector = upVector;
		teeVector.Rotate(angle);
		bool flip = false;
		if (do12)
		{
			flip = XpSign(teeVector, v3) == 1;
			OffsetH(pc1, pc2, pc3, loc, upVector, teeVector, flip, mirror, width);
		}
		if (do13)
		{
			flip = XpSign(teeVector, v2) == 1;
			OffsetH(pc1, pc3, pc2, loc, upVector, teeVector, flip, mirror, width);
		}
		if (do32)
		{
			flip = XpSign(teeVector, v1) == 1;
			OffsetH(pc3, pc2, pc1, loc, upVector, teeVector, flip, mirror, width);
		}
		for (RoadPoint *pc : {pc1, pc2, pc3})
		{
			pc->last = true;
			pc->count = 0;
		}
		if (b.count >= b.limit)
			return;
		WriteJoint(b, loc, loc + teeVector, index1, -3, flip ? RoadPiece::ThreeWayHFlip : RoadPiece::ThreeWayH, width);
	}
	else
	{
		float angle = Pi / 2;
		if (Cross2D(upVector, decider) < 0)
			angle = -angle;
		upVector.Normalize();
		upVector *= 0.5f * scale;
		RoadVec2 teeVector = upVector;
		teeVector.Rotate(angle);
		if (do12)
			Offset3Way(pc1, pc2, pc3, loc, upVector, teeVector, width);
		if (do13)
			Offset3Way(pc1, pc3, pc2, loc, upVector, teeVector, width);
		if (do32)
			Offset3Way(pc3, pc2, pc1, loc, upVector, teeVector, width);
		for (RoadPoint *pc : {pc1, pc2, pc3})
		{
			pc->last = true;
			pc->count = 0;
		}
		if (b.count >= b.limit)
			return;
		WriteJoint(b, loc, loc + teeVector, index1, -3, RoadPiece::Tee, width);
	}
}

inline void Insert4Way(Build &b, RoadVec2 loc, int index1, float scale)
{
	Legs legs = FindLegs(b, loc, index1, 4);
	if (legs.found < 4)
		return;
	RoadPoint *pc1 = legs.centre[0], *pc2 = legs.centre[1], *pc3 = legs.centre[2], *pc4 = legs.centre[3];
	RoadPoint *pr1 = legs.outer[0], *pr2 = legs.outer[1], *pr3 = legs.outer[2];
	std::array<RoadVec2, 4> v{};
	for (std::size_t i = 0; i < 4; ++i)
	{
		v[i] = legs.outer[i]->loc - loc;
		v[i].Normalize();
	}
	const float dot12 = v[0].Dot(v[1]), dot13 = v[0].Dot(v[2]), dot14 = v[0].Dot(v[3]), dot23 = v[1].Dot(v[2]), dot24 = v[1].Dot(v[3]),
				dot34 = v[2].Dot(v[3]);
	int curPair = 12;
	float curDot = dot12;
	if (dot13 < curDot)
	{
		curPair = 13;
		curDot = dot13;
	}
	if (dot14 < curDot)
	{
		curPair = 14;
		curDot = dot14;
	}
	if (dot23 < curDot)
	{
		curPair = 23;
		curDot = dot23;
	}
	if (dot24 < curDot)
	{
		curPair = 24;
		curDot = dot24;
	}
	if (dot34 < curDot)
	{
		curPair = 34;
		curDot = dot34;
	}
	RoadVec2 alignVector;
	switch (curPair)
	{
	case 12: alignVector = v[1] - v[0]; break;
	case 13: alignVector = v[2] - v[0]; break;
	case 14: alignVector = v[3] - v[0]; break;
	case 23: alignVector = v[2] - v[1]; break;
	case 24: alignVector = v[3] - v[1]; break;
	default: alignVector = v[3] - v[2]; break;
	}
	alignVector.Normalize();
	alignVector *= 0.5f * scale;
	const float width = b.roads[index1].widthInTexture;
	switch (curPair)
	{
	case 12: Offset4Way(pc1, pc2, pc3, pr3, pc4, loc, alignVector, width); break;
	case 13: Offset4Way(pc1, pc3, pc2, pr2, pc4, loc, alignVector, width); break;
	case 14: Offset4Way(pc1, pc4, pc3, pr3, pc2, loc, alignVector, width); break;
	case 23: Offset4Way(pc2, pc3, pc1, pr1, pc4, loc, alignVector, width); break;
	case 24: Offset4Way(pc2, pc4, pc1, pr1, pc3, loc, alignVector, width); break;
	default: Offset4Way(pc3, pc4, pc1, pr1, pc2, loc, alignVector, width); break;
	}
	if (alignVector.x < 0)
		alignVector = {-alignVector.x, -alignVector.y}; // symmetrical: make it go right
	if (b.count >= b.limit)
		return;
	WriteJoint(b, loc, loc + alignVector, index1, -4, RoadPiece::FourWay, TeeWidthAdjustment);
}

inline void InsertTeeIntersections(Build &b)
{
	const int segments = b.count;
	for (int i = 0; i < segments; ++i)
	{
		if (b.roads[i].type != RoadPiece::Segment)
			continue;
		if (b.roads[i].pt1.count == 2)
			InsertTee(b, b.roads[i].pt1.loc, i, b.roads[i].scale);
		if (b.roads[i].pt2.count == 2)
			InsertTee(b, b.roads[i].pt2.loc, i, b.roads[i].scale);
		if (b.roads[i].pt1.count == 3)
			Insert4Way(b, b.roads[i].pt1.loc, i, b.roads[i].scale);
		if (b.roads[i].pt2.count == 3)
			Insert4Way(b, b.roads[i].pt2.loc, i, b.roads[i].scale);
	}
}

inline void Miter(Build &b, int ndx1, int ndx2)
{
	Vec3 at;
	{
		const LineSeg line1(To3(b.roads[ndx1].pt1.top), To3(b.roads[ndx1].pt2.top));
		const LineSeg line2(To3(b.roads[ndx2].pt1.top), To3(b.roads[ndx2].pt2.top));
		if (line1.Intersection(line2, at))
		{
			b.roads[ndx2].pt2.top = {at.x, at.y};
			b.roads[ndx1].pt1.top = {at.x, at.y};
		}
	}
	const LineSeg line1(To3(b.roads[ndx1].pt1.bottom), To3(b.roads[ndx1].pt2.bottom));
	const LineSeg line2(To3(b.roads[ndx2].pt1.bottom), To3(b.roads[ndx2].pt2.bottom));
	if (line1.Intersection(line2, at))
	{
		b.roads[ndx2].pt2.bottom = {at.x, at.y};
		b.roads[ndx1].pt1.bottom = {at.x, at.y};
	}
}

inline void RotateAbout(RoadVec2 &point, RoadVec2 center, float angle)
{
	RoadVec2 offset{point.x - center.x, point.y - center.y};
	const RoadVec2 original = offset;
	offset.Rotate(angle);
	offset.y -= original.y;
	offset.x -= original.x;
	point += offset;
}

inline bool AddCurve(Build &b, RoadVec2 from, RoadVec2 to, int like)
{
	b.roads[b.count].pt1.loc = from;
	b.roads[b.count].pt2.loc = to;
	if (b.count >= b.limit)
		return false;
	RoadSegment &made = b.roads[b.count];
	made.pt1.last = true;
	made.pt2.last = true;
	made.scale = b.roads[like].scale;
	made.widthInTexture = b.roads[like].widthInTexture;
	made.type = RoadPiece::Curve;
	made.curveRadius = b.roads[like].curveRadius;
	made.uniqueID = b.roads[like].uniqueID;
	++b.count;
	return true;
}

inline void InsertCurveSegmentAt(Build &b, int ndx1, int ndx2)
{
	constexpr float DotLimit = 0.5f;
	const float radius = b.roads[ndx1].curveRadius * b.roads[ndx1].scale;
	const RoadVec2 originalPt = b.roads[ndx1].pt1.loc;
	LineSeg line1(To3(b.roads[ndx1].pt1.loc), To3(b.roads[ndx1].pt2.loc));
	LineSeg line2(To3(b.roads[ndx2].pt1.loc), To3(b.roads[ndx2].pt2.loc));
	if (b.roads[ndx1].uniqueID != b.roads[ndx2].uniqueID)
		return;
	const float curSin = line1.dir.Dot(line2.dir);
	const float xpdct = line1.dir.x * line2.dir.y - line1.dir.y * line2.dir.x;
	RoadVec2 *pr1, *pr2, *pr3, *pr4;
	if (xpdct > 0)
	{
		pr1 = &b.roads[ndx1].pt1.loc;
		pr2 = &b.roads[ndx1].pt2.loc;
		pr3 = &b.roads[ndx2].pt1.loc;
		pr4 = &b.roads[ndx2].pt2.loc;
	}
	else
	{
		pr4 = &b.roads[ndx1].pt1.loc;
		pr3 = &b.roads[ndx1].pt2.loc;
		pr2 = &b.roads[ndx2].pt1.loc;
		pr1 = &b.roads[ndx2].pt2.loc;
		line1.Set(To3(*pr1), To3(*pr2));
		line2.Set(To3(*pr3), To3(*pr4));
	}
	const float angle = static_cast<float>(std::acos(static_cast<double>(curSin)));
	const float count = angle / (Pi / 6.0f); // 30 degree steps
	if (static_cast<double>(count) < 0.9 || b.roads[ndx1].pt1.isAngled)
	{
		Miter(b, ndx1, ndx2);
		return;
	}
	Vec3 offset1 = radius * line1.dir;
	Vec3 offset2 = radius * line2.dir;
	offset1.RotateZ(-Pi / 2);
	offset2.RotateZ(-Pi / 2);
	const LineSeg offsetLine1(To3(*pr1) + offset1, To3(*pr2) + offset1);
	const LineSeg offsetLine2(To3(*pr3) + offset2, To3(*pr4) + offset2);
	Vec3 pInt1, pInt3;
	if (!offsetLine1.Intersection(offsetLine2, pInt1))
		return;
	b.roads[ndx2].pt2.last = true;
	const LineSeg cross1(pInt1, pInt1 - offset2);
	const LineSeg cross2(pInt1, pInt1 - offset1);
	cross1.Intersection(line2, pInt1);
	cross2.Intersection(line1, pInt3);
	// Make sure the lines did not clip out of existence.
	if (line2.dir.Dot(pInt1 - To3(*pr3)) < DotLimit || line1.dir.Dot(To3(*pr2) - pInt3) < DotLimit)
	{
		*pr1 = originalPt;
		*pr4 = originalPt;
		Miter(b, ndx1, ndx2);
		return;
	}
	*pr4 = {pInt1.x, pInt1.y};
	const float step = -Pi / 6.0f;
	RoadVec2 pt2 = *pr4;
	RoadVec2 pt1 = *pr3;
	RoadVec2 direction{pt1.x - pt2.x, pt1.y - pt2.y};
	RoadVec2 centerOfCurve{-direction.y, direction.x};
	centerOfCurve.Normalize();
	centerOfCurve *= b.roads[ndx1].curveRadius * b.roads[ndx1].scale;
	centerOfCurve += pt2;
	RotateAbout(pt2, centerOfCurve, step);
	direction.Rotate(step);
	pt1 = pt2 + direction;
	if (!AddCurve(b, pt2, pt1, ndx1))
		return;
	if (count > 2.0f)
		for (int i = 2; static_cast<float>(i) < count; ++i)
		{
			direction.Rotate(step);
			RotateAbout(pt2, centerOfCurve, step);
			pt1 = pt2 + direction;
			if (b.count >= b.limit)
				return;
			AddCurve(b, pt2, pt1, ndx1);
		}
	*pr1 = {pInt3.x, pInt3.y};
	b.roads[ndx1].pt1.last = true;
	if (count > 1.0f)
	{
		pt2 = *pr1;
		pt1 = *pr1 + *pr1 - *pr2;
		direction = {pt1.x - pt2.x, pt1.y - pt2.y};
		pt1 = pt2 + direction;
		if (b.count >= b.limit)
			return;
		AddCurve(b, pt2, pt1, ndx1);
	}
	// Recalculate top & bottom.
	for (const auto &[index, first] : {std::pair{ndx1, true}, std::pair{ndx2, false}})
	{
		RoadSegment &road = b.roads[index];
		const RoadVec2 roadVector = road.pt2.loc - road.pt1.loc;
		RoadVec2 roadNormal{-roadVector.y, roadVector.x};
		roadNormal.Normalize();
		roadNormal *= road.scale * road.widthInTexture / 2.0f;
		RoadPoint &end = first ? road.pt1 : road.pt2;
		end.top = end.loc + roadNormal;
		end.bottom = end.loc - roadNormal;
	}
}

inline void InsertCurveSegments(Build &b)
{
	const int segments = b.count;
	int segmentStartIndex = -1;
	for (int i = 0; i < segments; ++i)
	{
		if (i < segments - 1 && b.roads[i].pt1.loc == b.roads[i + 1].pt2.loc)
		{
			if (b.roads[i + 1].pt2.count == 1 && b.roads[i].pt1.count == 1)
			{
				InsertCurveSegmentAt(b, i, i + 1);
				if (segmentStartIndex < 0)
					segmentStartIndex = i;
			}
		}
		else if (segmentStartIndex >= 0)
		{
			if (b.roads[i].pt1.loc == b.roads[segmentStartIndex].pt2.loc)
				if (b.roads[segmentStartIndex].pt2.count == 1 && b.roads[i].pt1.count == 1)
					InsertCurveSegmentAt(b, i, segmentStartIndex);
			segmentStartIndex = -1;
		}
	}
}

inline std::int32_t FindCrossTypeJoinVector(Build &b, RoadVec2 loc, RoadVec2 &joinVector, std::int32_t uniqueID)
{
	const int segments = b.count;
	for (int i = 0; i < segments; ++i)
	{
		const RoadSegment &road = b.roads[i];
		if (road.uniqueID == uniqueID || road.type != RoadPiece::Segment)
			continue;
		const RoadVec2 loc1 = road.pt1.loc, loc2 = road.pt2.loc;
		float loX = loc1.x, loY = loc1.y, hiX = loc1.x, hiY = loc1.y;
		if (loX > loc2.x)
			loX = loc2.x;
		if (loY > loc2.y)
			loY = loc2.y;
		if (hiX < loc2.x)
			hiX = loc2.x;
		if (hiY < loc2.y)
			hiY = loc2.y;
		loX -= road.scale / 2;
		loY -= road.scale / 2;
		hiX += road.scale / 2;
		hiY += road.scale / 2;
		if (loc.x >= loX && loc.y >= loY && loc.x <= hiX && loc.y <= hiY)
		{
			const LineSeg roadLine(To3(loc1), To3(loc2));
			const Vec3 at = To3(loc);
			const float distance = (roadLine.ClosestTo(at) - at).Length();
			if (distance < road.scale * 0.55f)
			{
				RoadVec2 roadVec = loc2 - loc1;
				roadVec.Rotate(XpSign(roadVec, joinVector) == 1 ? Pi / 2 : -Pi / 2);
				joinVector = roadVec;
				return road.uniqueID;
			}
		}
	}
	return 0;
}

inline void AdjustStacking(Build &b, std::int32_t topUniqueID, std::int32_t bottomUniqueID)
{
	auto &types = *b.types;
	std::size_t i = 0, j = 0;
	for (i = 0; i < types.size(); ++i)
		if (types[i].uniqueID == topUniqueID)
			break;
	if (i >= types.size())
		return;
	for (j = 0; j < types.size(); ++j)
		if (types[j].uniqueID == bottomUniqueID)
			break;
	if (j >= types.size())
		return;
	if (types[i].stacking > types[j].stacking)
		return; // already on top
	const std::int32_t newStacking = types[j].stacking;
	for (RoadTypeSlot &type : types)
		if (type.stacking > newStacking)
			++type.stacking;
	types[i].stacking = newStacking + 1;
}

inline void InsertCrossTypeJoins(Build &b)
{
	const int segments = b.count;
	for (int i = 0; i < segments; ++i)
	{
		RoadVec2 loc1, loc2;
		bool isPt1 = false;
		if (b.roads[i].pt2.count == 0 && b.roads[i].pt2.isJoin)
		{
			loc1 = b.roads[i].pt2.loc;
			loc2 = b.roads[i].pt1.loc;
		}
		else if (b.roads[i].pt1.count == 0 && b.roads[i].pt1.isJoin)
		{
			loc1 = b.roads[i].pt1.loc;
			loc2 = b.roads[i].pt2.loc;
			isPt1 = true;
		}
		else
			continue;
		RoadVec2 roadVector{loc2.x - loc1.x, loc2.y - loc1.y};
		roadVector.Normalize();
		RoadVec2 joinVector = roadVector;
		const std::int32_t otherID = FindCrossTypeJoinVector(b, loc1, joinVector, b.roads[i].uniqueID);
		if (otherID == 0)
			joinVector *= 100;
		const RoadVec2 roadNormal{-roadVector.y, roadVector.x};
		const RoadVec2 joinNormal{-joinVector.y, joinVector.x};
		RoadSegment &road = b.roads[i];
		const float half = 2.0f;
		RoadVec2 p1 = loc1 + roadNormal * road.scale * road.widthInTexture / half;
		RoadVec2 p2 = loc2 + roadNormal * road.scale * road.widthInTexture / half;
		LineSeg roadLine(To3(p1), To3(p2));
		const Vec3 vLoc1 = To3(loc1);
		const LineSeg joinLine(vLoc1, To3(joinNormal) + vLoc1);
		Vec3 pInt1;
		RoadVec2 top = road.pt1.top;
		if (joinLine.Intersection(roadLine, pInt1))
		{
			if (isPt1)
			{
				road.pt1.top = {pInt1.x, pInt1.y};
				top = road.pt1.top;
			}
			else
			{
				road.pt2.bottom = {pInt1.x, pInt1.y};
				top = road.pt2.bottom;
			}
		}
		p1 = loc1 - roadNormal * road.scale * road.widthInTexture / half;
		p2 = loc2 - roadNormal * road.scale * road.widthInTexture / half;
		roadLine.Set(To3(p1), To3(p2));
		RoadVec2 bottom = road.pt1.bottom;
		if (joinLine.Intersection(roadLine, pInt1))
		{
			if (isPt1)
			{
				road.pt1.bottom = {pInt1.x, pInt1.y};
				bottom = road.pt1.bottom;
			}
			else
			{
				road.pt2.top = {pInt1.x, pInt1.y};
				bottom = road.pt2.top;
			}
		}
		bottom = bottom - top;
		const float scaleAdjustment = bottom.Length() / (road.scale * road.widthInTexture);
		if (otherID != 0)
			AdjustStacking(b, road.uniqueID, otherID);
		if (b.count >= b.limit)
			return;
		WriteJoint(b, loc1, loc1 + joinVector, i, 0, RoadPiece::AlphaJoin, b.roads[i].scale * scaleAdjustment);
	}
}

// The terrain the roads lie on: the height map in world units, its border, and its lowest and highest samples.
struct Ground
{
	const engine::level::Heightfield *field{nullptr};
	std::vector<float> heights;
	float lowest{0.0f}, highest{0.0f};

	explicit Ground(const engine::level::Heightfield &terrain) : field(&terrain)
	{
		heights.reserve(terrain.heights.size());
		for (const auto &height : terrain.heights)
			heights.push_back(Engine::Math::ToFloat(height));
		if (!heights.empty())
		{
			const auto [low, high] = std::minmax_element(heights.begin(), heights.end());
			lowest = *low;
			highest = *high;
		}
	}

	// BaseHeightMapRenderObjClass::getMaxCellHeight: the highest of the four corners of the cell under the point.
	float MaxCellHeight(float x, float y) const
	{
		const int width = static_cast<int>(field->width), height = static_cast<int>(field->height);
		if (width < 2 || height < 2)
			return 0.0f;
		int iX = static_cast<int>(x / MapXYFactor) + static_cast<int>(field->border);
		int iY = static_cast<int>(y / MapXYFactor) + static_cast<int>(field->border);
		if (iX < 0)
			iX = 0;
		if (iY < 0)
			iY = 0;
		if (iX >= width - 1)
			iX = width - 2;
		if (iY >= height - 1)
			iY = height - 2;
		const auto at = [&](int ix, int iy) { return heights[static_cast<std::size_t>(iy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(ix)]; };
		return std::max({at(iX, iY), at(iX + 1, iY), at(iX + 1, iY + 1), at(iX, iY + 1)});
	}
};

// RoadSegment::updateSegLighting: the height map vertex a road vertex takes its static lighting from.
inline std::array<std::int32_t, 2> LightCell(const Ground &ground, float x, float y)
{
	const auto border = static_cast<std::int32_t>(ground.field->border);
	return {static_cast<std::int32_t>(static_cast<double>(x / MapXYFactor) + 0.5) + border,
		static_cast<std::int32_t>(static_cast<double>(y / MapXYFactor) + 0.5) + border};
}

// One piece's drawing (RoadSegment's vertex and index buffers).
struct PieceMesh
{
	std::vector<RoadVertex> vertices;
	std::vector<std::uint32_t> indices;
};

// loadFloat4PtSection.
inline PieceMesh Load4PtSection(const Ground &ground, RoadVec2 loc, RoadVec2 roadNormal, RoadVec2 roadVector, const std::array<RoadVec2, 4> &corners,
	float uOffset, float vOffset, float uScale, float vScale)
{
	enum
	{
		BottomLeft = 0,
		BottomRight = 1,
		TopLeft = 2,
		TopRight = 3
	};
	constexpr float FloatAmount = MapHeightScale / 8;
	constexpr float MaxError = MapHeightScale * 1.1f;
	PieceMesh mesh;
	const float roadLen = roadVector.Length();
	const float halfHeight = roadNormal.Length();
	roadNormal.Normalize();
	roadVector.Normalize();
	int uCount = static_cast<int>(roadLen / MapXYFactor + 1);
	if (uCount < 2)
		uCount = 2;
	int vCount = static_cast<int>(2 * halfHeight / MapXYFactor + 1);
	if (vCount < 2)
		vCount = 2;
	constexpr int maxRows = 100;
	if (vCount > maxRows)
		vCount = maxRows;
	// Every column is collapsed to its two edge vertices at the column's highest cell (the original's "if (true)").
	struct Column
	{
		bool deleted{true};
		std::array<Vec3, 2> vtx{};
		std::array<int, 2> vertexIndex{-1, -1};
		float uIndex{0.0f};
	};
	Column prevColumn, curColumn, nextColumn;
	const Vec3 origin = To3(corners[BottomLeft]);
	const Vec3 uVector1 = To3(corners[BottomRight] - corners[BottomLeft]);
	Vec3 uVector2 = To3(corners[TopRight] - corners[TopLeft]);
	const Vec3 vVector1 = To3(corners[TopLeft] - corners[BottomLeft]);
	const Vec3 vVector2 = To3(corners[TopRight] - corners[BottomRight]);
	uVector2 += vVector1 - vVector2;
	int numVertices = 0;
	int numIndices = 0;
	for (int i = 0; i <= uCount; ++i)
	{
		const float iFactor = static_cast<float>(i) / static_cast<float>(uCount - 1);
		const float iBarFactor = 1.0f - iFactor;
		if (i < uCount)
		{
			nextColumn.deleted = false;
			nextColumn.uIndex = static_cast<float>(i);
			float maxHeight = ground.lowest;
			Vec3 first, lastVertex;
			for (int j = 0; j < vCount; ++j)
			{
				const float jFactor = static_cast<float>(j) / static_cast<float>(vCount - 1);
				const float jBarFactor = 1.0f - jFactor;
				Vec3 vtx = origin + (uVector1 * jBarFactor * iFactor) + (uVector2 * jFactor * iFactor) + (vVector1 * iBarFactor * jFactor) +
					(vVector2 * iFactor * jFactor);
				const float z = ground.MaxCellHeight(vtx.x, vtx.y);
				if (z > maxHeight)
					maxHeight = z;
				if (j == 0)
					first = vtx;
				lastVertex = vtx;
			}
			nextColumn.vtx[0] = {first.x, first.y, maxHeight};
			nextColumn.vtx[1] = {lastVertex.x, lastVertex.y, maxHeight};
			nextColumn.vertexIndex = {-1, -1};
			if (i < 2)
				curColumn = nextColumn;
			else
			{
				float theZ = prevColumn.vtx[0].z * (curColumn.uIndex - prevColumn.uIndex) + nextColumn.vtx[0].z * (nextColumn.uIndex - curColumn.uIndex);
				theZ /= nextColumn.uIndex - prevColumn.uIndex;
				if (theZ >= curColumn.vtx[0].z && theZ < curColumn.vtx[0].z + MaxError)
				{
					theZ = prevColumn.vtx[1].z * (curColumn.uIndex - prevColumn.uIndex) + nextColumn.vtx[1].z * (nextColumn.uIndex - curColumn.uIndex);
					theZ /= nextColumn.uIndex - prevColumn.uIndex;
					if (theZ >= curColumn.vtx[1].z && theZ < curColumn.vtx[1].z + MaxError)
						curColumn.deleted = true;
				}
			}
		}
		if (!curColumn.deleted && i != 1)
		{
			for (int j = 0; j < 2; ++j)
			{
				const RoadVec2 curVector{curColumn.vtx[j].x - loc.x, curColumn.vtx[j].y - loc.y};
				const float v = roadNormal.Dot(curVector);
				const float u = roadVector.Dot(curVector);
				RoadVertex vertex;
				vertex.uv = {uOffset + u / (uScale * 4), vOffset - v / (vScale * 4)};
				vertex.position = {curColumn.vtx[j].x, curColumn.vtx[j].y, curColumn.vtx[j].z + FloatAmount};
				vertex.lightCell = LightCell(ground, vertex.position[0], vertex.position[1]);
				mesh.vertices.push_back(vertex);
				curColumn.vertexIndex[j] = numVertices++;
			}
			if (i > 1)
			{
				// Both columns collapsed: two triangles between them.
				for (const int index : {prevColumn.vertexIndex[1], prevColumn.vertexIndex[0], curColumn.vertexIndex[0], prevColumn.vertexIndex[1],
						 curColumn.vertexIndex[0], curColumn.vertexIndex[1]})
					mesh.indices.push_back(static_cast<std::uint32_t>(index));
				numIndices += 6;
				prevColumn = curColumn;
			}
			else if (i == 0)
				prevColumn = curColumn;
			if (numVertices >= MaxSegIndex || numIndices >= MaxSegIndex)
				break;
		}
		curColumn = nextColumn;
	}
	// SetVertexBuffer / SetIndexBuffer refuse more than MAX_SEG_VERTEX vertices or MAX_SEG_INDEX indices (the original then
	// still adds the indices of a piece whose vertices it refused, pointing at other pieces' vertices: here the piece
	// draws nothing).
	if (numVertices > MaxSegVertex || numIndices > MaxSegIndex)
	{
		mesh.vertices.clear();
		mesh.indices.clear();
	}
	return mesh;
}

inline RoadVec2 SafeDirection(RoadVec2 loc1, RoadVec2 loc2, RoadVec2 &roadNormal)
{
	RoadVec2 roadVector{loc2.x - loc1.x, loc2.y - loc1.y};
	roadNormal = {-roadVector.y, roadVector.x};
	if (std::fabs(roadVector.x) < MinRoadSegment && std::fabs(roadVector.y) < MinRoadSegment)
	{
		roadVector = {1.0f, 0.0f};
		roadNormal = {0.0f, 1.0f};
	}
	else
	{
		roadVector.Normalize();
		roadNormal.Normalize();
	}
	return roadVector;
}

// loadFloatSection.
inline PieceMesh LoadFloatSection(const Ground &ground, RoadVec2 loc, RoadVec2 roadVector, float halfHeight, float left, float right, float uOffset,
	float vOffset, float scale)
{
	RoadVec2 roadNormal{-roadVector.y, roadVector.x};
	roadVector.Normalize();
	roadVector *= right;
	roadNormal.Normalize();
	if (halfHeight < 0)
		halfHeight = -halfHeight;
	roadNormal *= halfHeight;
	RoadVec2 roadLeft = roadVector;
	roadLeft.Normalize();
	roadLeft *= left;
	roadVector += roadLeft;
	RoadVec2 leftCenter = loc;
	leftCenter -= roadLeft;
	std::array<RoadVec2, 4> corners{};
	corners[0] = leftCenter - roadNormal;
	corners[1] = corners[0] + roadVector;
	corners[3] = corners[1] + 2.0f * roadNormal;
	corners[2] = corners[0] + 2.0f * roadNormal;
	return Load4PtSection(ground, loc, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
}

inline PieceMesh LoadPiece(const Ground &ground, const RoadSegment &road)
{
	enum
	{
		BottomLeft = 0,
		BottomRight = 1,
		TopLeft = 2,
		TopRight = 3
	};
	const RoadVec2 loc1 = road.pt1.loc, loc2 = road.pt2.loc;
	const float scale = road.scale;
	switch (road.type)
	{
	case RoadPiece::Segment:
	{
		// preloadRoadSegment.
		const RoadVec2 roadVector = loc2 - loc1;
		RoadVec2 roadNormal{-roadVector.y, roadVector.x};
		const float roadHeight = road.widthInTexture * road.scale / 2.0f;
		roadNormal.Normalize();
		roadNormal *= roadHeight;
		std::array<RoadVec2, 4> corners{};
		corners[BottomLeft] = road.pt1.bottom;
		corners[TopLeft] = road.pt1.top;
		corners[BottomRight] = road.pt2.bottom;
		corners[TopRight] = road.pt2.top;
		return Load4PtSection(ground, loc1, roadNormal, roadVector, corners, 0.0f, 85.0f / 512.0f, road.scale, road.scale);
	}
	case RoadPiece::Curve:
	{
		// loadCurve.
		const float uOffset = 4.0f / 512.0f;
		const bool tight = road.curveRadius == TightCornerRadius;
		const float vOffset = tight ? 425.0f / 512.0f : 255.0f / 512.0f;
		RoadVec2 roadVector{loc2.x - loc1.x, loc2.y - loc1.y};
		RoadVec2 roadNormal{-roadVector.y, roadVector.x};
		const float curveHeight = road.widthInTexture * scale / 2.0f;
		roadVector.Normalize();
		roadVector *= scale;
		roadNormal.Normalize();
		roadNormal *= std::fabs(curveHeight);
		std::array<RoadVec2, 4> corners{};
		corners[BottomLeft] = loc1 - roadNormal;
		corners[BottomRight] = corners[BottomLeft] + (tight ? roadVector * 0.5f : roadVector);
		corners[TopRight] = corners[BottomRight] + 2.0f * roadNormal;
		corners[TopLeft] = corners[BottomLeft] + 2.0f * roadNormal;
		const float side = tight ? 0.2f : 0.4f;
		corners[BottomRight] += roadVector * 0.1f;
		corners[BottomRight] += roadNormal * side;
		corners[BottomLeft] -= roadNormal * (tight ? 0.1f : 0.2f);
		corners[BottomLeft] -= roadVector * 0.02f;
		corners[TopLeft] -= roadVector * 0.02f;
		corners[TopRight] -= roadVector * 0.4f;
		corners[TopRight] += roadNormal * side;
		return Load4PtSection(ground, loc1, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
	}
	case RoadPiece::ThreeWayY:
	{
		// loadY.
		RoadVec2 roadNormal;
		RoadVec2 roadVector = SafeDirection(loc1, loc2, roadNormal);
		roadVector *= scale;
		roadNormal *= scale;
		roadVector *= 1.59f;
		std::array<RoadVec2, 4> corners{};
		corners[TopLeft] = loc1 + roadNormal * 0.29f - roadVector * 0.5f;
		corners[BottomLeft] = corners[TopLeft] - roadNormal * 1.08f;
		corners[BottomRight] = corners[BottomLeft] + roadVector;
		corners[TopRight] = corners[TopLeft] + roadVector;
		return Load4PtSection(ground, loc1, roadNormal, roadVector, corners, 255.0f / 512.0f, 226.0f / 512.0f, scale, scale);
	}
	case RoadPiece::ThreeWayH:
	case RoadPiece::ThreeWayHFlip:
	{
		// loadH.
		const bool flip = road.type == RoadPiece::ThreeWayHFlip;
		RoadVec2 roadNormal;
		RoadVec2 roadVector = SafeDirection(loc1, loc2, roadNormal);
		roadVector *= scale;
		roadNormal *= scale;
		roadNormal *= 1.35f;
		std::array<RoadVec2, 4> corners{};
		corners[BottomLeft] = loc1 - roadNormal * (flip ? 0.20f : 0.8f) - roadVector * road.widthInTexture / 2.0f;
		RoadVec2 width = roadVector * road.widthInTexture / 2.0f;
		width = width + roadVector * 1.2f;
		corners[BottomRight] = corners[BottomLeft] + width;
		corners[TopRight] = corners[BottomRight] + roadNormal;
		corners[TopLeft] = corners[BottomLeft] + roadNormal;
		if (flip)
			roadNormal = -roadNormal;
		return Load4PtSection(ground, loc1, roadNormal, roadVector, corners, 202.0f / 512.0f, 364.0f / 512.0f, scale, scale);
	}
	case RoadPiece::Tee:
	case RoadPiece::FourWay:
	{
		// loadTee.
		const bool fourWay = road.type == RoadPiece::FourWay;
		const float teeFactor = scale * TeeWidthAdjustment / 2.0f;
		const float left = road.widthInTexture * scale / 2.0f;
		return LoadFloatSection(ground, loc1, loc2 - loc1, teeFactor, left, teeFactor, 425.0f / 512.0f, fourWay ? 425.0f / 512.0f : 255.0f / 512.0f, scale);
	}
	case RoadPiece::AlphaJoin:
	{
		// loadAlphaJoin.
		RoadVec2 roadNormal;
		RoadVec2 roadVector = SafeDirection(loc1, loc2, roadNormal);
		const float uScale = road.widthInTexture;
		roadVector *= scale * 48 / 128;
		roadNormal *= uScale * (1 + 8.0f / 128);
		std::array<RoadVec2, 4> corners{};
		corners[TopLeft] = loc1 + roadNormal * 0.5f - roadVector * 0.65f;
		corners[BottomLeft] = corners[TopLeft] - roadNormal;
		corners[BottomRight] = corners[BottomLeft] + roadVector;
		corners[TopRight] = corners[TopLeft] + roadVector;
		return Load4PtSection(ground, loc1, roadNormal, roadVector, corners, 106.0f / 512.0f, 425.0f / 512.0f, scale, uScale);
	}
	default:
		return {};
	}
}
}

// W3DRoadBuffer::allocateRoadBuffers' road types (the roads in TerrainRoadCollection's list: the last defined first; at
// most `maxRoadTypes`), then loadRoads' segments, tees, curves and joins.
inline RoadNetwork BuildRoadNetwork(std::span<const engine::level::Placement> placements, const content::RoadCatalog &catalog, std::uint32_t maxRoadSegments,
	std::uint32_t maxRoadTypes)
{
	using namespace road_detail;
	RoadNetwork network;
	for (auto it = catalog.roads.rbegin(); it != catalog.roads.rend() && network.types.size() < maxRoadTypes; ++it)
		network.types.push_back({static_cast<std::int32_t>(it->id), 0, it->texture});
	Build b;
	b.limit = static_cast<int>(maxRoadSegments);
	b.roads.resize(static_cast<std::size_t>(b.limit) + 1);
	b.types = &network.types;
	// addMapObjects.
	for (std::size_t index = 0; index < placements.size(); ++index)
	{
		if (b.count >= b.limit)
			break;
		const engine::level::Placement &first = placements[index];
		if ((first.flags & RoadPoint1) == 0)
			continue;
		if (index + 1 >= placements.size())
			break;
		const engine::level::Placement &second = placements[index + 1];
		if ((second.flags & RoadPoint2) == 0)
			continue;
		RoadVec2 loc1{Engine::Math::ToFloat(first.position.x), Engine::Math::ToFloat(first.position.y)};
		RoadVec2 loc2{Engine::Math::ToFloat(second.position.x), Engine::Math::ToFloat(second.position.y)};
		if (loc1.x == loc2.x && loc1.y == loc2.y)
			loc2.x += 0.25f;
		RoadSegment road;
		road.scale = DefaultRoadScale;
		road.widthInTexture = 1.0f;
		road.uniqueID = 1;
		if (const content::RoadContent *kind = catalog.Find(first.type))
		{
			road.widthInTexture = RoadReal(kind->widthInTextureText);
			road.scale = RoadReal(kind->widthText);
			road.uniqueID = static_cast<std::int32_t>(kind->id);
		}
		road.pt1.loc = loc1;
		road.pt1.isAngled = (first.flags & RoadCornerAngled) != 0;
		road.pt1.isJoin = (first.flags & RoadJoin) != 0;
		road.pt2.loc = loc2;
		road.pt2.isAngled = (second.flags & RoadCornerAngled) != 0;
		road.pt2.isJoin = (second.flags & RoadJoin) != 0;
		road.type = RoadPiece::Segment;
		road.curveRadius = (first.flags & RoadCornerTight) != 0 ? TightCornerRadius : CornerRadius;
		AddMapObject(b, road, true);
		++index;
	}
	const int added = b.count;
	b.count = 0;
	for (int i = 0; i < added; ++i)
	{
		const RoadSegment road = b.roads[i];
		AddMapObject(b, road, false);
	}
	UpdateCountsAndFlags(b);
	InsertTeeIntersections(b);
	InsertCurveSegments(b);
	InsertCrossTypeJoins(b);
	network.roads.assign(b.roads.begin(), b.roads.begin() + b.count);
	return network;
}

// preloadRoadsInVertexAndIndexBuffers and loadRoadsInVertexAndIndexBuffers for every road type, in drawing order
// (stacking order, then the types' order); road types with nothing to draw left out.
inline RoadGeometry BuildRoadGeometry(const RoadNetwork &network, const engine::level::Heightfield &terrain)
{
	using namespace road_detail;
	const Ground ground(terrain);
	std::vector<PieceMesh> pieces;
	pieces.reserve(network.roads.size());
	for (const RoadSegment &road : network.roads)
		pieces.push_back(LoadPiece(ground, road));
	RoadGeometry geometry;
	std::int32_t maxStacking = 0;
	for (const RoadTypeSlot &type : network.types)
		maxStacking = std::max(maxStacking, type.stacking);
	for (std::int32_t stacking = 0; stacking <= maxStacking; ++stacking)
		for (const RoadTypeSlot &type : network.types)
		{
			if (type.stacking != stacking)
				continue;
			RoadMesh mesh;
			mesh.id = static_cast<std::uint32_t>(type.uniqueID);
			mesh.texture = type.texture;
			mesh.stacking = static_cast<std::uint32_t>(stacking);
			for (std::uint8_t kind = 0; kind < static_cast<std::uint8_t>(RoadPiece::Count); ++kind)
				for (std::size_t index = 0; index < network.roads.size(); ++index)
				{
					const RoadSegment &road = network.roads[index];
					if (road.type != static_cast<RoadPiece>(kind) || road.uniqueID != type.uniqueID || pieces[index].indices.empty())
						continue;
					const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
					mesh.vertices.insert(mesh.vertices.end(), pieces[index].vertices.begin(), pieces[index].vertices.end());
					for (const std::uint32_t at : pieces[index].indices)
						mesh.indices.push_back(base + at);
				}
			if (!mesh.indices.empty())
				geometry.meshes.push_back(std::move(mesh));
		}
	return geometry;
}
}
