export module games.generalszh.presentation.scripted.algorithms.camera_script_effects;
import std;

export import games.generalszh.scripting.presentation_vocabulary;
export import games.generalszh.session.session_view;
export import engine.camera.model.rts_camera;
export import games.generalszh.presentation.objects.resources.object_frame;
import engine.ecs.core.world;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.objects.resources.presentation_resources;
import games.generalszh.presentation.interaction.components.selected;
import games.generalszh.presentation.camera.resources.camera_shakers;
import games.generalszh.presentation.camera.algorithms.camera_shaking;
import games.generalszh.presentation.camera.resources.view_filter;
import games.generalszh.presentation.camera.algorithms.motion_blur_steps;
import games.generalszh.presentation.camera.algorithms.selection_focus;

// What the scripts do to the player's camera (ScriptActions' camera actions: moves, paths, zooms, pitches, rotations,
// locks, the final-move modifiers, shakes, slave mode, motion blur), as each command arrives: the camera, the camera's
// presentation resources in the world, and what the host keeps for them (CameraScriptHost).
export namespace generalszh::presentation
{
struct CameraScriptHost
{
	session::SessionView &game;
	std::function<float(float, float)> terrainHeight; // the ground's height there
	// A named unit as the camera sees it each frame from now on (where it is drawn; gone: none, which ends a lock).
	std::function<engine::camera::LockTargetProvider(ecs::Entity)> lockOn;
	std::span<const PresentedObject> presented; // the objects as last drawn, in the drawn order
	bool &slaved;                               // CAMERA_ENABLE_SLAVE_MODE: the camera rides a unit's bone
	std::string &slaveUnit;
	std::string &slaveBone;

	Engine::Math::Vector3 Ground(const Engine::Math::FixedVector3 &position) const
	{
		const float x = Engine::Math::ToFloat(position.x), y = Engine::Math::ToFloat(position.y);
		return {x, y, terrainHeight ? terrainHeight(x, y) : 0.0f};
	}
};

inline void ApplyCameraScript(engine::camera::RtsCamera &camera, ecs::World &world, CameraScriptHost &host, const scripting::CameraScriptCommand &command)
{
	using Kind = scripting::CameraScriptCommand::Kind;
	const auto ms = [](std::int64_t value) { return static_cast<float>(value); };
	const auto real = [](Engine::Math::Fixed value) { return Engine::Math::ToFloat(value); };
	switch (command.kind)
	{
	case Kind::Setup:
		// doSetupCamera: moveCameraTo at once, cameraModLookToward, cameraModFinalPitch, cameraModFinalZoom.
		camera.MoveCameraTo(host.Ground(command.points[0]), 0, 0, true, 0.0f, 0.0f);
		camera.CameraModLookToward(host.Ground(command.points[1]));
		camera.CameraModFinalPitch(real(command.values[1]), 0.0f, 0.0f);
		camera.CameraModFinalZoom(real(command.values[0]), 0.0f, 0.0f);
		return;
	case Kind::Zoom:
		camera.ZoomCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		return;
	case Kind::Pitch:
		camera.PitchCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		return;
	case Kind::Rotate:
		camera.RotateCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		return;
	case Kind::Reset:
		camera.ResetCamera(host.Ground(command.points[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		return;
	case Kind::FreezeTime: camera.CameraModFreezeTime(); return;
	case Kind::FreezeAngle: camera.CameraModFreezeAngle(); return;
	case Kind::FinalZoom: camera.CameraModFinalZoom(real(command.values[0]), real(command.values[1]), real(command.values[2])); return;
	case Kind::FinalPitch: camera.CameraModFinalPitch(real(command.values[0]), real(command.values[1]), real(command.values[2])); return;
	case Kind::FinalSpeed: camera.CameraModFinalTimeMultiplier(static_cast<int>(command.count)); return;
	case Kind::RollingAverage: camera.CameraModRollingAverage(static_cast<int>(command.count)); return;
	case Kind::Follow:
	case Kind::Tether:
	{
		// setCameraLock, snapToCameraLock when asked, setSnapMode(LOCK_FOLLOW, 0) or (LOCK_TETHER, play).
		const ecs::Entity unit = host.game.Named(command.unit);
		if (!world.IsAlive(unit))
			return;
		camera.SetCameraLock(host.lockOn(unit));
		if (command.flag)
			camera.SnapToCameraLock();
		camera.SetSnapMode(command.kind == Kind::Follow ? engine::camera::CameraLockMode::Follow : engine::camera::CameraLockMode::Tether,
			command.kind == Kind::Follow ? 0.0f : real(command.values[0]));
		return;
	}
	case Kind::StopFollow: camera.SetCameraLock({}); return;
	case Kind::SetDefault:
	{
		// doCameraSetDefault (PRESERVE_RETAIL_SCRIPTED_CAMERA): pitch = ViewDefaultPitchRadians - pitch; the angle unused.
		constexpr float DefaultPitch = 37.5f * std::numbers::pi_v<float> / 180.0f;
		camera.SetDefaultView(DefaultPitch - real(command.values[0]), real(command.values[1]), real(command.values[2]));
		return;
	}
	case Kind::LookTowardObject:
	{
		const ecs::Entity unit = host.game.Named(command.unit);
		if (!world.IsAlive(unit))
			return;
		auto lock = host.lockOn(unit);
		camera.RotateCameraTowardTarget([lock]() -> std::optional<Engine::Math::Vector3> {
			if (const auto state = lock())
				return state->position;
			return std::nullopt;
		}, static_cast<int>(command.milliseconds), static_cast<int>(command.holdMilliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		return;
	}
	case Kind::LookTowardWaypoint:
		camera.RotateCameraTowardPosition(host.Ground(command.points[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds),
			ms(command.easeOutMilliseconds), command.flag);
		return;
	case Kind::ShakerAt:
		// doC3CameraShake -> W3DView::Add_Camera_Shake(waypoint, radius, duration, amplitude).
		if (auto *shakers = world.FindResource<CameraShakers>(); shakers != nullptr && !command.points.empty())
		{
			const Engine::Math::Vector3 at = host.Ground(command.points.front());
			AddCameraShake(*shakers, {at.x, at.y, at.z}, real(command.values[2]), real(command.values[1]), real(command.values[0]),
				world.Resource<PresentationRandom>().engine);
		}
		return;
	case Kind::Slave:
		// W3DView::cameraEnableSlaveMode(unit, bone).
		host.slaved = true;
		host.slaveUnit = command.unit;
		host.slaveBone = command.bone;
		return;
	case Kind::SlaveOff: host.slaved = false; return;
	case Kind::MotionBlur:
		if (auto *filter = world.FindResource<ViewFilter>())
			StartMotionBlur(*filter, command.flag, command.count != 0);
		return;
	case Kind::MotionBlurJump:
		// doCameraMotionBlurJump: the blur set up, the camera no longer the player's, the waypoint to jump to at its peak.
		if (auto *filter = world.FindResource<ViewFilter>(); filter != nullptr && !command.points.empty())
		{
			const Engine::Math::Vector3 at = host.Ground(command.points.front());
			StartMotionBlurJump(*filter, {at.x, at.y, at.z}, command.count != 0);
			camera.SetUserControlled(false);
		}
		return;
	case Kind::MotionBlurFollow:
		if (auto *filter = world.FindResource<ViewFilter>())
			StartMotionBlurFollow(*filter, static_cast<std::int32_t>(command.count));
		return;
	case Kind::MotionBlurEndFollow:
		if (auto *filter = world.FindResource<ViewFilter>())
			EndMotionBlurFollow(*filter);
		return;
	case Kind::MoveToSelection:
	{
		// doModCameraMoveToSelection: the selected drawables (as last drawn, in the drawn order) averaged;
		// cameraModFinalMoveTo there.
		const auto &selected = world.Side<Selected>();
		std::vector<std::array<float, 3>> positions;
		for (const PresentedObject &object : host.presented)
			if (selected.Get(object.entity) != nullptr)
				positions.push_back(object.position);
		if (const auto centre = SelectionCentre(positions))
			camera.CameraModFinalMoveTo({(*centre)[0], (*centre)[1], (*centre)[2]});
		return;
	}
	case Kind::TimeMultiplier: camera.SetTimeMultiplier(static_cast<int>(command.count)); return;
	case Kind::Shake:
	{
		// View::shake at the camera's position: the type's GameData intensity, a random direction.
		ecs::World &world = world;
		if (auto *shakes = world.FindResource<ShakeRequests>())
		{
			const auto &position = camera.Position();
			shakes->pending.push_back({static_cast<ShakeType>(command.count), {position.x, position.y, position.z}});
		}
		return;
	}
	default:
		break;
	}
	if (command.points.empty())
		return;
	switch (command.kind)
	{
	case Kind::MoveTo:
		camera.MoveCameraTo(host.Ground(command.points.front()), static_cast<int>(command.milliseconds),
			static_cast<int>(command.shutterMilliseconds), true, ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		break;
	case Kind::MoveAlongPath:
	{
		std::vector<Engine::Math::Vector3> path;
		for (const auto &point : command.points)
			path.push_back(host.Ground(point));
		camera.MoveCameraAlongWaypointPath(path, static_cast<int>(command.milliseconds), static_cast<int>(command.shutterMilliseconds),
			true, ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
		break;
	}
	case Kind::LookToward:
		camera.CameraModLookToward(host.Ground(command.points.front()));
		break;
	case Kind::FinalLookToward:
		camera.CameraModFinalLookToward(host.Ground(command.points.front()));
		break;
	}
}
}
