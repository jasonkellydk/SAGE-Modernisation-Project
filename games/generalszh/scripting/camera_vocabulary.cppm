export module games.generalszh.scripting.camera_vocabulary;
import std;

export import engine.scripting.runtime.script_runtime;
export import engine.level.model.level;
import games.generalszh.scripting.script_parameters;

// The Zero Hour camera script actions. The camera is presentation, so the
// actions do not move it: they queue CameraScriptCommands (fixed-point, in
// world units and milliseconds) for the client to apply to its camera, as
// the original's script engine calls into the tactical view. Waypoints are
// the level's markers, by name.
export namespace generalszh::scripting
{
struct CameraScriptCommand
{
	enum class Kind : std::uint8_t
	{
		MoveTo,
		MoveAlongPath,
		LookToward,
		FinalLookToward,
		Setup,              // doSetupCamera: points {position, look at}; values {zoom, pitch}
		Zoom,               // zoomCamera: values {zoom}, timed
		Pitch,              // pitchCamera: values {pitch}, timed
		Rotate,             // rotateCamera: values {rotations}, timed
		Reset,              // resetCamera: points {location}, timed
		FreezeTime,         // cameraModFreezeTime
		FreezeAngle,        // cameraModFreezeAngle
		FinalZoom,          // cameraModFinalZoom: values {zoom, ease in, ease out}
		FinalPitch,         // cameraModFinalPitch: values {pitch, ease in, ease out}
		FinalSpeed,         // cameraModFinalTimeMultiplier: count
		RollingAverage,     // cameraModRollingAverage: count
		Follow,             // doCameraFollowNamed: unit, flag (snap to it)
		StopFollow,         // setCameraLock(INVALID_ID)
		Tether,             // doCameraTetherNamed: unit, flag (snap), values {play}
		SetDefault,         // doCameraSetDefault: values {pitch, angle, max height}
		LookTowardObject,   // rotateCameraTowardObject: unit, timed, hold
		LookTowardWaypoint, // rotateCameraTowardPosition: points {location}, timed, flag (reverse)
		Shake,              // doScreenShake: count (the shake type), at the camera's position
	};

	Kind kind{Kind::MoveTo};
	// MoveTo / LookToward / FinalLookToward: one point; MoveAlongPath: the
	// path from the named waypoint along each waypoint's first link.
	std::vector<Engine::Math::FixedVector3> points;
	std::int64_t milliseconds{0};
	std::int64_t shutterMilliseconds{0};
	std::int64_t easeInMilliseconds{0};
	std::int64_t easeOutMilliseconds{0};
	std::array<Engine::Math::Fixed, 3> values{};
	std::string unit; // a named unit (the client finds it among what it shows)
	std::int64_t holdMilliseconds{0};
	std::int64_t count{0};
	bool flag{false};
};

struct CameraScriptHost
{
	std::vector<CameraScriptCommand> *commands{nullptr};
	// CAMERA_MOVEMENT_FINISHED: the client's camera answers.
	std::function<bool()> movementFinished;
};

namespace detail
{
// Waypoint lookup by name, and the first-link chain the original's camera
// path follows (at most `limit` points, stopping at a revisited waypoint).
class WaypointIndex
{
public:
	explicit WaypointIndex(const engine::level::Level &level) : m_level(level)
	{
		for (std::size_t index = 0; index < level.markers.size(); ++index)
		{
			m_byName.emplace(level.markers[index].name, index);
			m_byId.emplace(level.markers[index].id, index);
		}
	}

	const engine::level::Marker *Find(const std::string &name) const
	{
		// The first waypoint of that name in level order, as the original's list walk.
		const auto found = m_byName.lower_bound(name);
		return found == m_byName.end() || found->first != name ? nullptr : &m_level.markers[found->second];
	}

	std::vector<Engine::Math::FixedVector3> FirstLinkPath(const engine::level::Marker &start, std::size_t limit) const
	{
		std::vector<Engine::Math::FixedVector3> path;
		std::vector<std::uint32_t> visited;
		const engine::level::Marker *current = &start;
		while (current != nullptr && path.size() < limit)
		{
			path.push_back(current->position);
			visited.push_back(current->id);
			if (current->links.empty())
				break;
			const auto next = m_byId.find(current->links.front());
			if (next == m_byId.end() || std::find(visited.begin(), visited.end(), current->links.front()) != visited.end())
				break;
			current = &m_level.markers[next->second];
		}
		return path;
	}

private:
	const engine::level::Level &m_level;
	std::multimap<std::string, std::size_t> m_byName;
	std::map<std::uint32_t, std::size_t> m_byId;
};
}

// The waypoint path a camera may follow (the original's view limit).
inline constexpr std::size_t CameraPathLimit = 25;

inline void AddCameraVocabulary(engine::scripting::Vocabulary &vocabulary, const engine::level::Level &level, CameraScriptHost host)
{
	using engine::scripting::ScriptCallContext;
	using Kind = CameraScriptCommand::Kind;
	auto waypoints = std::make_shared<detail::WaypointIndex>(level);
	const auto push = [host](CameraScriptCommand command) {
		if (host.commands != nullptr)
			host.commands->push_back(std::move(command));
	};
	// (waypoint, seconds, shutter seconds, ease-in seconds, ease-out seconds)
	const auto timed = [](ScriptCallContext &c, CameraScriptCommand &command) {
		command.milliseconds = parameters::Milliseconds(c, 1);
		command.shutterMilliseconds = parameters::Milliseconds(c, 2);
		command.easeInMilliseconds = parameters::Milliseconds(c, 3);
		command.easeOutMilliseconds = parameters::Milliseconds(c, 4);
	};
	vocabulary.AddAction("MOVE_CAMERA_TO", [waypoints, push, timed](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
		{
			CameraScriptCommand command{Kind::MoveTo, {waypoint->position}};
			timed(c, command);
			push(std::move(command));
		}
	});
	vocabulary.AddAction("MOVE_CAMERA_ALONG_WAYPOINT_PATH", [waypoints, push, timed](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
		{
			CameraScriptCommand command{Kind::MoveAlongPath, waypoints->FirstLinkPath(*waypoint, CameraPathLimit)};
			timed(c, command);
			push(std::move(command));
		}
	});
	vocabulary.AddAction("CAMERA_MOD_LOOK_TOWARD", [waypoints, push](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
			push({Kind::LookToward, {waypoint->position}});
	});
	vocabulary.AddAction("CAMERA_MOD_FINAL_LOOK_TOWARD", [waypoints, push](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
			push({Kind::FinalLookToward, {waypoint->position}});
	});
	// SETUP_CAMERA(waypoint, zoom, pitch, look-at waypoint): moveCameraTo at once, then look toward, final pitch and zoom.
	vocabulary.AddAction("SETUP_CAMERA", [waypoints, push](ScriptCallContext &c) {
		const auto *at = waypoints->Find(parameters::Text(c, 0));
		const auto *lookAt = waypoints->Find(parameters::Text(c, 3));
		if (at == nullptr || lookAt == nullptr)
			return;
		CameraScriptCommand command{Kind::Setup, {at->position, lookAt->position}};
		command.values = {parameters::Number(c, 1), parameters::Number(c, 2)};
		push(std::move(command));
	});
	// (value, seconds, ease-in seconds, ease-out seconds)
	const auto valueTimed = [push](Kind kind) {
		return [push, kind](ScriptCallContext &c) {
			CameraScriptCommand command{kind};
			command.values[0] = parameters::Number(c, 0);
			command.milliseconds = parameters::Milliseconds(c, 1);
			command.easeInMilliseconds = parameters::Milliseconds(c, 2);
			command.easeOutMilliseconds = parameters::Milliseconds(c, 3);
			push(std::move(command));
		};
	};
	vocabulary.AddAction("ZOOM_CAMERA", valueTimed(Kind::Zoom));
	vocabulary.AddAction("PITCH_CAMERA", valueTimed(Kind::Pitch));
	vocabulary.AddAction("ROTATE_CAMERA", valueTimed(Kind::Rotate));
	// RESET_CAMERA(waypoint, seconds, ease in, ease out): the first waypoint of that name.
	vocabulary.AddAction("RESET_CAMERA", [waypoints, push](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
		{
			CameraScriptCommand command{Kind::Reset, {waypoint->position}};
			command.milliseconds = parameters::Milliseconds(c, 1);
			command.easeInMilliseconds = parameters::Milliseconds(c, 2);
			command.easeOutMilliseconds = parameters::Milliseconds(c, 3);
			push(std::move(command));
		}
	});
	vocabulary.AddAction("CAMERA_MOD_FREEZE_TIME", [push](ScriptCallContext &) { push({Kind::FreezeTime}); });
	vocabulary.AddAction("CAMERA_MOD_FREEZE_ANGLE", [push](ScriptCallContext &) { push({Kind::FreezeAngle}); });
	// (value, ease in, ease out): the original hands its ease times on as they are (seconds, not milliseconds).
	const auto values = [push](Kind kind) {
		return [push, kind](ScriptCallContext &c) {
			CameraScriptCommand command{kind};
			command.values = {parameters::Number(c, 0), parameters::Number(c, 1), parameters::Number(c, 2)};
			push(std::move(command));
		};
	};
	vocabulary.AddAction("CAMERA_MOD_SET_FINAL_ZOOM", values(Kind::FinalZoom));
	vocabulary.AddAction("CAMERA_MOD_SET_FINAL_PITCH", values(Kind::FinalPitch));
	const auto counted = [push](Kind kind) {
		return [push, kind](ScriptCallContext &c) {
			CameraScriptCommand command{kind};
			command.count = parameters::Integer(c, 0);
			push(std::move(command));
		};
	};
	vocabulary.AddAction("CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER", counted(Kind::FinalSpeed));
	vocabulary.AddAction("CAMERA_MOD_SET_ROLLING_AVERAGE", counted(Kind::RollingAverage));
	vocabulary.AddAction("SCREEN_SHAKE", counted(Kind::Shake));
	// CAMERA_FOLLOW_NAMED(unit, snap), CAMERA_TETHER_NAMED(unit, snap, play).
	vocabulary.AddAction("CAMERA_FOLLOW_NAMED", [push](ScriptCallContext &c) {
		CameraScriptCommand command{Kind::Follow};
		command.unit = parameters::Text(c, 0);
		command.flag = parameters::Integer(c, 1) != 0;
		push(std::move(command));
	});
	vocabulary.AddAction("CAMERA_TETHER_NAMED", [push](ScriptCallContext &c) {
		CameraScriptCommand command{Kind::Tether};
		command.unit = parameters::Text(c, 0);
		command.flag = parameters::Integer(c, 1) != 0;
		command.values[0] = parameters::Number(c, 2);
		push(std::move(command));
	});
	vocabulary.AddAction("CAMERA_STOP_FOLLOW", [push](ScriptCallContext &) { push({Kind::StopFollow}); });
	vocabulary.AddAction("CAMERA_STOP_TETHER_NAMED", [push](ScriptCallContext &) { push({Kind::StopFollow}); });
	// CAMERA_SET_DEFAULT(pitch, angle, max height).
	vocabulary.AddAction("CAMERA_SET_DEFAULT", values(Kind::SetDefault));
	// CAMERA_LOOK_TOWARD_OBJECT(unit, seconds, hold seconds, ease in, ease out).
	vocabulary.AddAction("CAMERA_LOOK_TOWARD_OBJECT", [push](ScriptCallContext &c) {
		CameraScriptCommand command{Kind::LookTowardObject};
		command.unit = parameters::Text(c, 0);
		command.milliseconds = parameters::Milliseconds(c, 1);
		command.holdMilliseconds = parameters::Milliseconds(c, 2);
		command.easeInMilliseconds = parameters::Milliseconds(c, 3);
		command.easeOutMilliseconds = parameters::Milliseconds(c, 4);
		push(std::move(command));
	});
	// CAMERA_LOOK_TOWARD_WAYPOINT(waypoint, seconds, ease in, ease out, reverse).
	vocabulary.AddAction("CAMERA_LOOK_TOWARD_WAYPOINT", [waypoints, push](ScriptCallContext &c) {
		if (const auto *waypoint = waypoints->Find(parameters::Text(c, 0)))
		{
			CameraScriptCommand command{Kind::LookTowardWaypoint, {waypoint->position}};
			command.milliseconds = parameters::Milliseconds(c, 1);
			command.easeInMilliseconds = parameters::Milliseconds(c, 2);
			command.easeOutMilliseconds = parameters::Milliseconds(c, 3);
			command.flag = parameters::Integer(c, 4) != 0;
			push(std::move(command));
		}
	});
	// doCameraMoveHome: does nothing.
	vocabulary.AddAction("CAMERA_MOVE_HOME", [](ScriptCallContext &) {});
	vocabulary.AddCondition("CAMERA_MOVEMENT_FINISHED", [host](ScriptCallContext &) {
		return !host.movementFinished || host.movementFinished();
	});
}
}
