export module games.generalszh.content.locomotors.locomotor_catalog;
import std;

export import engine.config.binding.schema;
export import engine.gameplay.rts.movement.definitions.locomotor;
import games.generalszh.content.objects.object_definition;

// "Locomotor <name>" blocks (Data/INI/Locomotor.ini) bound onto the engine's
// locomotor definition, and an object's locomotor for a locomotor set.
export namespace generalszh::content
{
using engine::gameplay::HeightBehavior;
using engine::gameplay::LocomotorAppearance;
using engine::gameplay::LocomotorDefinition;

inline constexpr std::array<engine::config::EnumName<LocomotorAppearance>, 9> LocomotorAppearanceNames{{
	{"TWO_LEGS", LocomotorAppearance::TwoLegs},
	{"FOUR_WHEELS", LocomotorAppearance::FourWheels},
	{"TREADS", LocomotorAppearance::Treads},
	{"HOVER", LocomotorAppearance::Hover},
	{"THRUST", LocomotorAppearance::Thrust},
	{"WINGS", LocomotorAppearance::Wings},
	{"CLIMBER", LocomotorAppearance::Climber},
	{"OTHER", LocomotorAppearance::Other},
	{"MOTORCYCLE", LocomotorAppearance::Motorcycle},
}};

inline constexpr std::array<engine::config::EnumName<HeightBehavior>, 8> HeightBehaviorNames{{
	{"NO_Z_MOTIVE_FORCE", HeightBehavior::NoMotiveForce},
	{"SEA_LEVEL", HeightBehavior::SeaLevel},
	{"SURFACE_RELATIVE_HEIGHT", HeightBehavior::SurfaceRelative},
	{"ABSOLUTE_HEIGHT", HeightBehavior::Absolute},
	{"FIXED_SURFACE_RELATIVE_HEIGHT", HeightBehavior::FixedSurfaceRelative},
	{"FIXED_ABSOLUTE_HEIGHT", HeightBehavior::FixedAbsolute},
	{"FIXED_RELATIVE_TO_GROUND_AND_BUILDINGS", HeightBehavior::FixedRelativeToGroundAndBuildings},
	{"RELATIVE_TO_HIGHEST_LAYER", HeightBehavior::RelativeToHighestLayer},
}};

// The locomotor sets a unit moves on, in the port's numbering (0 its normal one; the names as TheLocomotorSetNames;
// SET_NORMAL_UPGRADED is the normal set of a unit with its locomotor upgrade).
inline constexpr std::array<std::string_view, 7> LocomotorSetNames{"SET_NORMAL", "SET_WANDER", "SET_PANIC", "SET_SLUGGISH", "SET_TAXIING", "SET_SUPERSONIC",
	"SET_FREEFALL"};

// A set's number by its name; none: not one of them.
inline std::optional<std::uint8_t> LocomotorSetIndex(std::string_view name) noexcept
{
	for (std::size_t index = 0; index < LocomotorSetNames.size(); ++index)
		if (LocomotorSetNames[index] == name)
			return static_cast<std::uint8_t>(index);
	return std::nullopt;
}

engine::config::Schema<LocomotorDefinition> LocomotorSchema()
{
	using D = LocomotorDefinition;
	engine::config::Schema<D> schema;
	schema.PerSecond("Speed", &D::maxSpeed)
		.PerSecond("MinSpeed", &D::minSpeed)
		.PerSecondSquared("Acceleration", &D::acceleration)
		.PerSecondSquared("Braking", &D::braking)
		.DegreesPerSecond("TurnRate", &D::turnRate)
		.Degrees("MaxThrustAngle", &D::maxThrustAngle)
		.Fixed("PreferredHeight", &D::preferredHeight)
		.Fixed("PreferredHeightDamping", &D::preferredHeightDamping)
		.Fixed("CloseEnoughDist", &D::closeEnough)
		.PerSecond("MinTurnSpeed", &D::minTurnSpeed)
		.Boolean("CanMoveBackwards", &D::canMoveBackward)
		.Fixed("WanderWidthFactor", &D::wanderWidth)
		.Fixed("WanderLengthFactor", &D::wanderLength)
		.Fixed("WanderAboutPointRadius", &D::wanderAboutPointRadius)
		.Enum("Appearance", &D::appearance, std::span<const engine::config::EnumName<LocomotorAppearance>>(LocomotorAppearanceNames))
		.Enum("ZAxisBehavior", &D::height, std::span<const engine::config::EnumName<HeightBehavior>>(HeightBehaviorNames))
		.On("SpeedDamaged", [](const engine::config::Node &node, D &out, engine::config::BindContext &bind) {
			if (const auto value = engine::config::ReadPerSecond(node, bind); value && *value >= Engine::Math::Fixed{})
			{
				out.speedDamaged = *value;
				out.damagedGiven |= 1u;
			}
		})
		.On("AccelerationDamaged", [](const engine::config::Node &node, D &out, engine::config::BindContext &bind) {
			if (const auto value = engine::config::ReadPerSecondSquared(node, bind); value && *value >= Engine::Math::Fixed{})
			{
				out.accelerationDamaged = *value;
				out.damagedGiven |= 2u;
			}
		})
		.On("TurnRateDamaged", [](const engine::config::Node &node, D &out, engine::config::BindContext &bind) {
			if (const auto value = engine::config::ReadDegreesPerSecond(node, bind))
			{
				out.turnRateDamaged = *value;
				out.damagedGiven |= 4u;
			}
		})
		.On("Surfaces", [](const engine::config::Node &node, D &out, engine::config::BindContext &) {
			static constexpr std::pair<std::string_view, std::uint8_t> names[] = {{"GROUND", 1}, {"WATER", 2}, {"CLIFF", 4}, {"AIR", 8}, {"RUBBLE", 16}};
			out.surfaces = 0;
			for (const std::string_view value : node.values)
				for (const auto &[name, bit] : names)
					if (value == name)
						out.surfaces |= bit;
		});
	// Recognised; ported with the locomotor's physics, damage and wander.
	for (const char *key : {"Lift", "LiftDamaged",
			 "CirclingRadius", "Extra2DFriction", "SpeedLimitZ", "GroupMovementPriority", "AccelerationPitchLimit",
			 "DecelerationPitchLimit", "BounceAmount", "PitchStiffness", "RollStiffness", "PitchDamping", "RollDamping", "ThrustRoll",
			 "ThrustWobbleRate", "ThrustMinWobble", "ThrustMaxWobble", "PitchInDirectionOfZVelFactor", "ForwardVelocityPitchFactor",
			 "LateralVelocityRollFactor", "ForwardAccelerationPitchFactor", "LateralAccelerationRollFactor", "UniformAxialDamping",
			 "TurnPivotOffset", "Apply2DFrictionWhenAirborne", "DownhillOnly", "AllowAirborneMotiveForce", "LocomotorWorksWhenDead",
			 "AirborneTargetingHeight", "StickToGround", "HasSuspension", "FrontWheelTurnAngle",
			 "MaximumWheelExtension", "MaximumWheelCompression", "CloseEnoughDist3D", "SlideIntoPlaceTime",
			 "RudderCorrectionDegree", "RudderCorrectionRate",
			 "ElevatorCorrectionDegree", "ElevatorCorrectionRate"})
		schema.Ignore(key);
	return schema;
}

engine::config::DefinitionTable<LocomotorDefinition> BuildLocomotorCatalog(const engine::config::Document &document,
	engine::config::BindContext &context)
{
	engine::config::DefinitionTable<LocomotorDefinition> locomotors;
	engine::config::BindBlocks(document, "Locomotor", LocomotorSchema(), locomotors, context, engine::config::Redefinition::Replace);
	// LocomotorTemplate::validate: a damaged rate below zero (not given) is the full one.
	std::vector<std::string> names;
	for (const auto &entry : locomotors)
		names.emplace_back(entry.first);
	for (const std::string &name : names)
		SettleLocomotorRates(*locomotors.Find(name));
	return locomotors;
}

// How far each locomotor turns its front wheels (FrontWheelTurnAngle,
// degrees): presentation's, so kept apart from the movement definitions.
std::map<std::string, Engine::Math::Fixed, std::less<>> ReadWheelTurnAngles(const engine::config::Document &document)
{
	std::map<std::string, Engine::Math::Fixed, std::less<>> angles;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Locomotor" || root.values.empty())
			continue;
		if (const auto *angle = root.Find("FrontWheelTurnAngle"))
			angles.insert_or_assign(std::string(root.Value()), engine::config::values::ParseFixed(angle->Value()).value_or(Engine::Math::Fixed{}));
	}
	return angles;
}

// The name of the first locomotor of the object's set that exists (SET_NORMAL unless asked otherwise); empty when none.
std::string_view ObjectLocomotorName(const ObjectDefinition &object, const engine::config::DefinitionTable<LocomotorDefinition> &locomotors,
	std::string_view set = "SET_NORMAL")
{
	for (const engine::config::Node *node : object.locomotorSets)
	{
		if (node == nullptr || node->values.size() < 2 || node->Value(0) != set)
			continue;
		for (std::size_t index = 1; index < node->values.size(); ++index)
			if (locomotors.Find(node->Value(index)) != nullptr)
				return node->Value(index);
	}
	return {};
}

// The first locomotor of the object's set (SET_NORMAL unless asked
// otherwise), or null when it has none.
const LocomotorDefinition *ObjectLocomotor(const ObjectDefinition &object,
	const engine::config::DefinitionTable<LocomotorDefinition> &locomotors, std::string_view set = "SET_NORMAL")
{
	for (const engine::config::Node *node : object.locomotorSets)
	{
		if (node == nullptr || node->values.size() < 2 || node->Value(0) != set)
			continue;
		for (std::size_t index = 1; index < node->values.size(); ++index)
			if (const auto *locomotor = locomotors.Find(node->Value(index)))
				return locomotor;
	}
	return nullptr;
}
}
