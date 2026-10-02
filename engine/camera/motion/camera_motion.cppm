export module engine.camera.motion.camera_motion;
import std;

export import Engine.Core.Math.Vector2;
export import Engine.Core.Math.Vector3;
import Engine.Core.Math.Scalar;

// Scripted camera motion data and helpers, ported from the legacy tactical
// view (W3DView TMoveAlongWaypointPathInfo / TRotateCameraInfo /
// TPitchCameraInfo / TZoomCameraInfo and GameClient ParabolicEase). The
// stepping logic that mutates the camera lives in engine.camera.model.rts_camera;
// this module holds the plain data and the pure math so it can be tested alone.
export namespace engine::camera
{
namespace Math = Engine::Math;

// Legacy MAX_WAYPOINTS: the path keeps at most this many real waypoints plus
// one padding entry at each end for the corner-smoothing interpolation.
inline constexpr int MaxWaypoints = 25;
inline constexpr int WaypointSlots = MaxWaypoints + 2;

// Legacy normAngle / WWMath::Normalize_Angle: range [-pi, pi), same order of operations.
inline float NormalizeAngle(float angle) noexcept
{
	return angle - (Math::Tau * std::floor((angle + Math::Pi) / Math::Tau));
}

// Legacy "angle toward" idiom used by setupWaypointPath, rotateCameraTowardPosition,
// cameraModLookToward and object tracking: yaw that makes the default camera
// (rotated 90 degrees) look along `direction`. Empty when the direction is shorter
// than 0.1 world units, where legacy code skips the update.
inline std::optional<float> YawToward(Math::Vector2 direction) noexcept
{
	const float length = direction.Length();
	if (length < 0.1f)
		return std::nullopt;
	float angle = std::acos(direction.x / length);
	if (direction.y < 0.0f)
		angle = -angle;
	// Default camera is rotated 90 degrees, so match.
	angle -= Math::Pi / 2;
	return NormalizeAngle(angle);
}

// Legacy ParabolicEase: constant acceleration for the ease-in fraction, constant
// speed in the middle, constant deceleration for the ease-out fraction. Maps
// [0,1] -> [0,1]. Out-of-range inputs are clamped (legacy asserted, then clamped).
class ParabolicEase
{
public:
	explicit ParabolicEase(float easeInTime = 0.0f, float easeOutTime = 0.0f) noexcept
	{
		SetEaseTimes(easeInTime, easeOutTime);
	}

	void SetEaseTimes(float easeInTime, float easeOutTime) noexcept
	{
		m_in = Clamp01(easeInTime);
		m_out = Clamp01(1.0f - easeOutTime);
		if (m_in > m_out)
			m_in = m_out;
	}

	float operator()(float param) const noexcept
	{
		param = Clamp01(param);
		const float v0 = 1.0f + m_out - m_in;
		if (param < m_in)
			return param * param / (v0 * m_in);
		if (param <= m_out)
			return static_cast<float>((m_in + 2.0 * (param - m_in)) / v0);
		return static_cast<float>(
			(m_in + 2.0 * (m_out - m_in) + (2.0 * (param - m_out) + m_out * m_out - param * param) / (1.0f - m_out)) / v0);
	}

	float EaseIn() const noexcept { return m_in; }
	float EaseOut() const noexcept { return m_out; }

private:
	static float Clamp01(float value) noexcept { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

	float m_in = 0.0f;
	float m_out = 1.0f;
};

// Legacy corner smoothing: the camera follows a quadratic curve from the middle
// of the previous segment, bent toward the corner waypoint `mid`, to the middle of
// the next segment. `factor` is in [0,1] across that curve. Z is not touched.
inline Math::Vector2 SmoothedCornerPoint(Math::Vector3 previous, Math::Vector3 mid, Math::Vector3 next, float factor) noexcept
{
	Math::Vector3 start = previous;
	start.x += mid.x;
	start.y += mid.y;
	start.x /= 2;
	start.y /= 2;
	Math::Vector3 end = mid;
	end.x += next.x;
	end.y += next.y;
	end.x /= 2;
	end.y /= 2;
	Math::Vector2 result{start.x, start.y};
	result.x += factor * (end.x - start.x);
	result.y += factor * (end.y - start.y);
	result.x += (1 - factor) * factor * (mid.x - end.x + mid.x - start.x);
	result.y += (1 - factor) * factor * (mid.y - end.y + mid.y - start.y);
	return result;
}

// Legacy TMoveAlongWaypointPathInfo. Index 0 and numWaypoints+1 are padding;
// index 1 is the camera's starting position.
struct WaypointPathMotion
{
	int numWaypoints = 0;
	std::array<Math::Vector3, WaypointSlots> waypoints{};
	std::array<float, WaypointSlots> waySegLength{};
	std::array<float, WaypointSlots> cameraAngle{};
	std::array<int, WaypointSlots> timeMultiplier{};
	float totalTimeMilliseconds = 0.0f;
	float elapsedTimeMilliseconds = 0.0f;
	float totalDistance = 0.0f;
	float curSegDistance = 0.0f;
	int shutter = 1;
	int curSegment = 0;
	int curShutter = 0;
	int rollingAverageFrames = 1;
	ParabolicEase ease;
};

// Where a tracked rotation target currently is. Returning nullopt means the
// target is gone (legacy: object destroyed); the last known position is kept.
using TargetPositionProvider = std::function<std::optional<Math::Vector3>()>;

// Legacy TRotateCameraInfo. The legacy union of {target object, angle range} is
// split into plain fields.
struct RotateMotion
{
	int numFrames = 0;
	float curFrame = 0.0f; // legacy frames elapsed (fractional: advanced by delta time)
	int startTimeMultiplier = 1;
	int endTimeMultiplier = 1;
	int numHoldFrames = 0;
	ParabolicEase ease;
	bool trackObject = false;
	TargetPositionProvider target;
	Math::Vector3 targetPosition{};
	float startAngle = 0.0f;
	float endAngle = 0.0f;
};

// Legacy TPitchCameraInfo (drives the FX pitch, not the camera pitch angle).
struct PitchMotion
{
	int numFrames = 0;
	float curFrame = 0.0f; // legacy frames elapsed (fractional: advanced by delta time)
	float startPitch = 1.0f;
	float endPitch = 1.0f;
	int startTimeMultiplier = 1;
	int endTimeMultiplier = 1;
	ParabolicEase ease;
};

// Legacy TZoomCameraInfo.
struct ZoomMotion
{
	int numFrames = 0;
	float curFrame = 0.0f; // legacy frames elapsed (fractional: advanced by delta time)
	float startZoom = 1.0f;
	float endZoom = 1.0f;
	int startTimeMultiplier = 1;
	int endTimeMultiplier = 1;
	ParabolicEase ease;
};

// Legacy REAL_TO_INT_FLOOR.
inline int FloorToInt(double value) noexcept { return static_cast<int>(std::floor(value)); }
} // namespace engine::camera
