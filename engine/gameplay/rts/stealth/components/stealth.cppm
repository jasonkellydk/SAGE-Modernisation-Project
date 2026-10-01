export module engine.gameplay.rts.stealth.components.stealth;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Hiding from enemies (the original's StealthUpdate): an entity that can
// stealth is stealthed once none of its forbidden conditions has held for
// `delay` ticks; detected, it shows until `detectedUntil`. Stealthed and not
// detected, enemies cannot pick it as a target (area damage still finds it).
// A disguiser (DisguisesAsTeam: the bomb truck) stealths only once it takes
// the look and apparent player of another thing (disguiseAsObject), over a
// transition whose halfway point swaps its look; revealed (markAsDetected),
// it loses the disguise over another, and its stealth ends with it.
export namespace engine::gameplay
{
namespace stealth_forbidden
{
// The original's StealthForbiddenConditions, in its bit order.
inline constexpr std::uint32_t Attacking = 1u << 0;
inline constexpr std::uint32_t Moving = 1u << 1;
inline constexpr std::uint32_t UsingAbility = 1u << 2;
inline constexpr std::uint32_t FiringPrimary = 1u << 3;
inline constexpr std::uint32_t FiringSecondary = 1u << 4;
inline constexpr std::uint32_t FiringTertiary = 1u << 5;
inline constexpr std::uint32_t NoBlackMarket = 1u << 6;
inline constexpr std::uint32_t TakingDamage = 1u << 7;
inline constexpr std::uint32_t RidersAttacking = 1u << 8;
inline constexpr std::uint32_t FiringAny = FiringPrimary | FiringSecondary | FiringTertiary;
}

namespace stealth_flag
{
inline constexpr std::uint32_t CanStealth = 1u << 0; // innate, or granted (upgrades, powers)
inline constexpr std::uint32_t Stealthed = 1u << 1;
inline constexpr std::uint32_t Detected = 1u << 2;
// Kept from stealth this tick while in one of its hint conditions (StealthUpdate::hintDetectableWhileUnstealthed):
// its own player sees it flash.
inline constexpr std::uint32_t HintDetectable = 1u << 3;
// OBJECT_STATUS_DISGUISED (m_disguised): its look is the disguise (changeVisualDisguise at a transition's halfway point).
inline constexpr std::uint32_t Disguised = 1u << 4;
// Its update is off (!m_enabled): a disguiser before it takes a disguise, and once it has lost one.
inline constexpr std::uint32_t Off = 1u << 5;
// Its update sleeps till stealth is granted it (GrantedBySpecialPower: UPDATE_SLEEP_FOREVER until receiveGrant).
inline constexpr std::uint32_t Asleep = 1u << 6;
// Its transition is into a disguise (m_transitioningToDisguise), else out of one.
inline constexpr std::uint32_t ToDisguise = 1u << 7;
// Its transition's halfway point is past (m_disguiseHalfpointReached).
inline constexpr std::uint32_t Halfway = 1u << 8;
}

// The original's HintDetectableConditions this port knows: firing a weapon, using an ability.
namespace stealth_hint
{
inline constexpr std::uint32_t FiringWeapon = 1u << 0;
inline constexpr std::uint32_t UsingAbility = 1u << 1;
}

// Its StealthUpdate's switches.
namespace stealth_option
{
inline constexpr std::uint32_t DisguisesAsTeam = 1u << 0;       // DisguisesAsTeam (canDisguise)
inline constexpr std::uint32_t OrderIdleEnemies = 1u << 1;      // OrderIdleEnemiesToAttackMeUponReveal
inline constexpr std::uint32_t GrantedBySpecialPower = 1u << 2; // GrantedBySpecialPower
inline constexpr std::uint32_t UseRiderStealth = 1u << 3;       // UseRiderStealth (calcStealthOwner: its first rider's rules)
}

struct Stealth
{
	static constexpr std::uint32_t NoDisguise = 0xFFFFFFFFu;
	static constexpr std::uint32_t NoTeam = 0xFFFFFFFFu;

	std::uint32_t forbidden{0};
	std::uint32_t flags{0};
	std::uint64_t delay{0};
	std::uint64_t allowedAt{0};
	std::uint64_t detectedUntil{0};
	Engine::Math::Fixed moveThreshold; // per tick
	std::uint32_t hint{0};             // stealth_hint bits
	std::uint32_t options{0};          // stealth_option bits
	Engine::Math::Fixed revealDistance; // RevealDistanceFromTarget (none: 0)
	std::uint32_t disguiseTicks{0};    // DisguiseTransitionTime
	std::uint32_t revealTicks{0};      // DisguiseRevealTransitionTime
	std::uint32_t transitionLeft{0};   // m_disguiseTransitionFrames: ticks left of its transition
	// The disguise it takes or has (m_disguiseAsTemplate: a definition index; NoDisguise: none), whose (m_disguiseAsPlayerIndex:
	// -1 none; 0 as it loses one) and that player's default team (for how others regard it).
	std::uint32_t disguiseAs{NoDisguise};
	std::int32_t disguisePlayer{-1};
	std::uint32_t disguiseTeam{NoTeam};
	// What its look shows (the drawable changeVisualDisguise made): the disguise's definition and player (NoDisguise: its own).
	std::uint32_t shownAs{NoDisguise};
	std::int32_t shownPlayer{-1};
	// m_framesGranted: the ticks left of a temporary grant (GrantTemporaryStealth); 0: none, or a lasting one.
	std::uint32_t framesGranted{0};
	std::uint32_t reserved{0};

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
	void Set(std::uint32_t flag, bool on) noexcept { flags = on ? (flags | flag) : (flags & ~flag); }
	bool Option(std::uint32_t option) const noexcept { return (options & option) != 0; }
	// Hidden from enemies: stealthed and not detected.
	bool Hidden() const noexcept { return Has(stealth_flag::Stealthed) && !Has(stealth_flag::Detected); }
	// OBJECT_STATUS_STEALTHED, not DETECTED and not DISGUISED (the tests of AIStates, BuildAssistant, the actions).
	bool HiddenUndisguised() const noexcept { return Hidden() && !Has(stealth_flag::Disguised); }
	// isDisguised: it has (or is taking) a disguise.
	bool IsDisguised() const noexcept { return disguiseAs != NoDisguise; }
};

// The rules its stealth follows (calcStealthOwner): its own, or its first rider's (UseRiderStealth): the forbidden
// conditions, the delay, whether idle enemies are ordered at it when revealed, and the owner's OBJECT_STATUS_CAN_STEALTH.
struct StealthRules
{
	std::uint64_t delay{0};
	std::uint32_t forbidden{0};
	bool canStealth{false};
	bool orderIdleEnemies{false};
};

inline StealthRules OwnRules(const Stealth &stealth) noexcept
{
	return {stealth.delay, stealth.forbidden, stealth.Has(stealth_flag::CanStealth), stealth.Option(stealth_option::OrderIdleEnemies)};
}

// StealthUpdate::receiveGrant(TRUE, frames): it may stealth from now on and is stealthed at once, its update woken; for
// `frames` ticks only (0: for good). A disguiser takes none. Returns whether it took it.
inline bool ReceiveGrant(Stealth &stealth, std::uint64_t tick, std::uint32_t frames) noexcept
{
	if (stealth.Option(stealth_option::DisguisesAsTeam))
		return false;
	stealth.Set(stealth_flag::CanStealth, true);
	stealth.Set(stealth_flag::Stealthed, true);
	stealth.Set(stealth_flag::Asleep, false);
	stealth.Set(stealth_flag::Off, false);
	stealth.allowedAt = tick;
	stealth.framesGranted = frames;
	return true;
}

// StealthUpdate::receiveGrant(FALSE): its grant is gone: it may no longer stealth (m_stealthAllowedFrame FOREVER) and its
// update sleeps (m_enabled off) till it is granted again.
inline void RevokeGrant(Stealth &stealth) noexcept
{
	if (stealth.Option(stealth_option::DisguisesAsTeam))
		return;
	stealth.Set(stealth_flag::Asleep, true);
	stealth.Set(stealth_flag::CanStealth, false);
	stealth.Set(stealth_flag::Stealthed, false);
	stealth.allowedAt = std::numeric_limits<std::uint64_t>::max();
	stealth.framesGranted = 0;
}

// StealthUpdate::isTemporaryGrant.
inline bool TemporaryGrant(const Stealth &stealth) noexcept
{
	return stealth.framesGranted > 0;
}

// StealthUpdate::disguiseAsObject(target): it takes the disguise of `definition` of `player` (whose default team is
// `team`), over its DisguiseTransitionTime; its update wakes.
inline void DisguiseAs(Stealth &stealth, std::uint32_t definition, std::int32_t player, std::uint32_t team) noexcept
{
	stealth.disguiseAs = definition;
	stealth.disguisePlayer = player;
	stealth.disguiseTeam = team;
	stealth.Set(stealth_flag::Off, false);
	stealth.Set(stealth_flag::ToDisguise, true);
	stealth.Set(stealth_flag::Halfway, false);
	stealth.transitionLeft = stealth.disguiseTicks;
}

// StealthUpdate::disguiseAsObject(nullptr): disguised, it starts losing the disguise over its DisguiseRevealTransitionTime
// (its apparent player 0 till the look goes); still taking one (not yet disguised), nothing changes.
inline void DropDisguise(Stealth &stealth) noexcept
{
	if (!stealth.Has(stealth_flag::Disguised))
		return;
	stealth.disguiseAs = Stealth::NoDisguise;
	stealth.disguisePlayer = 0;
	stealth.disguiseTeam = Stealth::NoTeam;
	stealth.transitionLeft = stealth.revealTicks;
	stealth.Set(stealth_flag::ToDisguise, false);
	stealth.Set(stealth_flag::Halfway, false);
}

// StealthUpdate::markAsDetected(numFrames): a disguise is dropped; it is detected until `ticks` from now (0: its owner's
// stealth delay from now; else never shortened). Returns whether idle enemies are to be woken at it
// (OrderIdleEnemiesToAttackMeUponReveal, the owner's).
inline bool MarkAsDetected(Stealth &stealth, const StealthRules &rules, std::uint64_t tick, std::uint64_t ticks) noexcept
{
	if (stealth.IsDisguised())
		DropDisguise(stealth);
	if (ticks == 0)
		stealth.detectedUntil = tick + rules.delay;
	else if (stealth.detectedUntil < tick + ticks)
		stealth.detectedUntil = tick + ticks;
	return rules.orderIdleEnemies;
}

// What changeVisualDisguise did at a transition's halfway point.
enum class DisguiseChange : std::uint8_t
{
	None,
	Disguised, // the disguise's look taken (DisguiseFX, DisguiseStarted, MODELCONDITION_DISGUISED)
	Revealed,  // its own look back (DisguiseRevealFX, DisguiseRevealedSuccess/Failure)
};

// StealthUpdate::update's disguise transition, one tick: counts down; at the halfway point (1 - left / total >= 0.5) the
// look changes (changeVisualDisguise); a transition out of a disguise, finished, turns the update off with its stealth and
// detection (`ended`).
inline DisguiseChange StepDisguiseTransition(Stealth &stealth, bool &ended) noexcept
{
	ended = false;
	if (stealth.transitionLeft == 0)
		return DisguiseChange::None;
	--stealth.transitionLeft;
	const std::uint64_t total = stealth.Has(stealth_flag::ToDisguise) ? stealth.disguiseTicks : stealth.revealTicks;
	DisguiseChange change = DisguiseChange::None;
	// factor = 1 - left / total, in exact integers: 2 (total - left) >= total.
	if (2 * (total - stealth.transitionLeft) >= total && !stealth.Has(stealth_flag::Halfway))
	{
		if (stealth.disguiseAs != Stealth::NoDisguise)
		{
			stealth.shownAs = stealth.disguiseAs;
			stealth.shownPlayer = stealth.disguisePlayer;
			stealth.Set(stealth_flag::Disguised, true);
			change = DisguiseChange::Disguised;
		}
		else if (stealth.disguisePlayer != -1)
		{
			stealth.disguisePlayer = -1;
			stealth.shownAs = Stealth::NoDisguise;
			stealth.shownPlayer = -1;
			stealth.Set(stealth_flag::Disguised, false);
			change = DisguiseChange::Revealed;
		}
		stealth.Set(stealth_flag::Halfway, true);
	}
	if (stealth.transitionLeft == 0 && !stealth.Has(stealth_flag::ToDisguise))
	{
		stealth.Set(stealth_flag::Off, true);
		stealth.Set(stealth_flag::Stealthed, false);
		stealth.Set(stealth_flag::Detected, false);
		ended = true;
	}
	return change;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Stealth>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
