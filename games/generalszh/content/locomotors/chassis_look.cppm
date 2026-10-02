export module games.generalszh.content.locomotors.chassis_look;
import std;

export import engine.config.document.document;
export import Engine.Core.Math.Fixed;
import engine.config.binding.values;

// How a locomotor's body rocks as it moves (the original's Locomotor fields
// the drawable's calcPhysicsXform reads): its suspension springs and dampers
// (PitchStiffness, RollStiffness 0.1; PitchDamping, RollDamping 0.9;
// UniformAxialDamping 1), how far acceleration may pitch it
// (AccelerationPitchLimit, DecelerationPitchLimit: degrees), how hard a
// wheeled vehicle bounces (BounceAmount: degrees a second), the speed and
// acceleration pitch and roll factors, a missile's wobble (ThrustRoll,
// ThrustWobbleRate, ThrustMinWobble, ThrustMaxWobble) and an aircraft's
// rudder and elevator corrections. Angles in radians, rates per frame, in
// fixed point (the presentation takes them as it draws).
export namespace generalszh::content
{
enum class ChassisKind : std::uint8_t
{
	None,
	Wheels,
	Motorcycle,
	Treads,
	HoverOrWings,
	Thrust,
};

struct ChassisLook
{
	ChassisKind kind{ChassisKind::None};
	Engine::Math::Fixed accelPitchLimit;
	Engine::Math::Fixed decelPitchLimit;
	Engine::Math::Fixed bounceKick;
	Engine::Math::Fixed pitchStiffness{Engine::Math::Fixed::FromRatio(1, 10)};
	Engine::Math::Fixed rollStiffness{Engine::Math::Fixed::FromRatio(1, 10)};
	Engine::Math::Fixed pitchDamping{Engine::Math::Fixed::FromRatio(9, 10)};
	Engine::Math::Fixed rollDamping{Engine::Math::Fixed::FromRatio(9, 10)};
	Engine::Math::Fixed pitchByZVel;
	Engine::Math::Fixed forwardVel;
	Engine::Math::Fixed lateralVel;
	Engine::Math::Fixed forwardAccel;
	Engine::Math::Fixed lateralAccel;
	Engine::Math::Fixed uniformAxialDamping{Engine::Math::Fixed::One()};
	Engine::Math::Fixed thrustRoll;
	Engine::Math::Fixed wobbleRate;
	Engine::Math::Fixed minWobble;
	Engine::Math::Fixed maxWobble;
	Engine::Math::Fixed rudderDegree;
	Engine::Math::Fixed rudderRate;
	Engine::Math::Fixed elevatorDegree;
	Engine::Math::Fixed elevatorRate;
	// HasSuspension: its tires ride up and down (FOUR_WHEELS), down to MaximumWheelExtension (negative: below).
	bool hasSuspension{false};
	Engine::Math::Fixed maxWheelExtension;
};

using ChassisLooks = std::map<std::string, ChassisLook, std::less<>>;

ChassisLooks ReadChassisLooks(const engine::config::Document &document)
{
	// Degrees to radians (pi / 180).
	const Engine::Math::Fixed degrees = Engine::Math::Fixed::FromRatio(314159265, 18000000000ll);
	ChassisLooks looks;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Locomotor" || root.values.empty())
			continue;
		ChassisLook &look = looks[std::string(root.Value())];
		look = {};
		for (const engine::config::Node &field : root.children)
		{
			const auto number = [&] { return engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{}); };
			const std::string_view key = field.key;
			if (key == "Appearance")
			{
				const std::string_view name = field.Value();
				look.kind = name == "FOUR_WHEELS" ? ChassisKind::Wheels : name == "MOTORCYCLE" ? ChassisKind::Motorcycle
					: name == "TREADS" ? ChassisKind::Treads : name == "HOVER" || name == "WINGS" ? ChassisKind::HoverOrWings
					: name == "THRUST" ? ChassisKind::Thrust : ChassisKind::None;
			}
			else if (key == "AccelerationPitchLimit")
				look.accelPitchLimit = number() * degrees;
			else if (key == "DecelerationPitchLimit")
				look.decelPitchLimit = number() * degrees;
			else if (key == "BounceAmount")
				look.bounceKick = number() * degrees / Engine::Math::Fixed::FromInt(30); // INI::parseAngularVelocityReal: per frame
			else if (key == "PitchStiffness")
				look.pitchStiffness = number();
			else if (key == "RollStiffness")
				look.rollStiffness = number();
			else if (key == "PitchDamping")
				look.pitchDamping = number();
			else if (key == "RollDamping")
				look.rollDamping = number();
			else if (key == "PitchInDirectionOfZVelFactor")
				look.pitchByZVel = number();
			else if (key == "ForwardVelocityPitchFactor")
				look.forwardVel = number();
			else if (key == "LateralVelocityRollFactor")
				look.lateralVel = number();
			else if (key == "ForwardAccelerationPitchFactor")
				look.forwardAccel = number();
			else if (key == "LateralAccelerationRollFactor")
				look.lateralAccel = number();
			else if (key == "UniformAxialDamping")
				look.uniformAxialDamping = number();
			else if (key == "ThrustRoll")
				look.thrustRoll = number();
			else if (key == "ThrustWobbleRate")
				look.wobbleRate = number();
			else if (key == "ThrustMinWobble")
				look.minWobble = number();
			else if (key == "ThrustMaxWobble")
				look.maxWobble = number();
			else if (key == "RudderCorrectionDegree")
				look.rudderDegree = number();
			else if (key == "RudderCorrectionRate")
				look.rudderRate = number();
			else if (key == "ElevatorCorrectionDegree")
				look.elevatorDegree = number();
			else if (key == "ElevatorCorrectionRate")
				look.elevatorRate = number();
			else if (key == "HasSuspension")
				look.hasSuspension = engine::config::values::ParseBool(field.Value()).value_or(false);
			else if (key == "MaximumWheelExtension")
				look.maxWheelExtension = number();
		}
	}
	return looks;
}
}
