export module engine.camera.model.rts_camera;
import std;

export import engine.camera.motion.camera_motion;
export import Engine.Core.Math.AffineTransform3;
export import Engine.Core.Math.Vector2;
export import Engine.Core.Math.Vector3;
import Engine.Core.Math.LineSegment3;
import Engine.Core.Math.Scalar;

// Generic RTS tactical camera: a faithful port of the legacy Generals / Zero Hour
// tactical view camera (View + W3DView) without any renderer, terrain or object
// dependency. Client-side presentation state (floats), never simulation state.
//
// Clocks: the camera runs on real elapsed time, so it moves at the same speed
// whatever the render rate (rendering is uncapped; the simulation ticks at a
// fixed rate on its own).
//   - Update(timing) is called once per rendered frame with the frame's
//     elapsed milliseconds.
//   - Waypoint / move-to motion advances by the elapsed milliseconds.
//   - Rotate / zoom / pitch motions keep the original's durations in legacy
//     frames (round_down(ms / 33.33)), advanced by elapsed / 33.33 frames.
//   - Height-above-ground settling closes CameraAdjustSpeed of the gap per
//     legacy frame, applied as frame-rate independent exponential smoothing.
//   - StepFixed() is the legacy stepView(): called once per fixed logic step,
//     it only advances the simple spring camera shake.
export namespace engine::camera
{
namespace Math = Engine::Math;

// Ground height at a world XY. Supplied by the host (terrain logic).
using TerrainHeightFunction = std::function<float(float x, float y)>;
// Terrain ray cast over the segment [start, end]; returns the first hit.
// Only used by LookAt for targets raised above the ground.
using TerrainRayCastFunction = std::function<std::optional<Math::Vector3>(Math::Vector3 start, Math::Vector3 end)>;

// Legacy constants from View.h: the camera distance math is anchored to these,
// independent of the configurable default pitch/yaw.
inline constexpr float ViewDefaultPitchRadians = (37.5f * std::numbers::pi_v<float>) / 180.0f;
inline constexpr float ViewDefaultYawRadians = 0.0f;

// Legacy clamp(lo, value, hi): lower bound checked first, never UB on lo > hi.
constexpr float LegacyClamp(float lo, float value, float hi) noexcept
{
	return value < lo ? lo : (value > hi ? hi : value);
}

// Legacy DEG_TO_RADF: (degrees * pi) / 180, in this operation order.
constexpr float LegacyDegreesToRadians(float degrees) noexcept { return (degrees * Math::Pi) / 180.0f; }

// GameData camera settings. Defaults are the retail Zero Hour GameData.ini values
// (the GlobalData constructor defaults are placeholders overwritten by the INI).
struct CameraSettings
{
	float cameraPitchDegrees = 37.5f;   // CameraPitch: default pitch
	float cameraYawDegrees = 0.0f;      // CameraYaw: default yaw
	float cameraHeight = 232.0f;        // CameraHeight: scripted camera offset height
	float maxCameraHeight = 310.0f;     // MaxCameraHeight
	float minCameraHeight = 120.0f;     // MinCameraHeight
	float cameraAdjustSpeed = 0.3f;     // CameraAdjustSpeed: height settling rate
	float scrollAmountCutoff = 50.0f;   // ScrollAmountCutoff
	bool enforceMaxCameraHeight = false; // EnforceMaxCameraHeight
	float partitionCellSize = 40.0f;    // PartitionCellSize: camera-lock snap distance
	bool disableCameraMovement = false; // DisableCameraMovements
	bool useCameraConstraints = true;   // UseCameraConstraints
	bool drawEntireTerrain = false;     // DrawEntireTerrain: far clip 100000
	float maxShakeIntensity = 10.0f;    // MaxShakeIntensity
	float maxShakeRange = 150.0f;       // MaxShakeRange
	float mapXYFactor = 10.0f;          // MAP_XY_FACTOR: height-map cell size, near clip
	float pathfindCellSize = 10.0f;     // PATHFIND_CELL_SIZE_F: LookAt raised-target threshold
	int terrainDrawWidth = 129;         // WorldHeightMap::NORMAL_DRAW_WIDTH: fallback far clip cells
	float frameLengthMilliseconds = 1000.0f / 30.0f; // TheW3DFrameLengthInMsec
	float fieldOfViewRadians = LegacyDegreesToRadians(50.0f); // horizontal FOV (Set_View_Plane)
	float aspectRatio = 800.0f / 600.0f; // width / height of the tactical viewport
};

// Per render-frame timing handed to Update.
struct CameraFrameTiming
{
	// Real time since the previous Update.
	float deltaMilliseconds = 1000.0f / 30.0f;
	// False while the game is paused or debug-time-frozen: scripted motion holds.
	bool advanceScriptedMotion = true;
	// False while script time is frozen or the game is paused (only affects
	// PreviousLookAt bookkeeping during a camera lock).
	bool gameTimeAdvancing = true;
};

// The drawn terrain window, for the legacy far clip fit (TheTerrainRenderObject draw region).
struct TerrainDrawBounds
{
	Math::Vector2 lo{};
	Math::Vector2 hi{};
	float minHeight = 0.0f;
};

// Map playable XY extent, for the legacy camera area constraints.
struct MapExtent
{
	Math::Vector2 lo{};
	Math::Vector2 hi{};
};

// Everything a renderer needs to build a view-projection.
struct CameraView
{
	Math::Vector3 eye{};
	Math::Vector3 target{};
	// Camera-to-world: local +X right, +Y up, +Z backward (looks down -Z).
	Math::AffineTransform3 transform{};
	float horizontalFieldOfView = LegacyDegreesToRadians(50.0f);
	float aspectRatio = 800.0f / 600.0f;
	float nearClip = 10.0f;
	float farClip = 1290.0f;
};

struct EyeAndTarget
{
	Math::Vector3 eye{};
	Math::Vector3 target{};
};

enum class CameraLockMode : std::uint8_t
{
	Follow,
	Tether
};

// What a camera lock follows this frame. Nullopt from the provider = target gone.
struct LockTargetState
{
	Math::Vector3 position{};
	// Set when the target is airborne above terrain/water and the camera should
	// turn with it (legacy: object orientation; the camera yaw is orientation - pi/2).
	std::optional<float> airborneOrientation;
};
using LockTargetProvider = std::function<std::optional<LockTargetState>()>;

class RtsCamera
{
public:
	explicit RtsCamera(CameraSettings settings = {}, TerrainHeightFunction terrainHeight = {})
		: m_settings(settings), m_terrainHeight(std::move(terrainHeight))
	{
		// View::View + View::init + W3DView::W3DView + W3DView::init.
		m_maxHeightAboveGround = m_settings.maxCameraHeight;
		m_minHeightAboveGround = m_settings.minCameraHeight;
		m_defaultAngle = LegacyDegreesToRadians(m_settings.cameraYawDegrees);
		m_defaultPitch = LegacyDegreesToRadians(m_settings.cameraPitchDegrees);
		m_angle = m_defaultAngle;
		m_pitch = m_defaultPitch;
		m_fov = m_settings.fieldOfViewRadians;
		m_aspectRatio = m_settings.aspectRatio;
		m_pos = {87.0f * m_settings.mapXYFactor, 77.0f * m_settings.mapXYFactor, 10.0f};
		m_view.horizontalFieldOfView = m_fov;
		m_view.aspectRatio = m_aspectRatio;
		m_view.nearClip = m_settings.mapXYFactor;
		m_view.farClip = static_cast<float>(m_settings.terrainDrawWidth) * m_settings.mapXYFactor;
	}

	// ---- Host wiring ---------------------------------------------------------
	void SetTerrainHeight(TerrainHeightFunction terrainHeight) { m_terrainHeight = std::move(terrainHeight); ForceRedraw(); }
	void SetTerrainRayCast(TerrainRayCastFunction rayCast) { m_terrainRayCast = std::move(rayCast); }
	void SetMapExtent(std::optional<MapExtent> extent) { m_mapExtent = extent; m_cameraAreaConstraintsValid = false; }
	void SetTerrainDrawBounds(std::optional<TerrainDrawBounds> bounds) { m_terrainDrawBounds = bounds; m_recalcCamera = true; }
	void SetAspectRatio(float widthOverHeight) noexcept
	{
		m_aspectRatio = widthOverHeight;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	const CameraSettings &Settings() const noexcept { return m_settings; }
	float GroundHeight(float x, float y) const { return m_terrainHeight ? m_terrainHeight(x, y) : 0.0f; }

	// Legacy W3DView::reset.
	void Reset()
	{
		m_zoomLimited = true;
		m_isUserControlled = true;
		m_timeMultiplier = 1;
		StopDoingScriptedCamera();
		SetUserControlled(true);
		m_pos = {};
		SetAngleToDefault();
		SetPitchToDefault();
		SetZoomToDefault();
		m_guardBandBias = {};
		m_recalcCameraConstraintsAfterScrolling = false;
	}

	// ---- State ---------------------------------------------------------------
	const Math::Vector3 &Position() const noexcept { return m_pos; }
	// View::setPosition: raw pivot assignment (no side effects).
	void SetPosition(Math::Vector3 position) noexcept { m_pos = position; m_recalcCamera = true; }
	Math::Vector3 PreviousLookAt() const noexcept { return m_previousLookAtPosition; }
	float Angle() const noexcept { return m_angle; }
	float Pitch() const noexcept { return m_pitch; }
	float DefaultAngle() const noexcept { return m_defaultAngle; }
	float DefaultPitch() const noexcept { return m_defaultPitch; }
	float Zoom() const noexcept { return m_zoom; }
	float FxPitch() const noexcept { return m_FXPitch; }
	float HeightAboveGround() const noexcept { return m_heightAboveGround; }
	float MaxHeightAboveGround() const noexcept { return m_maxHeightAboveGround; }
	float MinHeightAboveGround() const noexcept { return m_minHeightAboveGround; }
	float CurrentHeightAboveGround() const noexcept { return m_currentHeightAboveGround; }
	float TerrainHeightAtPivot() const noexcept { return m_terrainHeightAtPivot; }
	float FieldOfView() const noexcept { return m_fov; }
	bool IsUserControlled() const noexcept { return m_isUserControlled; }
	bool IsZoomLimited() const noexcept { return m_zoomLimited; }
	void SetZoomLimited(bool limited) noexcept { m_zoomLimited = limited; }
	void SetOkToAdjustHeight(bool ok) noexcept { m_okToAdjustHeight = ok; }
	int TimeMultiplier() const noexcept { return m_timeMultiplier; }
	void SetTimeMultiplier(int multiple) noexcept { m_timeMultiplier = multiple; }
	void SetFieldOfView(float radians) noexcept
	{
		m_fov = radians;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}

	// W3DView::setAngle: user/script direct set; cancels scripted motion.
	void SetAngle(float radians)
	{
		SetAngleRaw(radians);
		StopDoingScriptedCamera();
		m_cameraArrivedAtWaypointOnPath = false;
		m_recalcCamera = true;
	}
	void SetPitch(float radians)
	{
		m_pitch = ClampPitch(radians);
		StopDoingScriptedCamera();
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	void SetDefaultPitch(float radians)
	{
		m_defaultPitch = ClampPitch(radians);
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	void SetAngleToDefault() { m_angle = m_defaultAngle; m_recalcCamera = true; }
	void SetPitchToDefault()
	{
		m_pitch = m_defaultPitch;
		m_FXPitch = 1.0f;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	// Script "set default camera": pitch (radians), angle ignored like retail, max height scale.
	void SetDefaultView(float pitch, float /*angle*/, float maxHeightScale)
	{
		SetDefaultPitch(pitch);
		m_maxHeightAboveGround = m_settings.maxCameraHeight * maxHeightScale;
		if (m_minHeightAboveGround > m_maxHeightAboveGround)
			m_maxHeightAboveGround = m_minHeightAboveGround;
	}
	void SetHeightAboveGround(float z)
	{
		m_heightAboveGround = m_zoomLimited ? LegacyClamp(m_minHeightAboveGround, z, m_maxHeightAboveGround) : z;
		StopDoingScriptedCamera();
		m_cameraArrivedAtWaypointOnPath = false;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	// View::zoom: move the desired height by `height`.
	void ZoomBy(float height) { SetHeightAboveGround(m_heightAboveGround + height); }
	// W3DView::setZoom: z is a fraction of the max height above ground.
	void SetZoom(float z)
	{
		m_heightAboveGround = m_maxHeightAboveGround * z;
		m_zoom = DesiredZoom(m_pos.x, m_pos.y);
		StopDoingScriptedCamera();
		m_cameraArrivedAtWaypointOnPath = false;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}
	void SetZoomToDefault()
	{
		m_heightAboveGround = m_maxHeightAboveGround;
		m_zoom = MaxZoom(m_pos.x, m_pos.y);
		StopDoingScriptedCamera();
		m_cameraArrivedAtWaypointOnPath = false;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}

	void SetUserControlled(bool value)
	{
		if (m_isUserControlled != value) {
			m_isUserControlled = value;
			m_zoom = DesiredZoom(m_pos.x, m_pos.y); // PRESERVE_RETAIL_SCRIPTED_CAMERA
		}
	}
	// View::doUserAction prologue: call before applying a user camera control
	// (the host enforces any user-control lock window itself).
	void BeginUserAction()
	{
		StopDoingScriptedCamera();
		SetUserControlled(true);
	}

	// W3DView::scrollBy with the screen->world conversion done by the host:
	// worldDelta is the XY pivot shift, screenDelta the raw scroll amount used by
	// the height-adjust rules. A zero screenDelta means "not scrolling".
	void ScrollBy(Math::Vector2 worldDelta, Math::Vector2 screenDelta)
	{
		if (screenDelta.x != 0 || screenDelta.y != 0) {
			m_scrollAmount = screenDelta;
			m_pos.x += worldDelta.x;
			m_pos.y += worldDelta.y;
			RemoveScriptedState(ScriptedRotate);
			m_recalcCamera = true;
		} else {
			m_scrollAmount = {};
		}
	}

	void ForceRedraw() noexcept
	{
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}

	// W3DView::lookAt: center the view on `o`. A target raised above the ground is
	// projected along the current view direction onto the terrain (needs a ray cast).
	void LookAt(Math::Vector3 o)
	{
		Math::Vector3 pos = o;
		if (m_terrainRayCast && o.z > m_settings.pathfindCellSize + GroundHeight(pos.x, pos.y)) {
			const Math::Vector3 cameraPosition = m_view.transform.Translation();
			const Math::Vector3 centerPoint = m_view.transform.Transform_Point({0.0f, 0.0f, -1.0f});
			Math::Vector3 rayEnd = (centerPoint - cameraPosition).Normalized_Legacy() * (m_view.farClip * 2.0f);
			const Math::Vector3 rayStart = pos;
			rayEnd = rayEnd + rayStart;
			if (const auto hit = m_terrainRayCast(rayStart, rayEnd)) {
				pos.x = hit->x;
				pos.y = hit->y;
			}
		}
		m_pos.x = pos.x;
		m_pos.y = pos.y;
		ResetPivotToGround();
		if (!m_isUserControlled)
			m_zoom = DesiredZoom(m_pos.x, m_pos.y);
		RemoveScriptedState(ScriptedRotate | ScriptedCameraLock | ScriptedMoveOnWaypointPath);
		m_cameraArrivedAtWaypointOnPath = false;
		m_recalcCamera = true;
	}

	// W3DView::initHeightForMap: call after the map loads and the camera was placed.
	void InitHeightForMap()
	{
		constexpr float MaxGroundLevel = 120.0f; // starting ground level can't exceed this height
		m_initialGroundLevel = std::min(MaxGroundLevel, GroundHeight(m_pos.x, m_pos.y));
		ResetPivotToGround();
	}

	void ResetPivotToGround()
	{
		m_pos.z = m_isUserControlled ? HeightAroundPos(m_pos.x, m_pos.y) : m_initialGroundLevel;
		m_cameraAreaConstraintsValid = false;
		m_recalcCamera = true;
	}

	// ---- Scripted camera -----------------------------------------------------
	bool IsDoingScriptedCamera() const noexcept { return m_scriptedState != 0; }
	void StopDoingScriptedCamera() noexcept { m_scriptedState = 0; }
	bool IsCameraMovementFinished() const noexcept
	{
		return !HasScriptedState(ScriptedRotate | ScriptedPitch | ScriptedZoom | ScriptedMoveOnWaypointPath);
	}
	// Polled by scripts; reading clears the flag (legacy behaviour).
	bool IsCameraMovementAtWaypointAlongPath() noexcept { return std::exchange(m_cameraArrivedAtWaypointOnPath, false); }
	bool IsTimeFrozen() const noexcept { return m_freezeTimeForCameraMovement; }
	bool IsRotating() const noexcept { return HasScriptedState(ScriptedRotate); }
	bool IsMovingAlongPath() const noexcept { return HasScriptedState(ScriptedMoveOnWaypointPath); }
	bool IsZooming() const noexcept { return HasScriptedState(ScriptedZoom); }
	bool IsPitching() const noexcept { return HasScriptedState(ScriptedPitch); }
	const WaypointPathMotion &PathMotion() const noexcept { return m_mcwpInfo; }

	// Script "move camera to". `shutter` is ignored, like retail.
	void MoveCameraTo(Math::Vector3 destination, int milliseconds, int /*shutter*/, bool orient, float easeIn, float easeOut)
	{
		m_mcwpInfo.waypoints[0] = m_pos;
		m_mcwpInfo.cameraAngle[0] = m_angle;
		m_mcwpInfo.waySegLength[0] = 0;
		m_mcwpInfo.waypoints[1] = m_pos;
		m_mcwpInfo.waySegLength[1] = 0;
		m_mcwpInfo.waypoints[2] = destination;
		m_mcwpInfo.waySegLength[2] = 0;
		m_mcwpInfo.numWaypoints = 2;
		if (milliseconds < 1)
			milliseconds = 1;
		m_mcwpInfo.totalTimeMilliseconds = static_cast<float>(milliseconds);
		m_mcwpInfo.shutter = 1;
		m_mcwpInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
		m_mcwpInfo.curSegment = 1;
		m_mcwpInfo.curSegDistance = 0;
		m_mcwpInfo.totalDistance = 0;

		SetupWaypointPath(orient);
		if (m_mcwpInfo.totalTimeMilliseconds == 1) {
			// do it instantly.
			MoveAlongWaypointPath(1);
			AddScriptedState(ScriptedMoveOnWaypointPath);
			m_cameraArrivedAtWaypointOnPath = false;
		}
	}

	// Script "move camera along waypoint path". `path` is the chain of waypoint
	// positions in link order (legacy follows each waypoint's first link).
	void MoveCameraAlongWaypointPath(std::span<const Math::Vector3> path, int milliseconds, int shutter, bool orient, float easeIn, float easeOut)
	{
		const float minDelta = m_settings.mapXYFactor;
		m_mcwpInfo.waypoints[0] = m_pos;
		m_mcwpInfo.cameraAngle[0] = m_angle;
		m_mcwpInfo.waySegLength[0] = 0;
		m_mcwpInfo.waypoints[1] = m_pos;
		m_mcwpInfo.numWaypoints = 1;
		if (milliseconds < 1)
			milliseconds = 1;
		m_mcwpInfo.totalTimeMilliseconds = static_cast<float>(milliseconds);
		m_mcwpInfo.shutter = static_cast<int>(shutter / m_settings.frameLengthMilliseconds);
		if (m_mcwpInfo.shutter < 1)
			m_mcwpInfo.shutter = 1;
		m_mcwpInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);

		std::size_t next = 0;
		while (next < path.size() && m_mcwpInfo.numWaypoints < MaxWaypoints) {
			m_mcwpInfo.numWaypoints++;
			const int n = m_mcwpInfo.numWaypoints;
			m_mcwpInfo.waypoints[n] = path[next];
			++next;
			const bool hasMore = next < path.size();
			const Math::Vector2 dir{m_mcwpInfo.waypoints[n].x - m_mcwpInfo.waypoints[n - 1].x,
				m_mcwpInfo.waypoints[n].y - m_mcwpInfo.waypoints[n - 1].y};
			if (dir.Length() < minDelta) {
				if (hasMore) {
					m_mcwpInfo.numWaypoints--; // drop this one.
				} else {
					m_mcwpInfo.waypoints[n - 1] = m_mcwpInfo.waypoints[n];
					m_mcwpInfo.numWaypoints--; // Push this one back.
				}
			}
		}
		SetupWaypointPath(orient);
	}

	// Script "reset camera": move to location, back to default angle, max zoom, FX pitch 1.
	void ResetCamera(Math::Vector3 location, int milliseconds, float easeIn, float easeOut)
	{
		MoveCameraTo(location, milliseconds, 0, false, easeIn, easeOut);
		m_mcwpInfo.cameraAngle[2] = 0.0f; // default angle.
		SetAngleRaw(m_mcwpInfo.cameraAngle[0]);
		ZoomCamera(MaxZoom(location.x, location.y), milliseconds, easeIn, easeOut);
		PitchCamera(1.0f, milliseconds, easeIn, easeOut);
	}

	// Script "rotate camera": `rotations` full turns about the pivot.
	void RotateCamera(float rotations, int milliseconds, float easeIn, float easeOut)
	{
		m_rcInfo.numHoldFrames = 0;
		m_rcInfo.trackObject = false;
		m_rcInfo.target = {};
		if (milliseconds < 1)
			milliseconds = 1;
		m_rcInfo.numFrames = std::max(1, static_cast<int>(milliseconds / m_settings.frameLengthMilliseconds));
		m_rcInfo.curFrame = 0;
		AddScriptedState(ScriptedRotate);
		m_rcInfo.startAngle = m_angle;
		m_rcInfo.endAngle = m_angle + 2 * Math::Pi * rotations;
		m_rcInfo.startTimeMultiplier = m_timeMultiplier;
		m_rcInfo.endTimeMultiplier = m_timeMultiplier;
		m_rcInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
		RemoveScriptedState(ScriptedMoveOnWaypointPath);
		m_cameraArrivedAtWaypointOnPath = false;
	}

	// Script "rotate camera toward unit": track a moving target, then hold on it.
	// (Legacy rotateCameraTowardObject; the object lookup is the provider.)
	void RotateCameraTowardTarget(TargetPositionProvider target, int milliseconds, int holdMilliseconds, float easeIn, float easeOut)
	{
		m_rcInfo.trackObject = true;
		if (holdMilliseconds < 1)
			holdMilliseconds = 0;
		m_rcInfo.numHoldFrames = std::max(0, static_cast<int>(holdMilliseconds / m_settings.frameLengthMilliseconds));
		if (milliseconds < 1)
			milliseconds = 1;
		m_rcInfo.numFrames = std::max(1, static_cast<int>(milliseconds / m_settings.frameLengthMilliseconds));
		m_rcInfo.curFrame = 0;
		AddScriptedState(ScriptedRotate);
		m_rcInfo.target = std::move(target);
		// Legacy left this uninitialised (union); the pivot means "no rotation" until seen.
		m_rcInfo.targetPosition = m_pos;
		m_rcInfo.startTimeMultiplier = m_timeMultiplier;
		m_rcInfo.endTimeMultiplier = m_timeMultiplier;
		m_rcInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
		RemoveScriptedState(ScriptedMoveOnWaypointPath);
		m_cameraArrivedAtWaypointOnPath = false;
	}

	// Script "rotate camera toward position" (Zero Hour semantics: absolute end angle).
	void RotateCameraTowardPosition(Math::Vector3 location, int milliseconds, float easeIn, float easeOut, bool reverseRotation)
	{
		m_rcInfo.numHoldFrames = 0;
		m_rcInfo.trackObject = false;
		m_rcInfo.target = {};
		if (milliseconds < 1)
			milliseconds = 1;
		m_rcInfo.numFrames = std::max(1, static_cast<int>(milliseconds / m_settings.frameLengthMilliseconds));
		const auto yaw = YawToward({location.x - m_pos.x, location.y - m_pos.y});
		if (!yaw)
			return;
		float angle = *yaw;
		if (reverseRotation) {
			if (m_angle < angle)
				angle -= 2.0f * Math::Pi;
			else
				angle += 2.0f * Math::Pi;
		}
		m_rcInfo.curFrame = 0;
		AddScriptedState(ScriptedRotate);
		m_rcInfo.startAngle = m_angle;
		m_rcInfo.endAngle = angle;
		m_rcInfo.startTimeMultiplier = m_timeMultiplier;
		m_rcInfo.endTimeMultiplier = m_timeMultiplier;
		m_rcInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
		RemoveScriptedState(ScriptedMoveOnWaypointPath);
		m_cameraArrivedAtWaypointOnPath = false;
	}

	// Script "zoom camera": `finalZoom` is the raw zoom factor.
	void ZoomCamera(float finalZoom, int milliseconds, float easeIn, float easeOut)
	{
		if (milliseconds < 1)
			milliseconds = 1;
		m_zcInfo.numFrames = std::max(1, static_cast<int>(milliseconds / m_settings.frameLengthMilliseconds));
		m_zcInfo.curFrame = 0;
		AddScriptedState(ScriptedZoom);
		m_zcInfo.startZoom = m_zoom;
		m_zcInfo.endZoom = finalZoom;
		m_zcInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
	}

	// Script "pitch camera": animates the FX pitch (1 = normal, <1 flatter look, >1 steeper).
	void PitchCamera(float finalPitch, int milliseconds, float easeIn, float easeOut)
	{
		if (milliseconds < 1)
			milliseconds = 1;
		m_pcInfo.numFrames = std::max(1, static_cast<int>(milliseconds / m_settings.frameLengthMilliseconds));
		m_pcInfo.curFrame = 0;
		AddScriptedState(ScriptedPitch);
		m_pcInfo.startPitch = m_FXPitch;
		m_pcInfo.endPitch = finalPitch;
		m_pcInfo.ease.SetEaseTimes(easeIn / milliseconds, easeOut / milliseconds);
	}

	void CameraModFreezeTime() noexcept { m_freezeTimeForCameraMovement = true; }

	void CameraModFreezeAngle()
	{
		if (HasScriptedState(ScriptedRotate)) {
			if (m_rcInfo.trackObject)
				m_rcInfo.target = {}; // legacy: target id cleared, last position kept
			else
				m_rcInfo.startAngle = m_rcInfo.endAngle = m_angle; // Silly, but consistent.
		}
		if (HasScriptedState(ScriptedMoveOnWaypointPath)) {
			for (int i = 0; i < m_mcwpInfo.numWaypoints; i++)
				m_mcwpInfo.cameraAngle[i + 1] = m_mcwpInfo.cameraAngle[0];
		}
	}

	// `finalZoom` is a fraction of the max zoom at the end position; ease values are fractions.
	void CameraModFinalZoom(float finalZoom, float easeIn, float easeOut)
	{
		if (HasScriptedState(ScriptedRotate)) {
			const float time = (m_rcInfo.numFrames + m_rcInfo.numHoldFrames - m_rcInfo.curFrame) * m_settings.frameLengthMilliseconds;
			ZoomCamera(finalZoom * MaxZoom(m_pos.x, m_pos.y), static_cast<int>(time), time * easeIn, time * easeOut);
		}
		if (HasScriptedState(ScriptedMoveOnWaypointPath)) {
			const Math::Vector3 pos = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
			const float time = m_mcwpInfo.totalTimeMilliseconds - m_mcwpInfo.elapsedTimeMilliseconds;
			ZoomCamera(finalZoom * MaxZoom(pos.x, pos.y), static_cast<int>(time), time * easeIn, time * easeOut);
		}
	}

	void CameraModFinalPitch(float finalPitch, float easeIn, float easeOut)
	{
		if (HasScriptedState(ScriptedRotate)) {
			const float time = (m_rcInfo.numFrames + m_rcInfo.numHoldFrames - m_rcInfo.curFrame) * m_settings.frameLengthMilliseconds;
			PitchCamera(finalPitch, static_cast<int>(time), time * easeIn, time * easeOut);
		}
		if (HasScriptedState(ScriptedMoveOnWaypointPath)) {
			const float time = m_mcwpInfo.totalTimeMilliseconds - m_mcwpInfo.elapsedTimeMilliseconds;
			PitchCamera(finalPitch, static_cast<int>(time), time * easeIn, time * easeOut);
		}
	}

	void CameraModRollingAverage(int framesToAverage) noexcept
	{
		if (framesToAverage < 1)
			framesToAverage = 1;
		m_mcwpInfo.rollingAverageFrames = framesToAverage;
	}

	void CameraModFinalTimeMultiplier(int finalMultiplier)
	{
		if (HasScriptedState(ScriptedZoom))
			m_zcInfo.endTimeMultiplier = finalMultiplier;
		if (HasScriptedState(ScriptedPitch))
			m_pcInfo.endTimeMultiplier = finalMultiplier;
		if (HasScriptedState(ScriptedRotate)) {
			m_rcInfo.endTimeMultiplier = finalMultiplier;
		} else if (HasScriptedState(ScriptedMoveOnWaypointPath)) {
			float curDistance = 0;
			for (int i = 0; i < m_mcwpInfo.numWaypoints; i++) {
				curDistance += m_mcwpInfo.waySegLength[i];
				const float factor2 = curDistance / m_mcwpInfo.totalDistance;
				const float factor1 = static_cast<float>(1.0 - factor2);
				m_mcwpInfo.timeMultiplier[i + 1] = FloorToInt(0.5 + m_mcwpInfo.timeMultiplier[i + 1] * factor1 + finalMultiplier * factor2);
			}
		} else {
			// If we aren't doing a camera movement, just set the time.
			m_timeMultiplier = finalMultiplier;
		}
	}

	// Orient the camera toward `location` at every waypoint of the current path.
	void CameraModLookToward(Math::Vector3 location)
	{
		if (HasScriptedState(ScriptedRotate))
			return; // Doesn't apply to rotate about a point.
		if (!HasScriptedState(ScriptedMoveOnWaypointPath))
			return;
		for (int i = 2; i <= m_mcwpInfo.numWaypoints; i++) {
			const Math::Vector2 result = SmoothedCornerPoint(
				m_mcwpInfo.waypoints[i - 1], m_mcwpInfo.waypoints[i], m_mcwpInfo.waypoints[i + 1], 0.5f);
			if (const auto yaw = YawToward({location.x - result.x, location.y - result.y}))
				m_mcwpInfo.cameraAngle[i] = *yaw;
		}
		if (m_mcwpInfo.totalTimeMilliseconds == 1) {
			// do it instantly.
			MoveAlongWaypointPath(1);
			AddScriptedState(ScriptedMoveOnWaypointPath);
			m_cameraArrivedAtWaypointOnPath = false;
		}
	}

	// Orient the camera toward `location` by the end of the current path.
	void CameraModFinalLookToward(Math::Vector3 location)
	{
		if (HasScriptedState(ScriptedRotate))
			return;
		if (!HasScriptedState(ScriptedMoveOnWaypointPath))
			return;
		const int min = std::max(2, m_mcwpInfo.numWaypoints - 1);
		for (int i = min; i <= m_mcwpInfo.numWaypoints; i++) {
			const Math::Vector2 result = SmoothedCornerPoint(
				m_mcwpInfo.waypoints[i - 1], m_mcwpInfo.waypoints[i], m_mcwpInfo.waypoints[i + 1], 0.5f);
			const auto yaw = YawToward({location.x - result.x, location.y - result.y});
			if (!yaw)
				continue;
			float angle = *yaw;
			if (i == m_mcwpInfo.numWaypoints) {
				m_mcwpInfo.cameraAngle[i] = angle;
			} else {
				const float deltaAngle = NormalizeAngle(angle - m_mcwpInfo.cameraAngle[i]);
				angle = NormalizeAngle(m_mcwpInfo.cameraAngle[i] + deltaAngle / 2);
				m_mcwpInfo.cameraAngle[i] = angle;
			}
		}
	}

	// Shift the whole remaining path so it ends at `location`.
	void CameraModFinalMoveTo(Math::Vector3 location)
	{
		if (HasScriptedState(ScriptedRotate))
			return;
		if (!HasScriptedState(ScriptedMoveOnWaypointPath))
			return;
		const Math::Vector3 start = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
		const float dx = location.x - start.x;
		const float dy = location.y - start.y;
		for (int i = 2; i <= m_mcwpInfo.numWaypoints; i++) {
			m_mcwpInfo.waypoints[i].x += dx;
			m_mcwpInfo.waypoints[i].y += dy;
		}
	}

	// ---- Camera lock (script "camera follow unit") ---------------------------
	// An empty provider clears the lock (legacy setCameraLock(INVALID_ID)).
	void SetCameraLock(LockTargetProvider target)
	{
		if (m_settings.disableCameraMovement && target)
			return;
		m_lockTarget = std::move(target);
		m_lockDist = 0.0f;
		m_lockType = CameraLockMode::Follow;
		RemoveScriptedState(ScriptedCameraLock);
	}
	bool HasCameraLock() const noexcept { return static_cast<bool>(m_lockTarget); }
	void SetSnapMode(CameraLockMode lockType, float lockDist)
	{
		m_lockType = lockType;
		m_lockDist = lockDist;
		AddScriptedState(ScriptedCameraLock);
	}
	void SnapToCameraLock() noexcept { m_snapImmediate = true; }

	// ---- Camera shake (legacy View::shake spring; the random angle is injected) ----
	// `intensity` is the GameData Shake*Intensity for the shake type.
	void Shake(Math::Vector3 epicenter, float intensity, float angleRadians)
	{
		m_shakeAngleCos = std::cos(angleRadians);
		m_shakeAngleSin = std::sin(angleRadians);
		const float dx = epicenter.x - m_pos.x;
		const float dy = epicenter.y - m_pos.y;
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist > m_settings.maxShakeRange)
			return;
		intensity *= 1.0f - (dist / m_settings.maxShakeRange);
		m_shakeIntensity += intensity;
		constexpr float maxIntensity = 3.0f;
		if (m_shakeIntensity > m_settings.maxShakeIntensity)
			m_shakeIntensity = maxIntensity;
	}

	// Legacy W3DView::stepView: once per fixed logic step.
	void StepFixed()
	{
		if (m_shakeIntensity > 0.01f) {
			m_shakeOffset = {m_shakeIntensity * m_shakeAngleCos, m_shakeIntensity * m_shakeAngleSin};
			constexpr float dampingCoeff = 0.75f;
			m_shakeIntensity *= dampingCoeff;
			m_shakeAngleCos = -m_shakeAngleCos;
			m_shakeAngleSin = -m_shakeAngleSin;
		} else {
			m_shakeIntensity = 0.0f;
			m_shakeOffset = {};
		}
	}

	// ---- Per render frame ----------------------------------------------------
	// Legacy W3DView::update minus drawing: camera lock, scripted motion,
	// height-above-ground settling, area constraints, transform rebuild.
	void Update(const CameraFrameTiming &timing = {})
	{
		bool didScriptedMovement = false;

		if (!m_lockTarget)
			m_followFactor = -1;
		if (m_lockTarget) {
			RemoveScriptedState(ScriptedMoveOnWaypointPath);
			m_cameraArrivedAtWaypointOnPath = false;
			const std::optional<LockTargetState> locked = m_lockTarget();
			if (!locked) {
				SetCameraLock({});
				m_followFactor = -1;
			} else {
				UpdateCameraLock(*locked, timing);
				didScriptedMovement = true;
				m_recalcCamera = true;
			}
		}

		if (timing.advanceScriptedMotion) {
			if (UpdateCameraMovements(timing)) {
				didScriptedMovement = true;
				m_recalcCamera = true;
			}
		} else if (IsDoingScriptedCamera()) {
			didScriptedMovement = true; // don't mess up the scripted movement
		}

		if (!m_isUserControlled)
			didScriptedMovement = true;
		if (m_shakeIntensity > 0.01f)
			m_recalcCamera = true;

		m_terrainHeightAtPivot = HeightAroundPos(m_pos.x, m_pos.y);
		m_currentHeightAboveGround = CameraOffsetZ() * m_zoom - m_terrainHeightAtPivot;

		if (m_okToAdjustHeight) {
			if (didScriptedMovement)
				m_heightAboveGround = m_currentHeightAboveGround;

			const float scrollLenSqr = m_scrollAmount.Dot(m_scrollAmount);
			const bool isScrolling = scrollLenSqr > std::numeric_limits<float>::epsilon();
			const bool isScrollingTooFast = scrollLenSqr >= m_settings.scrollAmountCutoff * m_settings.scrollAmountCutoff;
			const bool isWithinHeightConstraints = IsWithinCameraHeightConstraints();
			const bool adjustZoomWhenScrolling = isScrolling && (!isScrollingTooFast || !isWithinHeightConstraints);
			const bool adjustZoomWhenNotScrolling = !isScrolling && !didScriptedMovement;

			if (adjustZoomWhenScrolling || adjustZoomWhenNotScrolling) {
				bool isZoomingOrMovingPivot = false;
				if (ZoomCameraToDesiredHeight(timing))
					isZoomingOrMovingPivot = true;
				if (MovePivotToGround(timing))
					isZoomingOrMovingPivot = true;
				if (isZoomingOrMovingPivot) {
					m_recalcCamera = true;
					if (isScrolling)
						m_recalcCameraConstraintsAfterScrolling = true;
					else
						m_cameraAreaConstraintsValid = false;
				}
			}
			if (m_recalcCameraConstraintsAfterScrolling && !isScrolling) {
				m_recalcCameraConstraintsAfterScrolling = false;
				m_cameraAreaConstraintsValid = false;
			}
		}

		if (!didScriptedMovement)
			UpdateCameraAreaConstraints();

		if (m_recalcCamera) {
			UpdateCameraTransform();
			m_recalcCamera = false;
		}
	}

	// Last camera built by Update.
	const CameraView &View() const noexcept { return m_view; }

	// Legacy buildCameraPosition for the current state (no terrain clip, no cache).
	EyeAndTarget BuildCameraPosition() const
	{
		Math::Vector3 pos = m_pos;
		pos.x += m_shakeOffset.x;
		pos.y += m_shakeOffset.y;

		// The default pitch affects the look-at distance to the target (legacy math).
		Math::Vector3 sourcePos;
		sourcePos.z = CameraOffsetZ();
		sourcePos.y = -(sourcePos.z / std::tan(ViewDefaultPitchRadians));
		sourcePos.x = -(sourcePos.y * std::tan(ViewDefaultYawRadians));
		sourcePos.x *= m_zoom;
		sourcePos.y *= m_zoom;
		sourcePos.z *= m_zoom;

		// Scale later to achieve the intended camera height. Must not scale before pitching.
		const float heightScale = 1.0f - (pos.z / sourcePos.z);
		const auto angleTransform = Math::AffineTransform3::From_Axis_Angle_Legacy({0.0f, 0.0f, 1.0f}, m_angle - ViewDefaultYawRadians);
		const auto pitchTransform = Math::AffineTransform3::From_Axis_Angle_Legacy({-1.0f, 0.0f, 0.0f}, m_pitch - ViewDefaultPitchRadians);
		sourcePos = pitchTransform.Transform_Vector(sourcePos);
		sourcePos = angleTransform.Transform_Vector(sourcePos);
		sourcePos = sourcePos * heightScale;

		Math::Vector3 targetPos = pos;
		sourcePos = sourcePos + targetPos;

		// Zero Hour FX pitch rule.
		if (m_FXPitch <= 1.0f) {
			targetPos.z = sourcePos.z - ((sourcePos.z - targetPos.z) * m_FXPitch);
		} else {
			sourcePos.x = targetPos.x + ((sourcePos.x - targetPos.x) / m_FXPitch);
			sourcePos.y = targetPos.y + ((sourcePos.y - targetPos.y) / m_FXPitch);
		}
		return {sourcePos, targetPos};
	}

	// Legacy desired / max zoom for a pivot XY (zoom that yields the desired / max height).
	float DesiredZoom(float x, float y) const { return DesiredHeight(x, y) / CameraOffsetZ(); }
	float MaxZoom(float x, float y) const { return MaxHeight(x, y) / CameraOffsetZ(); }

	// Legacy getHeightAroundPos: 5-point average while user controlled, 5-point max
	// for the scripted camera (PRESERVE_RETAIL_SCRIPTED_CAMERA).
	float HeightAroundPos(float x, float y) const { return HeightAroundPos(x, y, m_settings.mapXYFactor * 4); }
	float HeightAroundPos(float x, float y, float sample) const
	{
		float terrainHeight = GroundHeight(x, y);
		if (m_isUserControlled) {
			terrainHeight += GroundHeight(x + sample, y - sample);
			terrainHeight += GroundHeight(x - sample, y - sample);
			terrainHeight += GroundHeight(x + sample, y + sample);
			terrainHeight += GroundHeight(x - sample, y + sample);
			terrainHeight /= 5;
		} else {
			terrainHeight = std::max(terrainHeight, GroundHeight(x + sample, y - sample));
			terrainHeight = std::max(terrainHeight, GroundHeight(x - sample, y - sample));
			terrainHeight = std::max(terrainHeight, GroundHeight(x + sample, y + sample));
			terrainHeight = std::max(terrainHeight, GroundHeight(x - sample, y + sample));
		}
		return terrainHeight;
	}

	std::optional<MapExtent> CameraAreaConstraints() const
	{
		if (!m_cameraAreaConstraintsValid)
			return std::nullopt;
		return m_cameraAreaConstraints;
	}

private:
	enum ScriptedFlags : std::uint32_t
	{
		ScriptedRotate = 1u << 0,
		ScriptedPitch = 1u << 1,
		ScriptedZoom = 1u << 2,
		ScriptedCameraLock = 1u << 3,
		ScriptedMoveOnWaypointPath = 1u << 4,
	};

	bool HasScriptedState(std::uint32_t state) const noexcept { return (m_scriptedState & state) != 0; }
	void AddScriptedState(std::uint32_t state)
	{
		m_scriptedState |= state;
		SetUserControlled(false);
	}
	void RemoveScriptedState(std::uint32_t state) noexcept { m_scriptedState &= ~state; }

	// View::setAngle: normalize only.
	void SetAngleRaw(float radians) noexcept { m_angle = NormalizeAngle(radians); }
	static float ClampPitch(float radians) noexcept
	{
		return LegacyClamp(LegacyDegreesToRadians(0.1f), radians, LegacyDegreesToRadians(89.9f));
	}

	float CameraOffsetZ() const
	{
		if (!m_isUserControlled)
			return m_initialGroundLevel + m_settings.cameraHeight;
		return m_pos.z + m_settings.maxCameraHeight;
	}
	float DesiredHeight(float x, float y) const
	{
		if (!m_isUserControlled)
			return HeightAroundPos(x, y) + m_heightAboveGround;
		return m_pos.z + m_heightAboveGround;
	}
	float MaxHeight(float x, float y) const
	{
		if (!m_isUserControlled)
			return HeightAroundPos(x, y) + m_maxHeightAboveGround;
		return m_pos.z + m_maxHeightAboveGround;
	}

	bool IsWithinCameraHeightConstraints() const noexcept
	{
		const bool isAboveMinHeight = m_currentHeightAboveGround >= m_minHeightAboveGround;
		const bool isBelowMaxHeight = m_currentHeightAboveGround <= m_maxHeightAboveGround;
		return isAboveMinHeight && (isBelowMaxHeight || !m_settings.enforceMaxCameraHeight);
	}

	void UpdateCameraLock(const LockTargetState &locked, const CameraFrameTiming &timing)
	{
		if (m_followFactor < 0) {
			m_followFactor = 0.05f;
		} else {
			m_followFactor += 0.05f;
			if (m_followFactor > 1.0f)
				m_followFactor = 1.0f;
		}
		const Math::Vector3 objpos = locked.position;
		Math::Vector3 curpos = m_pos;
		const float snapThreshSqr = m_settings.partitionCellSize * m_settings.partitionCellSize;
		const float curDistSqr = (curpos.x - objpos.x) * (curpos.x - objpos.x) + (curpos.y - objpos.y) * (curpos.y - objpos.y);
		if (m_snapImmediate) {
			curpos.x = objpos.x;
			curpos.y = objpos.y;
		} else {
			const float dx = objpos.x - curpos.x;
			const float dy = objpos.y - curpos.y;
			if (m_lockType == CameraLockMode::Tether) {
				if (curDistSqr >= snapThreshSqr) {
					const float ratio = 1.0f - snapThreshSqr / curDistSqr;
					curpos.x += dx * ratio * 0.5f;
					curpos.y += dy * ratio * 0.5f;
				} else {
					const float ratio = 0.01f * m_lockDist;
					curpos.x += dx * ratio;
					curpos.y += dy * ratio;
				}
			} else {
				curpos.x += dx * m_followFactor;
				curpos.y += dy * m_followFactor;
			}
		}
		if (timing.gameTimeAdvancing)
			m_previousLookAtPosition = m_pos;
		m_pos = curpos;

		if (m_lockType == CameraLockMode::Follow && locked.airborneOrientation) {
			float idealZRot = static_cast<float>(*locked.airborneOrientation - std::numbers::pi / 2);
			if (m_snapImmediate) {
				SetAngleRaw(idealZRot);
			} else {
				idealZRot = NormalizeAngle(idealZRot);
				const float oldZRot = NormalizeAngle(m_angle);
				const float diffRot = NormalizeAngle(idealZRot - oldZRot);
				SetAngleRaw(m_angle + diffRot * 0.1f);
			}
		}
		m_snapImmediate = false;
		m_pos.z = objpos.z;
	}

	bool UpdateCameraMovements(const CameraFrameTiming &timing)
	{
		m_frameAdvance = timing.deltaMilliseconds / m_settings.frameLengthMilliseconds;
		bool didUpdate = false;
		if (HasScriptedState(ScriptedZoom)) {
			ZoomCameraOneFrame();
			didUpdate = true;
		}
		if (HasScriptedState(ScriptedPitch)) {
			PitchCameraOneFrame();
			didUpdate = true;
		}
		if (HasScriptedState(ScriptedRotate)) {
			m_previousLookAtPosition = m_pos;
			RotateCameraOneFrame();
			didUpdate = true;
		} else if (HasScriptedState(ScriptedMoveOnWaypointPath)) {
			m_previousLookAtPosition = m_pos;
			MoveAlongWaypointPath(timing.deltaMilliseconds);
			didUpdate = true;
		}
		if (HasScriptedState(ScriptedCameraLock))
			didUpdate = true;
		return didUpdate;
	}

	void SetupWaypointPath(bool orient)
	{
		auto &info = m_mcwpInfo;
		info.curSegment = 1;
		info.curSegDistance = 0;
		info.totalDistance = 0;
		info.rollingAverageFrames = 1;
		const int n = info.numWaypoints;
		float angle = m_angle;
		for (int i = 1; i < n; i++) {
			const Math::Vector2 dir{info.waypoints[i + 1].x - info.waypoints[i].x, info.waypoints[i + 1].y - info.waypoints[i].y};
			const float dirLength = dir.Length();
			info.waySegLength[i] = dirLength;
			info.totalDistance += info.waySegLength[i];
			if (orient) {
				if (const auto yaw = YawToward(dir))
					angle = *yaw;
			}
			info.cameraAngle[i] = angle;
		}
		info.cameraAngle[1] = m_angle;
		info.cameraAngle[n] = info.cameraAngle[n - 1];
		for (int i = n - 1; i > 1; i--)
			info.cameraAngle[i] = (info.cameraAngle[i] + info.cameraAngle[i - 1]) / 2;
		info.waySegLength[n + 1] = info.waySegLength[n];

		// Prevent a possible divide by zero.
		if (info.totalDistance < 1.0) {
			info.waySegLength[n - 1] += static_cast<float>(1.0 - info.totalDistance);
			info.totalDistance = 1.0;
		}

		float curDistance = 0;
		const Math::Vector3 finalPos = info.waypoints[n];
		const float newGround = GroundHeight(finalPos.x, finalPos.y);
		for (int i = 0; i <= n + 1; i++) {
			const float factor2 = curDistance / info.totalDistance;
			const float factor1 = static_cast<float>(1.0 - factor2);
			info.timeMultiplier[i] = m_timeMultiplier;
			info.waypoints[i].z = m_pos.z * factor1 + newGround * factor2;
			curDistance += info.waySegLength[i];
		}

		// Pad the end.
		info.waypoints[n + 1] = info.waypoints[n];
		Math::Vector3 cur = info.waypoints[n];
		Math::Vector3 prev = info.waypoints[n - 1];
		info.waypoints[n + 1].x += cur.x - prev.x;
		info.waypoints[n + 1].y += cur.y - prev.y;
		info.waypoints[n + 1].z = newGround;
		info.cameraAngle[n + 1] = info.cameraAngle[n];

		cur = info.waypoints[2];
		prev = info.waypoints[1];
		info.waypoints[0].x -= cur.x - prev.x;
		info.waypoints[0].y -= cur.y - prev.y;

		if (n > 1)
			AddScriptedState(ScriptedMoveOnWaypointPath);
		else
			RemoveScriptedState(ScriptedMoveOnWaypointPath);

		m_cameraArrivedAtWaypointOnPath = false;
		RemoveScriptedState(ScriptedRotate);
		info.elapsedTimeMilliseconds = 0;
		info.curShutter = info.shutter;
	}

	void ExpandConstraintsToInclude(Math::Vector3 pos)
	{
		// Assuming the scripter knows what he is doing, the constraints grow so
		// the scripted action can occur.
		m_cameraAreaConstraints.lo.x = std::min(m_cameraAreaConstraints.lo.x, pos.x);
		m_cameraAreaConstraints.hi.x = std::max(m_cameraAreaConstraints.hi.x, pos.x);
		m_cameraAreaConstraints.lo.y = std::min(m_cameraAreaConstraints.lo.y, pos.y);
		m_cameraAreaConstraints.hi.y = std::max(m_cameraAreaConstraints.hi.y, pos.y);
	}

	void MoveAlongWaypointPath(float milliseconds)
	{
		auto &info = m_mcwpInfo;
		info.elapsedTimeMilliseconds += milliseconds;
		if (m_settings.disableCameraMovement) {
			if (info.elapsedTimeMilliseconds > info.totalTimeMilliseconds) {
				RemoveScriptedState(ScriptedMoveOnWaypointPath);
				m_freezeTimeForCameraMovement = false;
			}
			return;
		}
		if (info.elapsedTimeMilliseconds > info.totalTimeMilliseconds) {
			RemoveScriptedState(ScriptedMoveOnWaypointPath);
			m_cameraArrivedAtWaypointOnPath = false;
			m_freezeTimeForCameraMovement = false;
			SetAngleRaw(info.cameraAngle[info.numWaypoints]);
			const Math::Vector3 pos = info.waypoints[info.numWaypoints];
			m_pos = pos;
			ExpandConstraintsToInclude(pos);
			return;
		}

		const float totalTime = info.totalTimeMilliseconds;
		const float deltaTime = info.ease(info.elapsedTimeMilliseconds / totalTime)
			- info.ease((info.elapsedTimeMilliseconds - milliseconds) / totalTime);
		info.curSegDistance += deltaTime * info.totalDistance;
		// Zero Hour condition (>=); Generals used > here.
		while (info.curSegDistance >= info.waySegLength[info.curSegment]) {
			if (HasScriptedState(ScriptedMoveOnWaypointPath))
				m_cameraArrivedAtWaypointOnPath = true;
			info.curSegDistance -= info.waySegLength[info.curSegment];
			info.curSegment++;
			if (info.curSegment >= info.numWaypoints) {
				info.totalTimeMilliseconds = 0; // Will end following next frame.
				return;
			}
		}
		float avgFactor = static_cast<float>(1.0 / info.rollingAverageFrames);
		info.curShutter--;
		if (info.curShutter > 0)
			return;
		info.curShutter = info.shutter;
		const int seg = info.curSegment;
		float factor = info.curSegDistance / info.waySegLength[seg];
		if (seg == info.numWaypoints - 1)
			avgFactor = static_cast<float>(avgFactor + (1.0 - avgFactor) * factor);
		const float factor1 = static_cast<float>(1.0 - factor);
		const float factor2 = static_cast<float>(1.0 - factor1);
		float angle1 = info.cameraAngle[seg];
		const float angle2 = info.cameraAngle[seg + 1];
		if (angle2 - angle1 > Math::Pi)
			angle1 += 2 * Math::Pi;
		if (angle2 - angle1 < -Math::Pi)
			angle1 -= 2 * Math::Pi;
		const float angle = NormalizeAngle(angle1 * factor1 + angle2 * factor2);
		const float deltaAngle = NormalizeAngle(angle - m_angle);
		SetAngleRaw(m_angle + (avgFactor * deltaAngle));

		const float timeMultiplier = info.timeMultiplier[seg] * factor1 + info.timeMultiplier[seg + 1] * factor2;
		m_timeMultiplier = FloorToInt(0.5 + timeMultiplier);

		Math::Vector2 result;
		if (factor < 0.5) {
			result = SmoothedCornerPoint(info.waypoints[seg - 1], info.waypoints[seg], info.waypoints[seg + 1], factor + 0.5f);
		} else {
			result = SmoothedCornerPoint(info.waypoints[seg], info.waypoints[seg + 1], info.waypoints[seg + 2], factor - 0.5f);
		}
		const Math::Vector3 position{result.x, result.y,
			info.waypoints[seg].z * factor1 + info.waypoints[seg + 1].z * factor2};
		m_pos = position;
		ExpandConstraintsToInclude(position);
	}

	void RotateCameraOneFrame()
	{
		auto &info = m_rcInfo;
		info.curFrame = std::min(info.curFrame + m_frameAdvance, static_cast<float>(info.numFrames + info.numHoldFrames));
		if (m_settings.disableCameraMovement) {
			if (info.curFrame >= info.numFrames + info.numHoldFrames) {
				RemoveScriptedState(ScriptedRotate);
				m_freezeTimeForCameraMovement = false;
			}
			return;
		}
		if (info.trackObject) {
			if (info.curFrame <= info.numFrames + info.numHoldFrames) {
				if (info.target) {
					if (const auto seen = info.target())
						info.targetPosition = *seen;
				}
				if (const auto angle = YawToward({info.targetPosition.x - m_pos.x, info.targetPosition.y - m_pos.y})) {
					if (info.curFrame <= info.numFrames) {
						const float factor = info.ease(static_cast<float>(info.curFrame) / info.numFrames);
						float angleDiff = NormalizeAngle(*angle - m_angle);
						angleDiff *= factor;
						SetAngleRaw(m_angle + angleDiff);
						m_timeMultiplier = info.startTimeMultiplier + FloorToInt(0.5 + (info.endTimeMultiplier - info.startTimeMultiplier) * factor);
					} else {
						SetAngleRaw(*angle);
					}
				}
			}
		} else if (info.curFrame <= info.numFrames) {
			const float factor = info.ease(static_cast<float>(info.curFrame) / info.numFrames);
			SetAngleRaw(Math::Lerp(info.startAngle, info.endAngle, factor));
			m_timeMultiplier = info.startTimeMultiplier + FloorToInt(0.5 + (info.endTimeMultiplier - info.startTimeMultiplier) * factor);
		}

		if (info.curFrame >= info.numFrames + info.numHoldFrames) {
			RemoveScriptedState(ScriptedRotate);
			m_freezeTimeForCameraMovement = false;
			if (!info.trackObject)
				SetAngleRaw(info.endAngle);
		}
	}

	void ZoomCameraOneFrame()
	{
		auto &info = m_zcInfo;
		info.curFrame = std::min(info.curFrame + m_frameAdvance, static_cast<float>(info.numFrames));
		if (m_settings.disableCameraMovement) {
			if (info.curFrame >= info.numFrames)
				RemoveScriptedState(ScriptedZoom);
			return;
		}
		if (info.curFrame <= info.numFrames) {
			const float factor = info.ease(static_cast<float>(info.curFrame) / info.numFrames);
			m_zoom = Math::Lerp(info.startZoom, info.endZoom, factor);
		}
		if (info.curFrame >= info.numFrames) {
			RemoveScriptedState(ScriptedZoom);
			m_zoom = info.endZoom;
		}
	}

	void PitchCameraOneFrame()
	{
		auto &info = m_pcInfo;
		info.curFrame = std::min(info.curFrame + m_frameAdvance, static_cast<float>(info.numFrames));
		if (m_settings.disableCameraMovement) {
			if (info.curFrame >= info.numFrames)
				RemoveScriptedState(ScriptedPitch);
			return;
		}
		if (info.curFrame <= info.numFrames) {
			const float factor = info.ease(static_cast<float>(info.curFrame) / info.numFrames);
			m_FXPitch = Math::Lerp(info.startPitch, info.endPitch, factor);
		}
		if (info.curFrame >= info.numFrames) {
			RemoveScriptedState(ScriptedPitch);
			m_FXPitch = info.endPitch;
		}
	}

	// CameraAdjustSpeed of the remaining gap per legacy frame, for any frame length.
	float AdjustFactor(const CameraFrameTiming &timing) const
	{
		const float speed = std::clamp(m_settings.cameraAdjustSpeed, 0.0f, 1.0f);
		const float frames = timing.deltaMilliseconds / m_settings.frameLengthMilliseconds;
		return 1.0f - std::pow(1.0f - speed, frames);
	}

	bool ZoomCameraToDesiredHeight(const CameraFrameTiming &timing)
	{
		const float desiredZoom = DesiredZoom(m_pos.x, m_pos.y);
		const float adjustZoom = desiredZoom - m_zoom;
		if (std::fabs(adjustZoom) >= 0.001f) {
			m_zoom += adjustZoom * AdjustFactor(timing);
			return true;
		}
		return false;
	}

	bool MovePivotToGround(const CameraFrameTiming &timing)
	{
		const float groundLevel = m_pos.z;
		const float groundLevelDiff = m_terrainHeightAtPivot - groundLevel;
		if (std::fabs(groundLevelDiff) <= 0.1f)
			return false;
		const float adjustFactor = AdjustFactor(timing);
		m_pos.z += groundLevelDiff * adjustFactor;

		// Reposition relative to the pitch: zooms along the view direction with the ground change.
		const EyeAndTarget positions = BuildCameraPosition();
		const Math::Vector3 delta = positions.target - positions.eye;
		if (std::fabs(delta.z) > 0.1f) {
			Math::Vector2 groundLevelCenter;
			Math::Vector2 terrainHeightCenter;
			const Math::LineSegment3 cameraLine{positions.eye, positions.target};
			const auto groundPoint = Math::Try_Point_At_Z(cameraLine, groundLevel);
			const auto terrainPoint = Math::Try_Point_At_Z(cameraLine, m_terrainHeightAtPivot);
			if (groundPoint && terrainPoint) {
				groundLevelCenter = {groundPoint->x, groundPoint->y};
				terrainHeightCenter = {terrainPoint->x, terrainPoint->y};
			}
			Math::Vector2 posDiff = terrainHeightCenter - groundLevelCenter;
			// Weaker repositioning at low pitch, where it feels bad over the terrain.
			const float pitch = std::asin(std::fabs(delta.z) / delta.Length());
			constexpr float lowerPitch = (15.f * Math::Pi / 180.0f);
			constexpr float upperPitch = (30.f * Math::Pi / 180.0f);
			float repositionStrength = Math::InverseLerp(lowerPitch, upperPitch, pitch);
			repositionStrength = std::clamp<float>(repositionStrength, 0.0f, 1.0f);
			posDiff = posDiff * repositionStrength;
			m_pos.x += posDiff.x * adjustFactor;
			m_pos.y += posDiff.y * adjustFactor;
		}
		return true;
	}

	void UpdateCameraTransform()
	{
		EyeAndTarget positions = BuildCameraPosition();
		if (m_isUserControlled) {
			// Keep the camera above the terrain (averaged sampling reduces bumps).
			const float nearZ = m_settings.mapXYFactor;
			const float minAcceptableCameraHeight = HeightAroundPos(positions.eye.x, positions.eye.y, m_settings.mapXYFactor) + nearZ;
			if (positions.eye.z < minAcceptableCameraHeight) {
				const float repositionZ = minAcceptableCameraHeight - positions.eye.z;
				positions.eye.z += repositionZ;
				positions.target.z += repositionZ;
			}
		}
		m_view.eye = positions.eye;
		m_view.target = positions.target;
		m_view.transform = Math::AffineTransform3::Look_At(positions.eye, positions.target);
		m_view.horizontalFieldOfView = m_fov;
		m_view.aspectRatio = m_aspectRatio;
		UpdateCameraClipPlanes();
	}

	void UpdateCameraClipPlanes()
	{
		float farZ;
		if (m_settings.drawEntireTerrain) {
			farZ = 100000.0f;
		} else if (m_terrainDrawBounds) {
			const Math::Vector3 camPos = m_view.transform.Translation();
			const Math::Vector3 camDir = m_view.transform.Basis_Z() * -1.0f;
			const TerrainDrawBounds &region = *m_terrainDrawBounds;
			const Math::Vector3 center{(region.lo.x + region.hi.x) * 0.5f, (region.lo.y + region.hi.y) * 0.5f,
				region.minHeight - 1.0f}; // -1 to avoid Z clipping when looking straight down
			const float dx = (region.hi.x - region.lo.x) * 0.5f;
			const float dy = (region.hi.y - region.lo.y) * 0.5f;
			const Math::Vector3 v = center - camPos;
			const float projectedDistanceToCenter = std::fabs(v.Dot(camDir));
			const float projectedRadiusToEdge = std::fabs(dx * camDir.x) + std::fabs(dy * camDir.y);
			farZ = std::max(projectedDistanceToCenter + projectedRadiusToEdge, 0.0f);
		} else {
			farZ = static_cast<float>(m_settings.terrainDrawWidth) * m_settings.mapXYFactor;
		}
		if (m_FXPitch < 0.95f)
			farZ *= 10.0f; // Extend far clip plane so entire terrain can be visible
		m_view.nearClip = m_settings.mapXYFactor;
		m_view.farClip = farZ;
	}

	// ---- Camera area constraints (keep the view inside the map) ----
	void UpdateCameraAreaConstraints()
	{
		if (!m_settings.useCameraConstraints)
			return;
		if (!m_cameraAreaConstraintsValid)
			CalcCameraAreaConstraints();
		if (m_cameraAreaConstraintsValid && !IsWithinCameraAreaConstraints()) {
			constexpr float eps = 0.1f;
			m_pos.x = LegacyClamp(m_cameraAreaConstraints.lo.x + eps, m_pos.x, m_cameraAreaConstraints.hi.x - eps);
			m_pos.y = LegacyClamp(m_cameraAreaConstraints.lo.y + eps, m_pos.y, m_cameraAreaConstraints.hi.y - eps);
			m_recalcCamera = true;
		}
	}

	bool IsWithinCameraAreaConstraints() const noexcept
	{
		const MapExtent &c = m_cameraAreaConstraints;
		return c.lo.x < m_pos.x && m_pos.x < c.hi.x && c.lo.y < m_pos.y && m_pos.y < c.hi.y;
	}

	void CalcCameraAreaConstraints()
	{
		if (!m_mapExtent)
			return;
		const MapExtent &mapRegion = *m_mapExtent;
		const EyeAndTarget positions = BuildCameraPosition();
		const auto transform = Math::AffineTransform3::Look_At(positions.eye, positions.target);
		float offset = CalcCameraAreaOffset(transform, m_pos.z);
		offset = std::min(offset, (mapRegion.hi.x - mapRegion.lo.x) / 2);
		offset = std::min(offset, (mapRegion.hi.y - mapRegion.lo.y) / 2);
		m_cameraAreaConstraints.lo.x = mapRegion.lo.x + offset;
		m_cameraAreaConstraints.hi.x = mapRegion.hi.x - offset;
		m_cameraAreaConstraints.lo.y = mapRegion.lo.y + offset;
		m_cameraAreaConstraints.hi.y = mapRegion.hi.y - offset;
		m_cameraAreaConstraintsValid = true;
	}

	// Legacy getPickRay for a W3D logical screen point (-1..1, +Y up).
	Math::LineSegment3 PickRay(const Math::AffineTransform3 &transform, float logicalX, float logicalY) const
	{
		const float widthHalf = std::tan(m_fov * 0.5f);
		const float heightHalf = widthHalf / m_aspectRatio;
		const Math::Vector3 rayStart = transform.Translation();
		const Math::Vector3 worldPoint = transform.Transform_Point({logicalX * widthHalf, logicalY * heightHalf, -1.0f});
		Math::Vector3 rayEnd = (worldPoint - rayStart).Normalized_Legacy();
		rayEnd = rayEnd * (m_view.farClip * 2);
		return {rayStart, rayEnd + rayStart};
	}

	float CalcCameraAreaOffset(const Math::AffineTransform3 &transform, float maxEdgeZ) const
	{
		const Math::LineSegment3 pickRay = PickRay(transform, 0.0f, 0.0f);
		if (std::fabs(pickRay.start.z - pickRay.end.z) < 1.0f)
			return 1e+6f; // Looking at the horizon would yield infinite numbers.
		const auto centerAtEdge = Math::Try_Point_At_Z(pickRay, maxEdgeZ);
		if (!centerAtEdge)
			return 1e+6f;
		const bool isLookingDown = pickRay.start.z >= pickRay.end.z;
		const Math::LineSegment3 edgeRay = PickRay(transform, 0.0f, isLookingDown ? -1.0f : 1.0f);
		const auto edgeAtEdge = Math::Try_Point_At_Z(edgeRay, maxEdgeZ);
		if (!edgeAtEdge)
			return 1e+6f;
		const Math::Vector2 center{centerAtEdge->x - edgeAtEdge->x, centerAtEdge->y - edgeAtEdge->y};
		// Reduced to allow scrolling closer to the edges.
		return center.Length() * 0.85f;
	}

	CameraSettings m_settings;
	TerrainHeightFunction m_terrainHeight;
	TerrainRayCastFunction m_terrainRayCast;
	std::optional<MapExtent> m_mapExtent;
	std::optional<TerrainDrawBounds> m_terrainDrawBounds;

	Math::Vector3 m_pos{};
	Math::Vector3 m_previousLookAtPosition{};
	float m_angle = 0.0f;
	float m_pitch = 0.0f;
	float m_defaultAngle = 0.0f;
	float m_defaultPitch = 0.0f;
	float m_zoom = 1.0f;
	float m_FXPitch = 1.0f;
	float m_fov = 0.0f;
	float m_aspectRatio = 1.0f;
	float m_maxHeightAboveGround = 0.0f;
	float m_minHeightAboveGround = 0.0f;
	float m_heightAboveGround = 0.0f;
	float m_currentHeightAboveGround = 0.0f;
	float m_terrainHeightAtPivot = 0.0f;
	float m_initialGroundLevel = 10.0f;
	bool m_zoomLimited = true;
	bool m_okToAdjustHeight = false;
	bool m_isUserControlled = true;

	std::uint32_t m_scriptedState = 0;
	float m_frameAdvance = 1.0f; // legacy frames this Update advances motions by
	RotateMotion m_rcInfo;
	PitchMotion m_pcInfo;
	ZoomMotion m_zcInfo;
	WaypointPathMotion m_mcwpInfo;
	bool m_cameraArrivedAtWaypointOnPath = false;
	bool m_freezeTimeForCameraMovement = false;
	int m_timeMultiplier = 1;

	LockTargetProvider m_lockTarget;
	CameraLockMode m_lockType = CameraLockMode::Follow;
	float m_lockDist = 0.0f;
	bool m_snapImmediate = false;
	float m_followFactor = -1.0f;

	Math::Vector2 m_shakeOffset{};
	float m_shakeAngleCos = 1.0f;
	float m_shakeAngleSin = 0.0f;
	float m_shakeIntensity = 0.0f;

	Math::Vector2 m_scrollAmount{};
	Math::Vector2 m_guardBandBias{};
	MapExtent m_cameraAreaConstraints{};
	bool m_cameraAreaConstraintsValid = false;
	bool m_recalcCameraConstraintsAfterScrolling = false;
	bool m_recalcCamera = true;

	CameraView m_view;
};
} // namespace engine::camera
