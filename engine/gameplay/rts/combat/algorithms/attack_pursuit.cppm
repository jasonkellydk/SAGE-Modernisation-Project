export module engine.gameplay.rts.combat.algorithms.attack_pursuit;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.rts.navigation.algorithms.attack_view;

// The attack's chase rules as free functions over plain values (the callers gather them): AIStates.cpp's canPursue,
// wantToSquishTarget and isSamePosition (AIAttackPursueTargetState). (The view rules it uses are attack_view's.)
export namespace engine::gameplay
{
// What canPursue reads of the pursuer and its victim.
struct ChaseView
{
	bool turret{false};      // its current weapon is on a turret (getWhichTurretForCurWeapon)
	bool computer{false};    // its player is a computer player
	bool aiCrushes{true};    // AICrushesInfantry
	bool canCrush{false};    // Object::canCrushOrSquish(victim)
	bool tooClose{false};    // Weapon::isTooClose
	Engine::Math::Fixed ourMaxSpeed; // getCurLocomotorSpeed
	bool victimPhysics{false};       // victim->getPhysics(): it moves
	Engine::Math::Fixed victimSpeed; // getForwardSpeed2D
	Engine::Math::FixedVector2 toVictim;    // victim - pursuer
	Engine::Math::FixedVector2 victimHeading; // getUnitDirectionVector2D
};

// AIStates.cpp canPursue: a moving victim (with physics), a turret weapon; a computer player's crusher always pursues what
// it can crush (AICrushesInfantry); else not when too close, nor a victim as fast as it or under a tenth of its speed,
// nor one coming at it.
inline bool CanPursue(const ChaseView &view) noexcept
{
	if (!view.victimPhysics || !view.turret)
		return false;
	if (view.aiCrushes && view.computer && view.canCrush)
		return true;
	if (view.tooClose)
		return false;
	if (view.victimSpeed >= view.ourMaxSpeed)
		return false;
	if (view.victimSpeed < view.ourMaxSpeed / Engine::Math::Fixed::FromInt(10))
		return false;
	return Engine::Math::Dot(view.toVictim, view.victimHeading) >= Engine::Math::Fixed{};
}

// wantToSquishTarget: a victim not inside anything, its weapon on a turret, AICrushesInfantry, a computer player's, one it
// can crush or squish, and its kind not KINDOF_DONT_AUTO_CRUSH_INFANTRY.
inline bool WantToSquish(bool contained, bool turret, bool aiCrushes, bool computer, bool canCrush, bool autoCrush) noexcept
{
	return !contained && turret && aiCrushes && computer && canCrush && autoCrush;
}

// isSamePosition: the victim has moved no more than a tenth of its distance from the pursuer (2D, squared).
inline bool SamePosition(Engine::Math::FixedVector2 ours, Engine::Math::FixedVector2 previous, Engine::Math::FixedVector2 current) noexcept
{
	const Engine::Math::FixedVector2 moved = current - previous;
	const Engine::Math::FixedVector2 toTarget = current - ours;
	const Engine::Math::Fixed tolerance = Engine::Math::Dot(toTarget, toTarget) / Engine::Math::Fixed::FromInt(100);
	return Engine::Math::Dot(moved, moved) <= tolerance;
}

// AIAttackPursueTargetState's MIN_RECOMPUTE_TIME: it works its way out again no sooner than this many frames on.
inline constexpr std::uint64_t PursuitRecomputeTicks = 10;
}
