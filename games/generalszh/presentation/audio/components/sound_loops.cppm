export module games.generalszh.presentation.audio.components.sound_loops;
import std;

import engine.ecs.core.component_registry;

// The sounds an object keeps playing while it is seen, as a side table on
// the simulation's own entity: its ambient loop, its movement loop, its
// burning loop and a crash's loop (0: not playing), and whether it was
// moving last frame (its move-start sound plays as it sets off).
export namespace generalszh::presentation
{
struct SoundLoops
{
	std::uint64_t ambient{0};
	std::uint64_t move{0};
	std::uint64_t burning{0};
	std::uint64_t crashing{0};
	std::uint64_t turret{0}; // its turret's rotation loop
	std::uint64_t afterburner{0}; // its afterburners' loop
	std::uint64_t powerslide{0};  // its powerslide sound
	std::uint64_t construction{0}; // its UnderConstruction loop, while a builder works on it
	std::uint32_t wasMoving{0};
	std::uint32_t stealth{0xFFFFFFFFu}; // its stealthed (1) and detected (2) bits as last heard (unknown: none yet)
	std::uint32_t burnerLit{0}; // its afterburners were lit last frame
	std::uint32_t sliding{0};   // it was powersliding last frame
	std::uint32_t circling{0};  // it was circling a dead airfield last frame
	std::uint32_t moveLoop{0}; // which move loop this move plays: 0 none, 1 SoundMoveLoop, 2 SoundMoveLoopDamaged
	std::uint32_t damage{0xFFFFFFFFu}; // the damage state its ambient sound is for (0 pristine .. 3 rubble)
	std::uint32_t seenFrame{0};
	// DISABLE_OBJECT_SOUND / ENABLE_OBJECT_SOUND (Drawable::enableAmbientSoundFromScript): its ambient kept off, and a
	// start asked for (startAmbientSound: once, a one-shot ambient too).
	std::uint32_t scriptOff{0};
	std::uint32_t scriptStart{0};
	// Its contain's last entry and exit ticks as last heard (Transport enteredTick / doorOpenedTick; Unheard: not yet seen).
	static constexpr std::uint64_t Unheard = ~std::uint64_t{0};
	std::uint64_t entered{Unheard};
	std::uint64_t exited{Unheard};
};

// An armed thing's looping fire sound as playing (FiringTracker::m_audioHandle; 0: none), the shot it was last kept
// going for, and its continuous fire as last heard (its VoiceRapidFire as it goes FAST).
struct FireSoundLoop
{
	std::uint64_t handle{0};
	std::uint64_t firedTick{0};
	std::uint32_t level{0};
	std::uint32_t reserved{0};
};

// The unit voice an object is saying (AudioEventRTS::setObjectID on a voice event; 0: none): SoundManager::violatesVoice
// lets no other voice of it start while this one plays.
struct SpeakingVoice
{
	std::uint64_t handle{0};
};

// A locomotive's RunningSound as playing (RailroadBehavior's m_runningSound; 0: none).
struct TrainSoundLoop
{
	std::uint64_t handle{0};
};

// A special ability's PrepSoundLoop as playing on the unit (SpecialAbilityUpdate::m_prepSoundLoop's handle; 0: none), and
// which start of it (PrepSoundCue::started) it plays.
struct PrepSoundLoop
{
	std::uint64_t handle{0};
	std::uint64_t heard{0};
};

// A Strategy Center's plan sound as playing (BattlePlanUpdate's m_*Unpack / m_searchAndDestroyIdle / m_*Pack handles;
// 0: none) and for which status and plan (0: none yet).
struct BattlePlanSound
{
	std::uint64_t handle{0};
	std::uint32_t key{0};
	std::uint32_t reserved{0};
};

// A missile launcher building's DoorOpenIdleAudio as playing (MissileLauncherBuildingUpdate::m_openIdleAudio; 0: none) and
// whether its door was open when last heard.
struct DoorIdleSound
{
	std::uint64_t handle{0};
	std::uint32_t open{0};
	std::uint32_t reserved{0};
};

// A Particle Cannon uplink's four sound loops as playing (0: none), and which start of each they play (UplinkEffects).
struct UplinkSounds
{
	std::array<std::uint64_t, 4> handles{};
	std::array<std::uint32_t, 4> heard{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::SoundLoops>
{
	static constexpr std::string_view StableName = "generalszh.presentation.sound_loops";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::SpeakingVoice>
{
	static constexpr std::string_view StableName = "generalszh.presentation.speaking_voice";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::FireSoundLoop>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fire_sound_loop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::TrainSoundLoop>
{
	static constexpr std::string_view StableName = "generalszh.presentation.train_sound_loop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::PrepSoundLoop>
{
	static constexpr std::string_view StableName = "generalszh.presentation.prep_sound_loop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::BattlePlanSound>
{
	static constexpr std::string_view StableName = "generalszh.presentation.battle_plan_sound";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::DoorIdleSound>
{
	static constexpr std::string_view StableName = "generalszh.presentation.door_idle_sound";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::UplinkSounds>
{
	static constexpr std::string_view StableName = "generalszh.presentation.uplink_sounds";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
