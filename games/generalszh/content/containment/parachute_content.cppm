export module games.generalszh.content.containment.parachute_content;
import std;

export import engine.gameplay.rts.parachute.definitions.parachute_definition;
export import games.generalszh.content.objects.object_definition;
export import engine.config.binding.schema;
import games.generalszh.content.combat.combat_catalog;

// Zero Hour's parachutes (ParachuteContain on an object, AmericaParachute) as the engine's parachute definition:
// PitchRateMax / RollRateMax (degrees a second), LowAltitudeDamping, ParachuteOpenDist, FreeFallDamagePercent,
// KillWhenLandingInWaterSlop and ParachuteOpenSound, with its SET_NORMAL and SET_FREEFALL locomotors as hover
// locomotors (Locomotor.ini, the original's units: speeds per second, accelerations per second squared,
// Extra2DFriction per second) and its model's PARA_COG and PARA_ATTCH at rest (supplied by the game's rigs).
export namespace generalszh::content
{
struct ParachuteContent
{
	engine::gameplay::ParachuteDefinition definition;
	std::string openSound;
};

using HoverLocomotors = std::map<std::string, engine::gameplay::HoverLocomotor, std::less<>>;

namespace parachute_content_detail
{
using engine::config::Node;

inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}
}

// Every Locomotor block as a hover locomotor (LocomotorTemplate's defaults where unset).
inline HoverLocomotors ReadHoverLocomotors(const engine::config::Document &document, const engine::time::FixedStep &step)
{
	using namespace parachute_content_detail;
	using Engine::Math::Fixed;
	HoverLocomotors hovers;
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	for (const Node &root : document.Roots())
	{
		if (root.key != "Locomotor" || root.values.empty())
			continue;
		engine::gameplay::HoverLocomotor hover;
		hover.braking = Fixed::FromInt(999999); // BIGNUM
		hover.closeEnough = Fixed::One();
		hover.pitchStiffness = hover.rollStiffness = Fixed::FromRatio(1, 10);
		hover.pitchDamping = hover.rollDamping = Fixed::FromRatio(9, 10);
		bool accelerationDamaged = false, liftDamaged = false;
		for (const Node &field : root.children)
		{
			const std::string_view key = field.key;
			const auto perSecond = [&] { return engine::config::ReadPerSecond(field, bind).value_or(Fixed{}); };
			const auto perSecondSquared = [&] { return engine::config::ReadPerSecondSquared(field, bind).value_or(Fixed{}); };
			const auto plain = [&] { return engine::config::ReadFixed(field, bind).value_or(Fixed{}); };
			if (Same(key, "Speed"))
				hover.maxSpeed = perSecond();
			else if (Same(key, "MinSpeed"))
				hover.minSpeed = perSecond();
			else if (Same(key, "Acceleration"))
				hover.acceleration = perSecondSquared();
			else if (Same(key, "Braking"))
				hover.braking = perSecondSquared();
			else if (Same(key, "Lift"))
				hover.lift = perSecondSquared();
			else if (Same(key, "AccelerationDamaged"))
			{
				hover.accelerationDamaged = perSecondSquared();
				accelerationDamaged = true;
			}
			else if (Same(key, "LiftDamaged"))
			{
				hover.liftDamaged = perSecondSquared();
				liftDamaged = true;
			}
			else if (Same(key, "SpeedLimitZ"))
				hover.speedLimitZ = perSecond();
			else if (Same(key, "Extra2DFriction"))
				hover.extraFriction = perSecond();
			else if (Same(key, "PreferredHeight"))
				hover.preferredHeight = plain();
			else if (Same(key, "PreferredHeightDamping"))
				hover.heightDamping = plain();
			else if (Same(key, "CloseEnoughDist"))
				hover.closeEnough = plain();
			else if (Same(key, "PitchStiffness"))
				hover.pitchStiffness = plain();
			else if (Same(key, "RollStiffness"))
				hover.rollStiffness = plain();
			else if (Same(key, "PitchDamping"))
				hover.pitchDamping = plain();
			else if (Same(key, "RollDamping"))
				hover.rollDamping = plain();
			else if (Same(key, "TurnRate"))
				hover.turnRate = engine::config::ReadDegreesPerSecond(field, bind).value_or(Engine::Math::TurnAngle{});
			else if (Same(key, "Apply2DFrictionWhenAirborne"))
				hover.airborneFriction = engine::config::ReadBool(field, bind).value_or(false);
			else if (Same(key, "CloseEnoughDist3D"))
				hover.closeEnough3D = engine::config::ReadBool(field, bind).value_or(false);
		}
		// LocomotorTemplate::validate: unset damaged values are the whole ones.
		if (!accelerationDamaged)
			hover.accelerationDamaged = hover.acceleration;
		if (!liftDamaged)
			hover.liftDamaged = hover.lift;
		hovers.insert_or_assign(std::string(root.Value()), hover);
	}
	return hovers;
}

// The object's ParachuteContain, its locomotors and its bones (PARA_COG, PARA_ATTCH; the origin when missing, as the
// original zeroes them); none without one.
inline std::optional<ParachuteContent> ReadParachute(const ObjectDefinition &object, const HoverLocomotors &hovers, const engine::time::FixedStep &step,
	const std::function<std::optional<Engine::Math::FixedVector3>(std::string_view bone)> &boneAt)
{
	using namespace parachute_content_detail;
	using Engine::Math::Fixed;
	const Node *block = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "ParachuteContain")
			block = module.block;
	if (block == nullptr)
		return std::nullopt;
	ParachuteContent content;
	auto &definition = content.definition;
	// onRemoving drowns a rider (DAMAGE_WATER, DEATH_FLOODED); onDie hurts one losing its chute (DAMAGE_FALLING, DEATH_SPLATTED).
	definition.drownDamageType = *DamageTypeIndex("WATER");
	definition.drownDeathType = *DeathTypeIndex("FLOODED");
	definition.fallDamageType = *DamageTypeIndex("FALLING");
	definition.fallDeathType = *DeathTypeIndex("SPLATTED");
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	for (const Node &field : block->children)
	{
		const std::string_view key = field.key;
		if (Same(key, "PitchRateMax"))
			definition.pitchRateMax = static_cast<std::int32_t>(engine::config::ReadDegreesPerSecond(field, bind).value_or(Engine::Math::TurnAngle{}).units);
		else if (Same(key, "RollRateMax"))
			definition.rollRateMax = static_cast<std::int32_t>(engine::config::ReadDegreesPerSecond(field, bind).value_or(Engine::Math::TurnAngle{}).units);
		else if (Same(key, "LowAltitudeDamping"))
			definition.lowAltitudeDamping = engine::config::ReadFixed(field, bind).value_or(definition.lowAltitudeDamping);
		else if (Same(key, "ParachuteOpenDist"))
			definition.openDistance = engine::config::ReadFixed(field, bind).value_or(Fixed{});
		else if (Same(key, "FreeFallDamagePercent"))
			definition.freeFallDamage = engine::config::ReadPercent(field, bind).value_or(definition.freeFallDamage);
		else if (Same(key, "KillWhenLandingInWaterSlop"))
			definition.waterSlop = engine::config::ReadFixed(field, bind).value_or(definition.waterSlop);
		else if (Same(key, "ParachuteOpenSound"))
			content.openSound = std::string(field.Value());
	}
	const auto locomotor = [&](std::string_view set) -> engine::gameplay::HoverLocomotor {
		for (const Node *node : object.locomotorSets)
			if (node != nullptr && node->values.size() >= 2 && Same(node->Value(0), set))
				for (std::size_t index = 1; index < node->values.size(); ++index)
					if (const auto found = hovers.find(node->Value(index)); found != hovers.end())
						return found->second;
		return {};
	};
	definition.open = locomotor("SET_NORMAL");
	definition.freeFall = locomotor("SET_FREEFALL");
	definition.swayBone = boneAt("PARA_COG").value_or(Engine::Math::FixedVector3{});
	definition.attachBone = boneAt("PARA_ATTCH").value_or(Engine::Math::FixedVector3{});
	return content;
}
}
