export module engine.gameplay.rts.death.algorithms.structure_topple;
import std;

export import engine.gameplay.rts.death.components.structure_topple;
export import engine.gameplay.rts.death.definitions.death_definition;
export import engine.gameplay.rts.death.algorithms.death_choice;
export import engine.gameplay.common.spatial.resources.ground_height;
export import Engine.Core.Math.FixedAngle;
export import Engine.Core.Math.FixedRandom;
export import Engine.Core.Math.UInt128;

// StructureToppleUpdate's arithmetic (beginStructureTopple, update, applyCrushingDamage, doDamageLine,
// doToppleDelayBurstFX, doPhaseStuff). Its lean is carried in Q32 radians: the first ticks' acceleration
// (0.02 x sin(0.001) x (1 - integrity)) is a fraction of a Q16 unit. Sines come from the shared Q1.30 Sin_Cos
// (~3.5e-6 absolute), the one departure from the original's float maths.
export namespace engine::gameplay::structure_topple
{
inline constexpr std::int64_t One = StructureTopple::One;
inline constexpr std::int64_t HalfPi = 6746518852;           // pi/2, Q32
inline constexpr std::int64_t ThetaCeiling = 2248839617;     // pi/6, Q32: crushing starts this close to the ground
inline constexpr std::int64_t Acceleration = 85899346;       // TOPPLE_ACCELERATION_FACTOR 0.02, Q32
inline constexpr std::int64_t TurnsPerRadianQ32 = 683565276; // 2^32 / 2pi, Q32
inline constexpr std::int64_t RadiansPerDegreeQ48 = 4912706252800; // pi/180 x 2^48: Q16 degrees to Q32 radians
// WEAPON_SPACING_PERPENDICULAR and WEAPON_SPACING_PARALLEL.
inline constexpr std::int64_t Spacing = 25;

// a x b, both Q32, rounded to nearest.
constexpr std::int64_t Multiply(std::int64_t a, std::int64_t b) noexcept
{
	const bool negative = (a < 0) != (b < 0);
	const auto magnitude = [](std::int64_t v) { return v < 0 ? static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v); };
	const Engine::Math::UInt128 product = Engine::Math::UInt128::Multiply(magnitude(a), magnitude(b)) + Engine::Math::UInt128{0, std::uint64_t{1} << 31};
	const auto shifted = static_cast<std::int64_t>((product >> 32).lo);
	return negative ? -shifted : shifted;
}

constexpr Engine::Math::TurnAngle TurnOf(std::int64_t radians) noexcept
{
	return Engine::Math::TurnAngle{static_cast<std::uint32_t>(static_cast<std::uint64_t>(Multiply(radians, TurnsPerRadianQ32)))};
}

// sin, Q32 in and out.
constexpr std::int64_t Sin(std::int64_t radians) noexcept
{
	return static_cast<std::int64_t>(Engine::Math::Sin_Cos(TurnOf(radians)).sine) * 4;
}

constexpr Engine::Math::Fixed ToFixed(std::int64_t q32) noexcept
{
	return Engine::Math::Fixed::FromRaw((q32 + (q32 >= 0 ? (std::int64_t{1} << 15) : -(std::int64_t{1} << 15))) >> 16);
}

constexpr std::int64_t FromFixed(Engine::Math::Fixed value) noexcept { return value.Raw() * (std::int64_t{1} << 16); }

// AngleFX: degrees as authored (Q16), to Q32 radians.
constexpr std::int64_t RadiansFromDegrees(Engine::Math::Fixed degrees) noexcept
{
	const auto magnitude = static_cast<std::uint64_t>(degrees.Raw() < 0 ? -degrees.Raw() : degrees.Raw());
	const auto radians = static_cast<std::int64_t>((Engine::Math::UInt128::Multiply(magnitude, static_cast<std::uint64_t>(RadiansPerDegreeQ48)) >> 32).lo);
	return degrees.Raw() < 0 ? -radians : radians;
}

// What a topple does, handed to `emit(kind, id, position, orient)`: an FX list (on it, or unrotated at a point),
// an object creation list, the crushing weapon fired at a point.
struct ToppleContext
{
	const StructureToppleDefinition &how;
	Engine::Math::FixedVector3 position; // where it stands
	Engine::Math::TurnAngle facing;
	const GroundHeight &ground;
	std::uint64_t tick;
};

template<typename Emit>
void PlayPhase(const ToppleContext &at, TopplePhase phase, Engine::Math::FixedVector3 where, Engine::Math::RandomStream &random, Emit &&emit)
{
	// doPhaseStuff: one of the phase's lists (m_oclCount is 1: nothing parses it).
	if (const auto id = PickEffect(at.how.objects[static_cast<std::size_t>(phase)], random))
		emit(DeathEffectKind::Objects, *id, where, true);
}

template<typename Emit>
void PlayEffect(const StructureTopple &topple, std::uint32_t effect, Engine::Math::FixedVector3 where, bool orient, Emit &&emit)
{
	if (topple.effects != 0 && effect != StructureToppleDefinition::None)
		emit(DeathEffectKind::Effect, effect, where, orient);
}

// beginStructureTopple: when it goes over (a logic random), which way (away from its killer, give or take pi/8;
// any way killed by nothing; a script's direction for it over both), its burst point (nine tenths of its average
// radius out that way, the unscripted one, on the ground), its start effects and when its first burst comes.
template<typename Emit>
StructureTopple BeginStructureTopple(const ToppleContext &at, std::uint32_t index, std::optional<Engine::Math::FixedVector2> killer,
	std::optional<Engine::Math::FixedVector2> scripted, std::uint32_t damageType, Engine::Math::RandomStream &random, Emit &&emit)
{
	using Engine::Math::Fixed;
	const StructureToppleDefinition &how = at.how;
	StructureTopple topple;
	topple.topple = index;
	topple.toppleTick = at.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(how.minToppleDelay),
		static_cast<std::int64_t>(std::max(how.minToppleDelay, how.maxToppleDelay))));
	Engine::Math::TurnAngle angle;
	if (!killer)
		angle = Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))};
	else
	{
		const Engine::Math::FixedVector2 away = at.position.XY() - *killer;
		angle = Engine::Math::Atan2(away.y, away.x) + Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, -(1ll << 28), 1ll << 28))};
	}
	topple.direction = {Engine::Math::Cos(angle), Engine::Math::Sin(angle)};
	topple.toppleAngle = angle;
	// ScriptEngine::adjustToppleDirection. The original tips it by the script's vector as given; taken as a
	// direction here (a position-sized vector would spin it over in a tick).
	if (scripted && (scripted->x != Fixed{} || scripted->y != Fixed{}))
	{
		topple.toppleAngle = Engine::Math::Atan2(scripted->y, scripted->x);
		topple.direction = {Engine::Math::Cos(topple.toppleAngle), Engine::Math::Sin(topple.toppleAngle)};
	}
	const Fixed out = (how.majorRadius + how.minorRadius) / Fixed::FromInt(2) * Fixed::FromRatio(9, 10);
	topple.burstAt = {at.position.x + out * Engine::Math::Cos(angle), at.position.y + out * Engine::Math::Sin(angle), Fixed{}};
	topple.burstAt.z = at.ground.At(topple.burstAt.XY());
	// Its effects only for the killing blow's kinds of damage (none: every kind).
	topple.effects = damageType >= 64 || ((how.damageFxTypes >> damageType) & 1u) != 0 ? 1 : 0;
	// doToppleStartFX.
	PlayEffect(topple, how.startEffect, at.position, false, emit);
	PlayPhase(at, TopplePhase::Initial, at.position, random, emit);
	topple.burstTick = at.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(how.minBurstDelay),
		static_cast<std::int64_t>(std::max(how.minBurstDelay, how.maxBurstDelay))));
	return topple;
}

// doToppleDelayBurstFX, and when the next burst comes.
template<typename Emit>
void Burst(StructureTopple &topple, const ToppleContext &at, Engine::Math::RandomStream &random, Emit &&emit)
{
	PlayEffect(topple, at.how.delayEffect, topple.burstAt, false, emit);
	PlayPhase(at, TopplePhase::Delay, topple.burstAt, random, emit);
	topple.burstTick = at.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(at.how.minBurstDelay),
		static_cast<std::int64_t>(std::max(at.how.minBurstDelay, at.how.maxBurstDelay))));
}

// doDamageLine: across the face of the building `along` out from its base, a crushing shot (and crushing effect) every
// 25 from -width short of the far edge, one on the edge, and the line's final-phase list at its middle. (The original
// steps across with (sin, cos) of its fall, not the perpendicular; kept.)
template<typename Emit>
void DamageLine(const StructureTopple &topple, const ToppleContext &at, Engine::Math::Fixed along, Engine::Math::Fixed width, Engine::Math::RandomStream &random,
	Emit &&emit)
{
	using Engine::Math::Fixed;
	const Fixed c = Engine::Math::Cos(topple.toppleAngle), s = Engine::Math::Sin(topple.toppleAngle);
	const Fixed jc = along * c, js = along * s;
	const auto shoot = [&](Fixed across) {
		Engine::Math::FixedVector3 target{at.position.x + jc + across * s, at.position.y + js + across * c, Fixed{}};
		target.z = at.ground.At(target.XY());
		emit(DeathEffectKind::Weapon, at.how.crushingWeapon, target, false);
		PlayEffect(topple, at.how.crushingEffect, target, false, emit);
	};
	for (Fixed across = Fixed{} - width; across < width; across += Fixed::FromInt(Spacing))
		shoot(across);
	shoot(width);
	Engine::Math::FixedVector3 middle{at.position.x + jc, at.position.y + js, Fixed{}};
	middle.z = at.ground.At(middle.XY());
	PlayPhase(at, TopplePhase::Final, middle, random, emit);
}

// applyCrushingDamage: `theta` (Q32) is its angle to the ground. Within pi/6 of it, the ground it now covers (out to its
// height x (1 - sin theta) from its base) is crushed a line every 25 on from where it last got to, and at the furthest.
// The width is its footprint across the fall, from its facing (the original's orientation read off its leaning
// transform; the same for every shipped toppler, their radii being equal).
template<typename Emit>
void Crush(StructureTopple &topple, const ToppleContext &at, std::int64_t theta, Engine::Math::RandomStream &random, Emit &&emit)
{
	using Engine::Math::Fixed;
	if (theta > ThetaCeiling || at.how.crushingWeapon == StructureToppleDefinition::None)
		return;
	const Engine::Math::TurnAngle angle = at.facing - topple.toppleAngle;
	const Engine::Math::FixedVector2 across{at.how.majorRadius * Engine::Math::Sin(angle), at.how.minorRadius * Engine::Math::Cos(angle)};
	const Fixed width = Engine::Math::Length(across) / Fixed::FromInt(2);
	const Fixed furthest = at.how.height * ToFixed(One - Sin(theta));
	Fixed along = topple.lastCrushed;
	for (; along < furthest; along += Fixed::FromInt(Spacing))
		DamageLine(topple, at, along, width, random, emit);
	DamageLine(topple, at, furthest, width, random, emit);
	topple.lastCrushed = along;
}

// One tick of StructureToppleUpdate::update after it died. True as it comes to rest (it then stands again,
// turned to its fall: doToppleDoneStuff; its post-collapse look shows).
template<typename Emit>
bool StepStructureTopple(StructureTopple &topple, const ToppleContext &at, Engine::Math::RandomStream &random, Emit &&emit)
{
	const StructureToppleDefinition &how = at.how;
	if (topple.state == StructureToppleState::Waiting)
	{
		if (at.tick >= topple.burstTick)
			Burst(topple, at, random, emit);
		if (at.tick >= topple.toppleTick)
		{
			topple.state = StructureToppleState::Toppling;
			topple.integrity = how.integrity;
		}
	}
	if (topple.state != StructureToppleState::Toppling)
		return false;
	topple.velocity += Multiply(Acceleration, Multiply(Sin(topple.angle), One - topple.integrity));
	if (topple.integrity > 0)
		topple.integrity = std::max<std::int64_t>(Multiply(topple.integrity, how.decay), 0);
	// doAngleFX: each angle the fall passes this tick.
	for (const ToppleAngleEffect &effect : how.angleEffects)
		if (effect.angle > topple.angle && effect.angle <= topple.angle + topple.velocity)
			PlayEffect(topple, effect.effect, at.position, true, emit);
	topple.angle += topple.velocity;
	Crush(topple, at, HalfPi - topple.angle, random, emit);
	bool flat = false;
	if (topple.angle >= HalfPi)
	{
		topple.velocity -= topple.angle - HalfPi;
		topple.angle = HalfPi;
		Crush(topple, at, 0, random, emit);
		PlayPhase(at, TopplePhase::Final, at.position, random, emit);
		PlayEffect(topple, how.doneEffect, at.position, true, emit);
		flat = true;
	}
	if (at.tick >= topple.burstTick)
		Burst(topple, at, random, emit);
	if (flat)
		topple.state = StructureToppleState::Done;
	return flat;
}
}
