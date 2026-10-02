export module games.generalszh.content.objects.model_states;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.objects.model_draw;
import games.generalszh.content.objects.model_conditions;
import engine.config.binding.values;

// Every look an object's model draw module has: each ConditionState (a copy
// of the DefaultConditionState with its own changes) with the condition bit
// sets it is chosen for (its own and its AliasConditionStates), and the
// original's choice among them for a set of current conditions: the state
// matching the most current conditions, then the one with the fewest
// conditions not current (SparseMatchFinder).
export namespace generalszh::content
{
// PRIMARY, SECONDARY, TERTIARY as 0, 1, 2 (3: none).
inline std::size_t SlotIndex(std::string_view slot) noexcept
{
	return slot == "PRIMARY" ? 0 : slot == "SECONDARY" ? 1 : slot == "TERTIARY" ? 2 : 3;
}

using ConditionBits = std::array<std::uint64_t, 2>;

struct ModelState
{
	std::vector<ConditionBits> conditions;
	std::string model;
	std::vector<std::string> animations; // one is picked per play
	// Each animation's distanceCovered: moving, its clip is played in the time the object takes to cover it (0: none).
	std::vector<Engine::Math::Fixed> distances;
	ModelAnimationMode animationMode{ModelAnimationMode::Once};
	bool idleAnimation{false};
	// Where its animation starts (Flags: RANDOMSTART, START_FRAME_FIRST, START_FRAME_LAST; else the first frame, the
	// last going backwards) and how fast it runs (AnimationSpeedFactorRange: one factor picked per play).
	enum class StartFrame : std::uint8_t
	{
		Natural,
		Random,
		First,
		Last,
	};
	StartFrame startFrame{StartFrame::Natural};
	// Flags MAINTAIN_FRAME_ACROSS_STATES(2, 3, 4) as bits 0..3: taken from a state sharing one of them, its
	// animation goes on from where that one's was; RESTART_ANIM_WHEN_COMPLETE: it starts over once it finishes.
	std::uint8_t maintainFrame{0};
	bool restartWhenComplete{false};
	bool adjustHeightByConstruction{false}; // ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT: sunk by what is left to build
	// How it changes to other states (lowercase keys): TransitionKey names it for transitions, and
	// WaitForStateToFinishIfPossible lets the state keyed so finish its animation before this one shows.
	std::string transitionKey;
	std::string waitKey;
	// A transition state (TransitionState = from to: played once between states keyed so); never picked by
	// conditions (it has none).
	std::string transitionFrom;
	std::string transitionTo;

	bool IsTransition() const noexcept { return !transitionFrom.empty(); }
	Engine::Math::Fixed speedMin{Engine::Math::Fixed::One()};
	Engine::Math::Fixed speedMax{Engine::Math::Fixed::One()};
	std::vector<std::string> hiddenSubObjects;
	std::vector<std::string> shownSubObjects;
	// Sub-objects shown only as a weapon fires ("WeaponMuzzleFlash = PRIMARY MuzzleFX": names starting so).
	std::vector<std::string> muzzleFlashes;
	// The PRIMARY weapon's barrel bone names (WeaponFireFXBone, WeaponRecoilBone,
	// WeaponMuzzleFlash, WeaponLaunchBone): each barrel is NAME01, NAME02...
	std::string fireFxBone;
	std::string recoilBone;
	std::string muzzleBone;
	std::string launchBone;
	// Every weapon slot's (PRIMARY, SECONDARY, TERTIARY) barrel bones the same way.
	std::array<std::string, 3> slotFireFxBones;
	std::array<std::string, 3> slotRecoilBones;
	std::array<std::string, 3> slotMuzzleBones;
	std::array<std::string, 3> slotLaunchBones;
	// WeaponHideShowBone: the one sub-object shown while a slot's clip is full (else its loaded projectiles are the
	// launch bone's NAME01, NAME02...).
	std::array<std::string, 3> slotHideShowBones;
	// The bones its turret turns ("Turret") and pitches ("TurretPitch") by
	// the turret's aim, plus their art offsets ("TurretArtAngle/Pitch").
	std::string turretBone;
	std::string turretPitchBone;
	Engine::Math::TurnAngle turretArtAngle;
	Engine::Math::TurnAngle turretArtPitch;
	// The second turret's bones (AltTurret / AltTurretPitch) and art offsets (AltTurretArtAngle/Pitch).
	std::string altTurretBone;
	std::string altTurretPitchBone;
	Engine::Math::TurnAngle altTurretArtAngle;
	Engine::Math::TurnAngle altTurretArtPitch;
	// Particle systems running while in this state ("ParticleSysBone = Bone System"),
	// at a bone of the model ("None": the object's origin); a state keeps its default's.
	struct ParticleBone
	{
		std::string bone; // empty: the origin
		std::string system;
		Engine::Math::FixedVector3 offset{}; // with no bone: where in the object's frame (HelicopterSlowDeath AttachParticleLoc)
	};
	std::vector<ParticleBone> particleBones;
};

// How its barrels recoil (W3DModelDraw InitialRecoilSpeed, MaxRecoilDistance,
// RecoilDamping, RecoilSettleSpeed), per frame as the original stores them.
struct RecoilSettings
{
	Engine::Math::Fixed initial{Engine::Math::Fixed::FromInt(2)};
	Engine::Math::Fixed max{Engine::Math::Fixed::FromInt(3)};
	Engine::Math::Fixed damping{Engine::Math::Fixed::FromRatio(2, 5)};
	Engine::Math::Fixed settle{Engine::Math::Fixed::FromRatio(65, 1000)};
};

struct ModelStates
{
	std::string drawType;
	RecoilSettings recoil;
	// AnimationsRequirePower: an underpowered object's animations pause (the original's default: yes).
	bool animationsRequirePower{true};
	bool receivesDynamicLights{true}; // ReceivesDynamicLights
	// MinLODRequired: the lowest static detail level (0 Low .. 4 Custom) the draw module is made at while draw module
	// LOD is on (W3DModelDrawModuleData m_minLODRequired: Low by default).
	std::int32_t minLodRequired{0};
	// AttachToBoneInAnotherModule (lower-cased, parseAsciiStringLC): drawn at that bone of its drawable as it is now (the
	// object's own model first: a Technical's gun on its chassis' Dum_Turret), not at the object (none: at the object).
	std::string attachToBone;
	// OkToChangeModelColor: its model takes a new indicator colour later (a capture, a script's colour); without, it keeps
	// the one it was made with (W3DModelDraw::replaceIndicatorColor).
	bool okToChangeColor{false};
	std::string trackMarks; // TrackMarks: the texture of the tracks it leaves (none: it leaves none)
	// ProjectileBoneFeedbackEnabledSlots: the slots (bit per PRIMARY, SECONDARY, TERTIARY) whose loaded projectiles show.
	std::uint8_t projectileFeedbackSlots{0};
	std::vector<ModelState> states;
	// Conditions this draw module never switches on (IgnoreConditionStates).
	ConditionBits ignored{};

	bool Empty() const noexcept { return states.empty(); }
};

ModelStates ReadModelStates(const ObjectDefinition &object);

// The object's other model draw modules (drawn with it, each in the state its
// own conditions pick: a combat bike's rider), in module order.
std::vector<ModelStates> ReadExtraModelStates(const ObjectDefinition &object);

// The state index to draw for `current` (0 when there are no states).
std::size_t SelectModelState(const ModelStates &states, ConditionBits current) noexcept;

// The transition state played going from state `from` to state `to` (W3DModelDraw::findTransitionForSig: both
// keyed, the last declared for those keys); none when there is none.
std::optional<std::size_t> FindTransition(const ModelStates &states, std::size_t from, std::size_t to) noexcept;
}

namespace generalszh::content
{
namespace
{
bool IsNone(std::string_view value) noexcept { return value.empty() || value == "NONE" || value == "None" || value == "none"; }

// NameKeys of lowercased names (parseLowercaseNameKey).
std::string Lowercase(std::string_view value)
{
	std::string lower(value);
	for (char &c : lower)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return lower;
}

ConditionBits ReadConditions(const engine::config::Node &node)
{
	ConditionBits bits{};
	for (const std::string_view name : node.values)
		if (const std::uint32_t bit = ModelConditionBit(name); bit != NoCondition)
			bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
	return bits;
}

void ReadState(const engine::config::Node &block, ModelState &state)
{
	bool ownAnimations = false;
	for (const engine::config::Node &child : block.children)
	{
		if (child.key == "Model")
			state.model = IsNone(child.Value()) ? std::string{} : std::string(child.Value());
		else if (child.key == "Animation" || child.key == "IdleAnimation")
		{
			// The first of a state's own animations replaces those copied from the default.
			if (!ownAnimations)
			{
				state.animations.clear();
				state.distances.clear();
			}
			ownAnimations = true;
			// "name distanceCovered timesToRepeat": listed that many times over, it is picked that much more often.
			std::int64_t times = 1;
			if (child.values.size() >= 3)
				times = std::max<std::int64_t>(engine::config::values::ParseInt(child.values[2]).value_or(1), 1);
			const Engine::Math::Fixed distance =
				child.values.size() >= 2 ? engine::config::values::ParseFixed(child.values[1]).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
			if (!IsNone(child.Value()))
				for (std::int64_t copy = 0; copy < times; ++copy)
				{
					state.animations.emplace_back(child.Value());
					state.distances.push_back(distance);
				}
			state.idleAnimation = child.key == "IdleAnimation";
		}
		else if (child.key == "AnimationSpeedFactorRange" && child.values.size() >= 2)
		{
			state.speedMin = engine::config::values::ParseFixed(child.values[0]).value_or(Engine::Math::Fixed::One());
			state.speedMax = engine::config::values::ParseFixed(child.values[1]).value_or(state.speedMin);
		}
		else if (child.key == "Flags")
		{
			state.startFrame = ModelState::StartFrame::Natural;
			state.maintainFrame = 0;
			state.restartWhenComplete = false;
			for (const std::string_view flag : child.values)
				if (flag == "MAINTAIN_FRAME_ACROSS_STATES")
					state.maintainFrame |= 1u;
				else if (flag.starts_with("MAINTAIN_FRAME_ACROSS_STATES") && flag.size() == 29 && flag.back() >= '2' && flag.back() <= '4')
					state.maintainFrame |= static_cast<std::uint8_t>(1u << (flag.back() - '1'));
				else if (flag == "RESTART_ANIM_WHEN_COMPLETE")
					state.restartWhenComplete = true;
				else if (flag == "ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT")
					state.adjustHeightByConstruction = true;
				else if (flag == "RANDOMSTART")
					state.startFrame = ModelState::StartFrame::Random;
				else if (flag == "START_FRAME_FIRST" && state.startFrame != ModelState::StartFrame::Random)
					state.startFrame = ModelState::StartFrame::First;
				else if (flag == "START_FRAME_LAST" && state.startFrame == ModelState::StartFrame::Natural)
					state.startFrame = ModelState::StartFrame::Last;
		}
		else if (child.key == "AnimationMode")
		{
			static constexpr std::array<std::string_view, 6> names{"MANUAL", "LOOP", "ONCE", "LOOP_PINGPONG", "LOOP_BACKWARDS", "ONCE_BACKWARDS"};
			for (std::size_t index = 0; index < names.size(); ++index)
				if (child.Value() == names[index])
					state.animationMode = static_cast<ModelAnimationMode>(index);
		}
		else if (child.key == "TransitionKey")
			state.transitionKey = Lowercase(child.Value());
		else if (child.key == "WaitForStateToFinishIfPossible")
			state.waitKey = Lowercase(child.Value());
		else if (child.key == "HideSubObject")
			state.hiddenSubObjects.insert(state.hiddenSubObjects.end(), child.values.begin(), child.values.end());
		else if (child.key == "ShowSubObject")
			state.shownSubObjects.insert(state.shownSubObjects.end(), child.values.begin(), child.values.end());
		else if (child.key == "ParticleSysBone" && child.values.size() >= 2 && !IsNone(child.values[1]))
			state.particleBones.push_back({IsNone(child.values[0]) ? std::string{} : std::string(child.values[0]), std::string(child.values[1])});
		else if (child.key == "WeaponMuzzleFlash" && child.values.size() >= 2)
		{
			state.muzzleFlashes.emplace_back(child.values[1]);
			if (child.values[0] == "PRIMARY")
				state.muzzleBone = std::string(child.values[1]);
			if (const std::size_t slot = SlotIndex(child.values[0]); slot < 3)
				state.slotMuzzleBones[slot] = std::string(child.values[1]);
		}

		else if (child.key == "WeaponHideShowBone" && child.values.size() >= 2 && SlotIndex(child.values[0]) < 3)
			state.slotHideShowBones[SlotIndex(child.values[0])] = std::string(child.values[1]);
		else if ((child.key == "WeaponFireFXBone" || child.key == "WeaponRecoilBone" || child.key == "WeaponLaunchBone") && child.values.size() >= 2 &&
			SlotIndex(child.values[0]) < 3)
		{
			const std::size_t slot = SlotIndex(child.values[0]);
			(child.key == "WeaponFireFXBone" ? state.slotFireFxBones : child.key == "WeaponRecoilBone" ? state.slotRecoilBones : state.slotLaunchBones)[slot] =
				std::string(child.values[1]);
			if (slot == 0)
				(child.key == "WeaponFireFXBone" ? state.fireFxBone : child.key == "WeaponRecoilBone" ? state.recoilBone : state.launchBone) =
					std::string(child.values[1]);
		}
		else if (child.key == "Turret")
			state.turretBone = IsNone(child.Value()) ? std::string{} : std::string(child.Value());
		else if (child.key == "TurretPitch")
			state.turretPitchBone = IsNone(child.Value()) ? std::string{} : std::string(child.Value());
		else if (child.key == "AltTurret")
			state.altTurretBone = IsNone(child.Value()) ? std::string{} : std::string(child.Value());
		else if (child.key == "AltTurretPitch")
			state.altTurretPitchBone = IsNone(child.Value()) ? std::string{} : std::string(child.Value());
		else if (child.key == "AltTurretArtAngle" || child.key == "AltTurretArtPitch")
		{
			if (const auto degrees = engine::config::values::ParseFixed(child.Value()))
				(child.key == "AltTurretArtAngle" ? state.altTurretArtAngle : state.altTurretArtPitch) = Engine::Math::TurnFromDegrees(*degrees);
		}
		else if (child.key == "TurretArtAngle" || child.key == "TurretArtPitch")
		{
			if (const auto degrees = engine::config::values::ParseFixed(child.Value()))
				(child.key == "TurretArtAngle" ? state.turretArtAngle : state.turretArtPitch) = Engine::Math::TurnFromDegrees(*degrees);
		}
	}
}

bool IsModelDraw(const ModuleEntry &module)
{
	if (module.slot != ModuleSlot::Draw || module.block == nullptr)
		return false;
	for (const engine::config::Node &child : module.block->children)
		if (child.key == "ConditionState" || child.key == "DefaultConditionState")
			return true;
	return false;
}
}

namespace
{
ModelStates ReadDrawStates(const ModuleEntry &module)
{
	ModelStates result;
	{
		result.drawType = module.type;
		std::optional<std::size_t> defaults;
		for (const engine::config::Node &child : module.block->children)
		{
			// Recoil speeds are per second in the INI, per frame (30 a second) in the original.
			const auto perFrame = [&](Engine::Math::Fixed &out, bool speed) {
				if (const auto value = engine::config::values::ParseFixed(child.Value()))
					out = speed ? *value / Engine::Math::Fixed::FromInt(30) : *value;
			};
			if (child.key == "InitialRecoilSpeed")
				perFrame(result.recoil.initial, true);
			else if (child.key == "MaxRecoilDistance")
				perFrame(result.recoil.max, false);
			else if (child.key == "RecoilDamping")
				perFrame(result.recoil.damping, false);
			else if (child.key == "RecoilSettleSpeed")
				perFrame(result.recoil.settle, true);
			else if (child.key == "IgnoreConditionStates")
				result.ignored = ReadConditions(child);
			else if (child.key == "OkToChangeModelColor")
				result.okToChangeColor = engine::config::values::ParseBool(child.Value()).value_or(false);
			else if (child.key == "MinLODRequired" && !child.values.empty())
			{
				// INI::parseStaticGameLODLevel: one of StaticGameLODNames, case blind.
				constexpr std::array<std::string_view, 5> levels{"LOW", "MEDIUM", "HIGH", "VERYHIGH", "CUSTOM"};
				std::string upper(child.Value());
				for (char &c : upper)
					c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				for (std::size_t level = 0; level < levels.size(); ++level)
					if (upper == levels[level])
						result.minLodRequired = static_cast<std::int32_t>(level);
			}
			else if (child.key == "AttachToBoneInAnotherModule" && !child.values.empty())
			{
				result.attachToBone = std::string(child.Value());
				for (char &c : result.attachToBone)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			else if (child.key == "AnimationsRequirePower")
				result.animationsRequirePower = engine::config::values::ParseBool(child.Value()).value_or(true);
			else if (child.key == "ProjectileBoneFeedbackEnabledSlots")
			{
				result.projectileFeedbackSlots = 0;
				for (const std::string_view token : child.values)
					if (const std::size_t slot = SlotIndex(token); slot < 3)
						result.projectileFeedbackSlots |= static_cast<std::uint8_t>(1u << slot);
			}
			else if (child.key == "ReceivesDynamicLights")
				result.receivesDynamicLights = engine::config::values::ParseBool(child.Value()).value_or(true);
			else if (child.key == "TrackMarks")
			{
				// parseAsciiStringLC: lower case.
				result.trackMarks = std::string(child.Value());
				for (char &c : result.trackMarks)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			else if (child.key == "DefaultConditionState")
			{
				ModelState state;
				ReadState(child, state);
				state.conditions.push_back({});
				defaults = result.states.size();
				result.states.push_back(std::move(state));
			}
			else if (child.key == "ConditionState")
			{
				ModelState state = defaults ? result.states[*defaults] : ModelState{};
				state.conditions = {ReadConditions(child)};
				ReadState(child, state);
				result.states.push_back(std::move(state));
			}
			// A transition: the default state's look (without its keys) as the block changes it.
			else if (child.key == "TransitionState" && child.values.size() >= 2)
			{
				ModelState state = defaults ? result.states[*defaults] : ModelState{};
				state.conditions.clear();
				state.transitionKey.clear();
				state.waitKey.clear();
				state.transitionFrom = Lowercase(child.values[0]);
				state.transitionTo = Lowercase(child.values[1]);
				ReadState(child, state);
				result.states.push_back(std::move(state));
			}
			else if (child.key == "AliasConditionState" && !result.states.empty() && !result.states.back().IsTransition())
				result.states.back().conditions.push_back(ReadConditions(child));
		}
	}
	return result;
}
}

ModelStates ReadModelStates(const ObjectDefinition &object)
{
	// The first model draw module is the object's own look.
	for (const ModuleEntry &module : object.modules)
		if (IsModelDraw(module))
			return ReadDrawStates(module);
	return {};
}

std::vector<ModelStates> ReadExtraModelStates(const ObjectDefinition &object)
{
	std::vector<ModelStates> extra;
	bool first = true;
	for (const ModuleEntry &module : object.modules)
		if (IsModelDraw(module))
		{
			if (!first)
				extra.push_back(ReadDrawStates(module));
			first = false;
		}
	return extra;
}

std::optional<std::size_t> FindTransition(const ModelStates &states, std::size_t from, std::size_t to) noexcept
{
	if (from >= states.states.size() || to >= states.states.size())
		return std::nullopt;
	const std::string &fromKey = states.states[from].transitionKey, &toKey = states.states[to].transitionKey;
	if (fromKey.empty() || toKey.empty())
		return std::nullopt;
	for (std::size_t index = states.states.size(); index-- > 0;)
		if (states.states[index].transitionFrom == fromKey && states.states[index].transitionTo == toKey)
			return index;
	return std::nullopt;
}

std::size_t SelectModelState(const ModelStates &states, ConditionBits current) noexcept
{
	current[0] &= ~states.ignored[0];
	current[1] &= ~states.ignored[1];
	std::size_t best = 0;
	int bestYes = 0, bestExtra = 999;
	for (std::size_t index = 0; index < states.states.size(); ++index)
		for (const ConditionBits &wanted : states.states[index].conditions)
		{
			const int yes = std::popcount(current[0] & wanted[0]) + std::popcount(current[1] & wanted[1]);
			const int extra = std::popcount(wanted[0] & ~current[0]) + std::popcount(wanted[1] & ~current[1]);
			if (yes > bestYes || (yes >= bestYes && extra < bestExtra))
			{
				best = index;
				bestYes = yes;
				bestExtra = extra;
			}
		}
	return best;
}
}
