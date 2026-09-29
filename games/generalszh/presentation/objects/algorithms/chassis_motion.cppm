export module games.generalszh.presentation.objects.algorithms.chassis_motion;
import std;

export import games.generalszh.content.locomotors.chassis_look;

// How a vehicle's body rocks, one frame (a tick) at a time, as the
// original's Drawable::calcPhysicsXform* does by its locomotor's appearance:
//   wheels and treads: suspension springs pull its pitch and roll toward the
//   ground's slope under it (pitching up only half as eagerly), acceleration
//   pitches and rolls it against separate springs within the pitch limits;
//   a wheeled one also bounces at random while it moves, and in the air keeps
//   its attitude, lifted by how far its ends hang;
//   hover and wings: springs toward level, pitching and rolling with speed,
//   climb and acceleration, and an aircraft's rudder and elevator swing;
//   thrust (missiles): wobbling between its limits and rolling.
// Velocity and acceleration are per frame, the direction its facing; the
// result is added to the body's pose: lifted by z, pitched (positive: nose
// down), rolled (the original's -roll about x) and yawed.
export namespace generalszh::presentation
{
// A locomotor's chassis values as presentation computes with them.
struct ChassisTuning
{
	content::ChassisKind kind{content::ChassisKind::None};
	float accelPitchLimit{0}, decelPitchLimit{0}, bounceKick{0};
	float pitchStiffness{0.1f}, rollStiffness{0.1f}, pitchDamping{0.9f}, rollDamping{0.9f};
	float pitchByZVel{0}, forwardVel{0}, lateralVel{0}, forwardAccel{0}, lateralAccel{0}, uniformAxialDamping{1};
	float thrustRoll{0}, wobbleRate{0}, minWobble{0}, maxWobble{0};
	float rudderDegree{0}, rudderRate{0}, elevatorDegree{0}, elevatorRate{0};
	bool hasSuspension{false};
	float maxWheelExtension{0};
};

inline ChassisTuning TuningOf(const content::ChassisLook &look)
{
	const auto f = [](Engine::Math::Fixed value) { return static_cast<float>(static_cast<double>(value.Raw()) / 65536.0); };
	return {look.kind, f(look.accelPitchLimit), f(look.decelPitchLimit), f(look.bounceKick), f(look.pitchStiffness), f(look.rollStiffness),
		f(look.pitchDamping), f(look.rollDamping), f(look.pitchByZVel), f(look.forwardVel), f(look.lateralVel), f(look.forwardAccel),
		f(look.lateralAccel), f(look.uniformAxialDamping), f(look.thrustRoll), f(look.wobbleRate), f(look.minWobble), f(look.maxWobble),
		f(look.rudderDegree), f(look.rudderRate), f(look.elevatorDegree), f(look.elevatorRate), look.hasSuspension, f(look.maxWheelExtension)};
}

struct ChassisState
{
	float pitch{0}, roll{0}, pitchRate{0}, rollRate{0};
	float accelPitch{0}, accelRoll{0}, accelPitchRate{0}, accelRollRate{0};
	float wobble{1}, yaw{0}, yawModulator{0}, pitchModulator{0};
	// TWheelInfo's tire height offsets: front left, front right, rear left, rear right.
	std::array<float, 4> wheels{};
};

struct ChassisInput
{
	std::array<float, 2> dir{1, 0};
	std::array<float, 3> velocity{};
	std::array<float, 3> acceleration{};
	std::array<float, 3> groundNormal{0, 0, 1};
	bool airborne{false}; // significantly above the ground
	float height{0};      // above the ground
	bool motive{true};    // its locomotor drives it
	float speed{0};       // this frame, and its locomotor's top speed
	float maxSpeed{0};
	float majorRadius{0}, minorRadius{0};
	int bounce{0};        // 0..3: which way a wheeled bounce kicks (presentation's random)
};

struct ChassisPose
{
	float pitch{0}, roll{0}, yaw{0}, z{0};
	std::array<float, 4> wheels{}; // its tires' height offsets (front left, front right, rear left, rear right)
};

namespace chassis_detail
{
constexpr float HalfPi = 1.57079632679f;
constexpr float Pi = 3.14159265359f;

inline void LimitAcceleration(ChassisState &state, const ChassisTuning &look)
{
	state.accelPitch = std::clamp(state.accelPitch, -look.accelPitchLimit, std::max(-look.accelPitchLimit, look.decelPitchLimit));
	state.accelRoll = std::clamp(state.accelRoll, -look.accelPitchLimit, std::max(-look.accelPitchLimit, look.decelPitchLimit));
}

inline void AccelerationSprings(ChassisState &state, const ChassisTuning &look)
{
	state.accelPitchRate += -look.pitchStiffness * state.accelPitch - look.pitchDamping * state.accelPitchRate;
	state.accelPitch += state.accelPitchRate;
	state.accelRollRate += -look.rollStiffness * state.accelRoll - look.rollDamping * state.accelRollRate;
	state.accelRoll += state.accelRollRate;
}

inline void AccelerationKick(ChassisState &state, const ChassisTuning &look, const ChassisInput &in)
{
	const float forward = in.dir[0] * in.acceleration[0] + in.dir[1] * in.acceleration[1];
	state.accelPitchRate += -look.forwardAccel * forward;
	const float lateral = -in.dir[1] * in.acceleration[0] + in.dir[0] * in.acceleration[1];
	state.accelRollRate += -look.lateralAccel * lateral;
}

inline void GroundSlope(const ChassisInput &in, float &groundPitch, float &groundRoll)
{
	groundPitch = (in.groundNormal[0] * in.dir[0] + in.groundNormal[1] * in.dir[1]) * HalfPi;
	groundRoll = (in.groundNormal[0] * -in.dir[1] + in.groundNormal[1] * in.dir[0]) * HalfPi;
}

// calcPhysicsXformWheels's tire offsets: the front down and the rear up as the front rises (and the other way), the
// right down and the left up as the right rises; a tire dropping moves half way, one rising at once; none below the
// suspension's full extension.
inline void WheelOffsets(std::array<float, 4> &wheels, float pitchHeight, float rollHeight, float maxExtension)
{
	constexpr float Spring = 0.9f;
	std::array<float, 4> target{};
	if (pitchHeight < 0.0f)
	{
		target[0] = target[1] = Spring * (pitchHeight / 3 + pitchHeight / 2);
		target[2] = target[3] = -pitchHeight / 2 + pitchHeight / 4;
	}
	else
	{
		target[0] = target[1] = -pitchHeight / 4 + pitchHeight / 2;
		target[2] = target[3] = Spring * (-pitchHeight / 2 + -pitchHeight / 3);
	}
	if (rollHeight > 0.0f)
	{
		target[1] += -Spring * (rollHeight / 3 + rollHeight / 2);
		target[3] += -Spring * (rollHeight / 3 + rollHeight / 2);
		target[2] += rollHeight / 2 - rollHeight / 4;
		target[0] += rollHeight / 2 - rollHeight / 4;
	}
	else
	{
		target[1] += -rollHeight / 2 + rollHeight / 4;
		target[3] += -rollHeight / 2 + rollHeight / 4;
		target[2] += Spring * (rollHeight / 3 + rollHeight / 2);
		target[0] += Spring * (rollHeight / 3 + rollHeight / 2);
	}
	for (std::size_t wheel = 0; wheel < 4; ++wheel)
	{
		if (target[wheel] < wheels[wheel])
			wheels[wheel] += (target[wheel] - wheels[wheel]) / 2.0f;
		else
			wheels[wheel] = target[wheel];
		if (wheels[wheel] < maxExtension)
			wheels[wheel] = maxExtension;
	}
}

inline void Suspension(ChassisState &state, const ChassisTuning &look, float groundPitch, float groundRoll)
{
	state.pitchRate += -look.pitchStiffness * (state.pitch - groundPitch) - look.pitchDamping * state.pitchRate;
	if (state.pitchRate > 0.0f)
		state.pitchRate *= 0.5f;
	state.rollRate += -look.rollStiffness * (state.roll - groundRoll) - look.rollDamping * state.rollRate;
}
}

// calcPhysicsXformTreads (without the crushing overlap's climb).
inline ChassisPose StepTreads(ChassisState &state, const ChassisTuning &look, const ChassisInput &in)
{
	using namespace chassis_detail;
	float groundPitch = 0, groundRoll = 0;
	GroundSlope(in, groundPitch, groundRoll);
	Suspension(state, look, groundPitch, groundRoll);
	state.pitch += state.pitchRate * look.uniformAxialDamping;
	state.roll += state.rollRate * look.uniformAxialDamping;
	AccelerationSprings(state, look);
	ChassisPose pose{state.pitch + state.accelPitch, state.roll + state.accelRoll, 0, 0};
	if (in.motive)
		AccelerationKick(state, look, in);
	LimitAcceleration(state, look);
	return pose;
}

// calcPhysicsXformWheels (and the motorcycle's alike, its body's attitude).
inline ChassisPose StepWheels(ChassisState &state, const ChassisTuning &look, const ChassisInput &in)
{
	using namespace chassis_detail;
	float groundPitch = 0, groundRoll = 0;
	GroundSlope(in, groundPitch, groundRoll);
	if (in.airborne)
	{
		// Its rear tires hang down (to their full extension while it is not that far up, else back up).
		if (look.hasSuspension)
			for (const std::size_t rear : {std::size_t{2}, std::size_t{3}})
				state.wheels[rear] += ((in.height > -look.maxWheelExtension ? look.maxWheelExtension : 0.0f) - state.wheels[rear]) / 2.0f;
		// The same attitude through the air, lifted by how far its ends hang.
		const float pitchHeight = in.majorRadius * std::sin(state.pitch + state.accelPitch - groundPitch);
		const float rollHeight = in.minorRadius * std::sin(state.roll + state.accelRoll - groundRoll);
		return {state.pitch + state.accelPitch, state.roll + state.accelRoll, 0, std::fabs(pitchHeight) / 4 + std::fabs(rollHeight) / 4, state.wheels};
	}
	if (in.maxSpeed > 0.0f && in.speed > in.maxSpeed / 10)
	{
		const float factor = in.speed / in.maxSpeed;
		if (std::fabs(state.pitchRate) < factor * look.bounceKick / 4 && std::fabs(state.rollRate) < factor * look.bounceKick / 8)
		{
			const float pitchKick = look.bounceKick * factor, rollKick = look.bounceKick * factor / 2;
			state.pitchRate += (in.bounce == 1 || in.bounce == 3) ? pitchKick : -pitchKick;
			state.rollRate += (in.bounce >= 2) ? rollKick : -rollKick;
		}
	}
	Suspension(state, look, groundPitch, groundRoll);
	state.pitch += state.pitchRate * look.uniformAxialDamping;
	state.roll += state.rollRate * look.uniformAxialDamping;
	AccelerationSprings(state, look);
	ChassisPose pose{state.pitch + state.accelPitch, state.roll + state.accelRoll, 0, 0};
	if (in.motive)
		AccelerationKick(state, look, in);
	LimitAcceleration(state, look);
	// Its suspension: how far its ends and sides stand off the slope.
	const float pitchHeight = in.majorRadius * std::sin(pose.pitch - groundPitch);
	const float rollHeight = in.minorRadius * std::sin(pose.roll - groundRoll);
	if (look.hasSuspension)
		WheelOffsets(state.wheels, pitchHeight, rollHeight, look.maxWheelExtension);
	pose.wheels = state.wheels;
	// Lifted by them (more gently past 22.5 degrees of pitch).
	float divisor = 4.0f;
	const float pitch = std::fabs(pose.pitch - groundPitch);
	if (pitch > Pi / 8.0f)
		divisor = ((4.0f * Pi / 8.0f) + (pitch - Pi / 8.0f)) / pitch;
	pose.z = std::fabs(pitchHeight) / divisor + std::fabs(rollHeight) / divisor;
	return pose;
}

// calcPhysicsXformHoverOrWings.
inline ChassisPose StepHoverOrWings(ChassisState &state, const ChassisTuning &look, const ChassisInput &in)
{
	using namespace chassis_detail;
	state.pitchRate += -look.pitchStiffness * state.pitch - look.pitchDamping * state.pitchRate;
	state.rollRate += -look.rollStiffness * state.roll - look.rollDamping * state.rollRate;
	state.pitch += state.pitchRate * look.uniformAxialDamping;
	state.roll += state.rollRate * look.uniformAxialDamping;
	AccelerationSprings(state, look);
	ChassisPose pose{state.pitch + state.accelPitch, state.roll + state.accelRoll, 0, 0};
	if (in.motive)
	{
		if (look.pitchByZVel != 0.0f && std::fabs(in.velocity[2]) > 0.001f)
			state.pitch -= look.pitchByZVel * std::atan2(in.velocity[2], std::hypot(in.velocity[0], in.velocity[1]));
		state.pitch += -look.forwardVel * (in.dir[0] * in.velocity[0] + in.dir[1] * in.velocity[1]);
		state.roll += -look.lateralVel * (-in.dir[1] * in.velocity[0] + in.dir[0] * in.velocity[1]);
		AccelerationKick(state, look, in);
	}
	LimitAcceleration(state, look);
	state.yawModulator += look.rudderRate;
	state.pitchModulator += look.elevatorRate;
	pose.yaw = look.rudderDegree * std::sin(state.yawModulator);
	pose.pitch += look.elevatorDegree * std::cos(state.pitchModulator);
	return pose;
}

// calcPhysicsXformThrust.
inline ChassisPose StepThrust(ChassisState &state, const ChassisTuning &look, const ChassisInput &)
{
	ChassisPose pose;
	if (look.wobbleRate != 0.0f)
	{
		if (state.wobble >= 1.0f)
		{
			const float step = state.pitch < look.maxWobble - look.wobbleRate * 2 ? look.wobbleRate : look.wobbleRate / 2.0f;
			state.pitch += step;
			state.yaw += step;
			if (state.pitch >= look.maxWobble)
				state.wobble = -1.0f;
		}
		else
		{
			const float step = state.pitch >= look.minWobble + look.wobbleRate * 2.0f ? look.wobbleRate : look.wobbleRate / 2.0f;
			state.pitch -= step;
			state.yaw -= step;
			if (state.pitch <= look.minWobble)
				state.wobble = 1.0f;
		}
		pose.pitch = state.pitch;
		pose.yaw = state.yaw;
	}
	if (look.thrustRoll != 0.0f)
	{
		state.roll += look.thrustRoll;
		pose.roll = state.roll;
	}
	return pose;
}

inline ChassisPose StepChassis(ChassisState &state, const ChassisTuning &look, const ChassisInput &in)
{
	switch (look.kind)
	{
	case content::ChassisKind::Wheels:
	case content::ChassisKind::Motorcycle:
		return StepWheels(state, look, in);
	case content::ChassisKind::Treads:
		return StepTreads(state, look, in);
	case content::ChassisKind::HoverOrWings:
		return StepHoverOrWings(state, look, in);
	case content::ChassisKind::Thrust:
		return StepThrust(state, look, in);
	case content::ChassisKind::None:
		break;
	}
	return {};
}
}
