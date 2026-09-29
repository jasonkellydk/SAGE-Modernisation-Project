export module games.generalszh.presentation.objects.components.object_presentation;
import std;

import engine.ecs.core.component_registry;

// How a visible object is presented, as side tables on the simulation's own
// entities (never in the archetypes, the state hash or checkpoints): where
// it was at the last two ticks (drawn between them), and the look it shows
// and since when on the presentation clock (its animation runs from then).
export namespace generalszh::presentation
{
struct TickPose
{
	std::array<float, 3> previous{};
	std::array<float, 3> current{};
	std::uint32_t previousFacing{0}; // turn units
	std::uint32_t currentFacing{0};
	std::uint32_t reserved{0};
};

// Its weapon as the last two ticks left it: its turret's turn and pitch
// (radians, relative to the body) and whether it fired in the last tick
// (its muzzle flashes show until the next, as the original's one frame).
struct WeaponPose
{
	float previousTurret{0.0f};
	float currentTurret{0.0f};
	float previousPitch{0.0f};
	float currentPitch{0.0f};
	std::uint64_t firedTick{0}; // the shot last seen
	std::uint32_t flash{0};
	std::uint32_t barrel{0};    // the barrel that fired it
	std::uint32_t turning{0};   // its turret turned or pitched in the last tick
	std::uint32_t reserved{0};
	// Its second turret (AltTurret), as the first.
	float previousAltTurret{0.0f};
	float currentAltTurret{0.0f};
	float previousAltPitch{0.0f};
	float currentAltPitch{0.0f};
	// Each slot's loaded projectiles hidden as of this tick (Weapon::getRemainingAmmo: none while its clip reloads).
	std::array<std::uint8_t, 3> projectilesHidden{};
};

// Its barrels' recoil and flashes, as W3DModelDraw::handleClientRecoil steps
// them once a (legacy, 30 a second) frame: a shot starts its barrel (RECOIL_START,
// its flash showing) moving back at the initial speed, slowing by the damping
// each frame until it reaches the maximum or nearly stops, then it settles
// forward (SETTLE) and rests (IDLE). A barrel without a recoil bone flashes
// for one frame.
struct BarrelRecoil
{
	static constexpr std::uint32_t MaxBarrels = 8;
	enum State : std::uint8_t
	{
		Idle,
		Start,
		Settle,
	};
	std::array<float, MaxBarrels> shift{};
	std::array<float, MaxBarrels> rate{};
	std::array<std::uint8_t, MaxBarrels> state{};
	std::uint64_t firedTick{0}; // the shot last started
	float frames{0.0f};         // legacy frames not yet stepped
	std::uint32_t reserved{0};
};

struct RecoilMotion
{
	float initial{2.0f};
	float max{3.0f};
	float damping{0.4f};
	float settle{0.065f};
	bool bones{false}; // its barrels have recoil bones
};

// One legacy frame of `barrel`'s recoil.
inline void StepRecoil(BarrelRecoil &recoil, std::uint32_t barrel, const RecoilMotion &motion) noexcept
{
	float &shift = recoil.shift[barrel];
	float &rate = recoil.rate[barrel];
	std::uint8_t &state = recoil.state[barrel];
	if (!motion.bones)
	{
		state = BarrelRecoil::Idle;
		return;
	}
	if (state == BarrelRecoil::Start)
	{
		shift += rate;
		rate *= motion.damping;
		if (shift >= motion.max)
		{
			shift = motion.max;
			state = BarrelRecoil::Settle;
		}
		else if (std::fabs(rate) < 0.01f)
			state = BarrelRecoil::Settle;
	}
	else if (state == BarrelRecoil::Settle)
	{
		shift -= motion.settle;
		if (shift <= 0.0f)
		{
			shift = 0.0f;
			state = BarrelRecoil::Idle;
		}
	}
}

// A shot from `barrel`: it starts recoiling (and flashing).
inline void StartRecoil(BarrelRecoil &recoil, std::uint32_t barrel, const RecoilMotion &motion) noexcept
{
	recoil.state[barrel] = BarrelRecoil::Start;
	recoil.rate[barrel] = motion.initial;
}

struct ShownLook
{
	double since{0.0}; // presentation clock seconds
	std::uint32_t look{0};
	float speed{1.0f}; // its animation's speed factor (AnimationSpeedFactorRange)
	float start{0.0f}; // where its animation starts, as a share of it (RANDOMSTART; 1: its last frame)
	// While its animation is paused (Drawable::getShouldAnimate): the animation time it holds; below zero: running.
	double held{-1.0};
	// Its draw's model states (W3DModelDraw m_curState / m_nextState): the state last asked for by its
	// conditions, the state whose look shows (a transition, or one finishing its animation first), and the
	// state to show once that one's animation finishes (NoState: none).
	static constexpr std::uint32_t NoState = 0xFFFFFFFFu;
	std::uint32_t state{0};
	std::uint32_t shown{0};
	std::uint32_t pending{NoState};
	// The indicator colour its model was made with (W3DModelDraw's m_hexColor), kept unless its draw may change it
	// (OkToChangeModelColor).
	std::array<float, 4> madeColor{1, 1, 1, 0};
	// What last stretched its animation (setAnimationLoopDuration), as bits: 1 a weapon winding up (PREATTACK), 2 being
	// sold, 4 SOLD; each stretches it once as it comes on.
	std::uint8_t stretched{0};
};
}

export namespace generalszh::presentation
{
// A tree swaying in the breeze (SwayClientUpdate): where its sway is (radians round), how fast it goes (radians a
// logic frame), how far it sways and leans, the angle it stands at, the breeze version it rolled these for, and
// whether it still sways (a burned tree stops).
// Its heat vision (Drawable's second material pass): how strong it glows (1 as a detector finds it or it turns
// detected, fading each frame), the stealth look it last showed (0 normal, 1 seen through by friends, 2 friends' and
// detected, 3 detected by an enemy, 4 hidden), and the detection it last glowed for.
// The power loss (underpowered, EMP, subdued, hacked) it was last heard to have (DisabledSoundSystem).
struct DisableHeard
{
	std::uint32_t mask{0};
};

// Its colour tint (Drawable's m_colorTintEnvelope, TintEnvelope: see tint_envelope), and the tint status it was last
// shown for (TINT_STATUS_DISABLED: `status`; `forced`: blackened for good, an aircraft an EMP pulse killed).
struct TintEnvelope
{
	static constexpr std::uint32_t Rest = 0;
	static constexpr std::uint32_t Attack = 1;
	static constexpr std::uint32_t Decay = 2;
	static constexpr std::uint32_t Sustain = 3;
	std::array<float, 3> attackRate{};
	std::array<float, 3> decayRate{};
	std::array<float, 3> peak{};
	std::array<float, 3> current{};
	float sustain{0.0f};
	std::uint32_t state{Rest};
	std::uint32_t affect{0};
	std::uint32_t status{0};
	std::uint32_t forced{0};
};

// Its selection flash (Drawable's m_selectionFlashEnvelope: flashAsSelected), added to its lights with its tint.
struct SelectionFlash
{
	TintEnvelope envelope;
};

// A script's flashing (NAMED_FLASH, TEAM_FLASH: Drawable's m_flashCount and m_flashColor): how many flashes are left
// (one each DRAWABLE_FRAMES_PER_FLASH, 15, frames: a colorFlash of its tint) and in what colour.
struct ScriptFlash
{
	std::int32_t count{0};
	std::array<float, 3> color{};
};

// A capturer's capture flash phases, by its special ability slot (SpecialAbilityUpdate's m_captureFlashPhase: kept
// from one capture to the next, as the original's).
struct CaptureFlash
{
	std::array<float, 3> phase{};
};

// An undetected defector's timer effects (ObjectDefectionHelper): its flash phase, when its cover ends, and whether it
// was seen covered (its end then flashes and dings when the time ran out).
struct DefectorFlash
{
	float phase{0.0f};
	std::uint32_t seen{0};
	std::uint64_t until{0};
};

struct HeatVision
{
	float opacity{0.0f};
	std::uint32_t look{0};
	std::uint64_t detection{0};
};

// What the viewer last saw of it through the shroud (Drawable::m_shroudClearFrame, PartitionData::m_everSeenByPlayer):
// when it was last clear (game seconds; never: below zero) and whether it was ever at least partly clear.
struct ShroudSight
{
	double lastClear{-1.0e30};
	std::uint32_t everSeen{0};
	std::uint32_t reserved{0};
};

struct TreeSway
{
	float value{0.0f};
	float delta{0.0f};
	float limit{0.0f};
	float lean{0.0f};
	float angle{0.0f};
	std::int32_t version{-1};
	std::uint32_t swaying{1};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::TreeSway>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_sway";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::ShroudSight>
{
	static constexpr std::string_view StableName = "generalszh.presentation.shroud_sight";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::DisableHeard>
{
	static constexpr std::string_view StableName = "generalszh.presentation.disable_heard";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::TintEnvelope>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tint_envelope";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::SelectionFlash>
{
	static constexpr std::string_view StableName = "generalszh.presentation.selection_flash";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::ScriptFlash>
{
	static constexpr std::string_view StableName = "generalszh.presentation.script_flash";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::CaptureFlash>
{
	static constexpr std::string_view StableName = "generalszh.presentation.capture_flash";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::DefectorFlash>
{
	static constexpr std::string_view StableName = "generalszh.presentation.defector_flash";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::HeatVision>
{
	static constexpr std::string_view StableName = "generalszh.presentation.heat_vision";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::TickPose>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tick_pose";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::WeaponPose>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_pose";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::BarrelRecoil>
{
	static constexpr std::string_view StableName = "generalszh.presentation.barrel_recoil";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::ShownLook>
{
	static constexpr std::string_view StableName = "generalszh.presentation.shown_look";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
