export module games.generalszh.content.physics.physics_content;
import std;

export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.physics.resources.physics_settings;
export import games.generalszh.content.objects.object_definition;
import games.generalszh.content.combat.combat_catalog;

// An object's body, from its PhysicsBehavior module: mass, friction per
// second (the original stores it per frame), bouncing, being killed once it
// comes to rest, the turn-rate factor, and how hard a landing must be to
// hurt (MinFallHeightForDamage, as the speed a fall from that high lands at;
// FallHeightDamageFactor). Zero Hour's world: gravity -64 per second squared,
// falls hurting as FALLING and killing as SPLATTED.
export namespace generalszh::content
{
engine::gameplay::PhysicsSettings ZeroHourPhysicsSettings(Engine::Math::Fixed gravity)
{
	engine::gameplay::PhysicsSettings settings;
	settings.gravity = gravity;
	settings.fallDamageType = *DamageTypeIndex("FALLING");
	settings.fallDeathType = *DeathTypeIndex("SPLATTED");
	return settings;
}

std::optional<engine::gameplay::PhysicsBody> ReadObjectPhysics(const ObjectDefinition &object, const engine::time::FixedStep &step,
	const engine::gameplay::PhysicsSettings &world)
{
	const ModuleEntry *physics = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.slot == ModuleSlot::Behavior && module.block != nullptr && module.type == "PhysicsBehavior" &&
			(physics == nullptr || (physics->copied && !module.copied)))
			physics = &module;
	if (physics == nullptr)
		return std::nullopt;
	using engine::config::Node;
	namespace flag = engine::gameplay::physics_flag;
	engine::gameplay::PhysicsBody body;
	const auto fixed = [&](std::string_view key) -> std::optional<Engine::Math::Fixed> {
		const Node *node = physics->block->Find(key);
		return node != nullptr ? engine::config::values::ParseFixed(node->Value()) : std::nullopt;
	};
	const auto yes = [&](std::string_view key) {
		const Node *node = physics->block->Find(key);
		return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
	};
	if (const auto mass = fixed("Mass"); mass && *mass > Engine::Math::Fixed{})
		body.mass = *mass;
	if (const auto friction = fixed("ForwardFriction"))
		body.forwardFriction = step.PerTick(*friction);
	if (const auto friction = fixed("LateralFriction"))
		body.lateralFriction = step.PerTick(*friction);
	if (const auto friction = fixed("AerodynamicFriction"))
		body.aerodynamicFriction = step.PerTick(*friction);
	if (const auto factor = fixed("PitchRollYawFactor"))
		body.rateFactor = *factor;
	if (const auto offset = fixed("CenterOfMassOffset"))
		body.centerOfMassOffset = *offset;
	if (const auto resistance = fixed("ShockResistance"))
		body.shockResistance = *resistance;
	// ShockMax*: radians per frame.
	if (const auto most = fixed("ShockMaxYaw"))
		body.shockMaxYaw = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(*most).units);
	if (const auto most = fixed("ShockMaxPitch"))
		body.shockMaxPitch = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(*most).units);
	if (const auto most = fixed("ShockMaxRoll"))
		body.shockMaxRoll = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(*most).units);
	if (yes("AllowBouncing"))
		body.flags |= flag::AllowBouncing | flag::AuthoredBouncing;
	if (yes("KillWhenRestingOnGround"))
		body.flags |= flag::KillWhenResting;
	// The original's defaults: hurt by falls from 40 up, one point per unit of speed over per unit of mass.
	body.minFallSpeed = world.FallSpeed(fixed("MinFallHeightForDamage").value_or(Engine::Math::Fixed::FromInt(40)));
	body.fallDamageFactor = fixed("FallHeightDamageFactor").value_or(Engine::Math::Fixed::One());
	// Projectiles never take falling damage.
	if (object.Is("PROJECTILE"))
		body.flags |= flag::ImmuneToFalling;
	return body;
}
}
