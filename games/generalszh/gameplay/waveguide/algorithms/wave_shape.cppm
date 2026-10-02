export module games.generalszh.gameplay.waveguide.algorithms.wave_shape;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;
export import engine.gameplay.common.spatial.resources.ground_height;
export import Engine.Core.Math.FixedRandom;

// The flood wave's geometry (WaveGuideUpdate, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/WaveGuideUpdate.cpp):
//   WaveShapePoints (computeWaveShapePoints): its front's sample points in its own frame, from y = -halfY (Int halfY =
//     YSize / 2, toward zero) while y < halfY, y += LinearWaveSpacing kept a whole number (an Int: toward zero), at most
//     MAX_WAVEGUIDE_SHAPE_POINTS (64); each bent back to x = -(y * y) / WaveBendMagnitude (none: x = 0);
//   WavePoint (Object::transformPoint): a point of its frame in the world, turned by its facing (only its yaw: the wave
//     flies level) and moved to where it is;
//   BehindWave (doDamage's test): the original takes the cosine between the direction from a sample point to the object
//     and the sample point's own world position (not the wave's facing, as its comment says): the object is "behind" the
//     wave when that dot product is below zero. Kept exactly as it decides who the wave hurts; the sign is the same
//     without the normalisation (a zero direction: not behind);
//   StartAlongPath (startMoving): the path from the waypoint named "WaveGuide1" (none: the wave stays where it is, its
//     final destination the origin): a waypoint with more than one link anywhere along it, or a first waypoint with no
//     link, is a bad path (the wave is destroyed); else it ends at the last waypoint reached through the first links,
//     and the wave starts at the first waypoint on the ground facing its next one (Coord2D::toAngle);
//   BridgeParticleYaw: the BridgeParticle's facing: the bridge's from-to angle (toAngle) plus BridgeParticleAngleFudge.
export namespace generalszh::gameplay
{
inline constexpr std::size_t MaxWaveShapePoints = 64; // MAX_WAVEGUIDE_SHAPE_POINTS
// PATH_EXTRA_DISTANCE (10 * PATHFIND_CELL_SIZE_F): the wave ends this near (2D) its path's last waypoint.
inline constexpr std::int64_t WaveEndDistance = 100;
// LOGICFRAMES_PER_SECOND / 2.0f: a splash sound is tried on the first frame more than this after the last try.
inline constexpr std::uint64_t WaveSplashFrames = 15;

struct WaveShape
{
	std::array<Engine::Math::FixedVector2, MaxWaveShapePoints> points{};
	std::uint32_t count{0};
};

// (Int) of a Real: toward zero.
inline std::int64_t TowardZero(Engine::Math::Fixed value) noexcept
{
	const std::int64_t raw = value.Raw();
	return raw >= 0 ? raw >> Engine::Math::Fixed::FractionBits : -((-raw) >> Engine::Math::Fixed::FractionBits);
}

inline WaveShape WaveShapePoints(Engine::Math::Fixed ySize, Engine::Math::Fixed spacing, Engine::Math::Fixed bend) noexcept
{
	using Engine::Math::Fixed;
	WaveShape shape;
	const std::int64_t halfY = TowardZero(ySize / Fixed::FromInt(2));
	for (std::int64_t y = -halfY; y < halfY; y = TowardZero(Fixed::FromInt(y) + spacing))
	{
		if (shape.count >= MaxWaveShapePoints)
			break;
		Engine::Math::FixedVector2 &point = shape.points[shape.count++];
		point.x = bend != Fixed{} ? Fixed::FromInt(-(y * y)) / bend : Fixed{};
		point.y = Fixed::FromInt(y);
	}
	return shape;
}

inline Engine::Math::FixedVector2 WavePoint(Engine::Math::FixedVector2 position, Engine::Math::TurnAngle facing, Engine::Math::FixedVector2 local) noexcept
{
	const Engine::Math::Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	return {position.x + c * local.x - s * local.y, position.y + s * local.x + c * local.y};
}

inline bool BehindWave(Engine::Math::FixedVector2 object, Engine::Math::FixedVector3 point) noexcept
{
	const Engine::Math::Fixed dx = object.x - point.x, dy = object.y - point.y;
	return dx * point.x + dy * point.y < Engine::Math::Fixed{};
}

// Coord2D::toAngle: atan2(y, x); a zero vector 0.
inline Engine::Math::TurnAngle AngleOf(Engine::Math::FixedVector2 v) noexcept
{
	if (v.x == Engine::Math::Fixed{} && v.y == Engine::Math::Fixed{})
		return {};
	return Engine::Math::Atan2(v.y, v.x);
}

struct WavePath
{
	bool bad{false};   // destroy the wave
	bool found{false}; // a "WaveGuide1" waypoint: it starts there
	std::uint32_t first{engine::gameplay::WaypointGraph::None};
	Engine::Math::FixedVector3 start;
	Engine::Math::TurnAngle facing{};
	Engine::Math::FixedVector3 final;
};

inline WavePath StartAlongPath(const engine::gameplay::WaypointGraph &waypoints, const engine::gameplay::GroundHeight &ground)
{
	WavePath path;
	const std::uint32_t first = waypoints.Find("WaveGuide1");
	if (first == engine::gameplay::WaypointGraph::None)
		return path;
	// The whole path, through each waypoint's only link (one that loops back on itself never ends in the original:
	// stopped here once every waypoint could have been passed).
	std::uint32_t at = first;
	for (std::size_t steps = 0;; ++steps)
	{
		const auto links = waypoints.Links(at);
		if (links.size() > 1)
		{
			path.bad = true;
			return path;
		}
		path.final = waypoints.Position(at);
		if (links.empty() || steps > waypoints.Size())
			break;
		at = links.front();
	}
	const auto links = waypoints.Links(first);
	if (links.empty())
	{
		path.bad = true;
		return path;
	}
	const Engine::Math::FixedVector2 from = waypoints.Position(first).XY();
	path.found = true;
	path.first = first;
	path.facing = AngleOf(waypoints.Position(links.front()).XY() - from);
	path.start = {from.x, from.y, ground.At(from)};
	return path;
}

// doShoreEffects (even frames only, the caller's test): the points ShorelineEffectDistance behind each front point, in
// order, at the wave's own height; starting "under water", a point whose ground is above PreferredHeight after one that
// was not splashes WaveSplashRight01 at the point before it, one not above after one that was splashes WaveSplashLeft01
// at itself; the first point never splashes.
struct ShoreSplash
{
	bool right{false}; // WaveSplashRight01; else WaveSplashLeft01
	Engine::Math::FixedVector3 at;
};

template<typename Ground>
inline std::vector<ShoreSplash> ShoreSplashes(const WaveShape &shape, Engine::Math::FixedVector3 position, Engine::Math::TurnAngle facing,
	Engine::Math::Fixed shoreline, Engine::Math::Fixed preferredHeight, const Ground &ground)
{
	std::vector<ShoreSplash> splashes;
	bool underWater = true;
	Engine::Math::FixedVector3 previous;
	for (std::uint32_t point = 0; point < shape.count; ++point)
	{
		const Engine::Math::FixedVector2 behind{shape.points[point].x - shoreline, shape.points[point].y};
		const Engine::Math::FixedVector2 at = WavePoint(position.XY(), facing, behind);
		const Engine::Math::FixedVector3 here{at.x, at.y, position.z};
		if (ground(at) > preferredHeight)
		{
			if (underWater && point != 0)
				splashes.push_back({true, previous});
			underWater = false;
		}
		else
		{
			if (!underWater && point != 0)
				splashes.push_back({false, here});
			underWater = true;
		}
		previous = here;
	}
	return splashes;
}

// The splash sound's roll (GameLogicRandomValue(1, 100)): the original draws it from the logic's one sequence at each
// try; the port gives each wave its own sequence (the match seed and the wave's object id, the original's ObjectID),
// drawn in the order of its tries (its `tries`-th roll), so a checkpoint resumes it where it was. It plays above
// RandomSplashSoundFrequency.
inline constexpr std::uint64_t WaveSplashSalt = 0x57415645u;

inline std::int64_t SplashRoll(std::uint64_t seed, std::uint32_t objectId, std::uint32_t tries) noexcept
{
	auto random = Engine::Math::Stream(seed, {WaveSplashSalt, objectId, tries});
	return Engine::Math::UniformInt(random, 1, 100);
}

inline bool SplashPlays(std::int64_t roll, std::int32_t frequency) noexcept { return roll > frequency; }

inline Engine::Math::TurnAngle BridgeParticleYaw(Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to, Engine::Math::TurnAngle fudge) noexcept
{
	return AngleOf(to - from) + fudge;
}
}
