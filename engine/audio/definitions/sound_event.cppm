export module engine.audio.definitions.sound_event;
import std;

// What a named sound is: which files it plays and how (volume, pitch and
// volume variation, delay, looping, how many may play at once, priority),
// where it is heard (2D interface sound, 3D world sound with a range), and
// on which mix bus. Games bind their content onto this; the audio player
// only reads it.
export namespace engine::audio
{
enum class Bus : std::uint8_t
{
	Effects,
	Music,
	Speech,
	Ambient,
	Interface,
	Count,
};

enum class SoundPriority : std::uint8_t
{
	Lowest,
	Low,
	Normal,
	High,
	Critical,
};

// Where and to whom a sound plays.
namespace sound_type
{
inline constexpr std::uint32_t Interface = 1u << 0; // 2D, not positioned
inline constexpr std::uint32_t World = 1u << 1;     // 3D, at the emitter
inline constexpr std::uint32_t Shrouded = 1u << 2;  // heard even under the shroud
inline constexpr std::uint32_t Global = 1u << 3;    // heard at any distance
inline constexpr std::uint32_t Voice = 1u << 4;     // a unit's voice (selection and orders)
inline constexpr std::uint32_t Player = 1u << 5;    // only for the local player's own
inline constexpr std::uint32_t Allies = 1u << 6;
inline constexpr std::uint32_t Enemies = 1u << 7;
inline constexpr std::uint32_t Everyone = 1u << 8;
}

// How the sound list is played.
namespace sound_control
{
inline constexpr std::uint32_t Loop = 1u << 0;
inline constexpr std::uint32_t Random = 1u << 1;    // a random sound of the list (else in order)
inline constexpr std::uint32_t All = 1u << 2;       // every sound of the list, one after another
inline constexpr std::uint32_t PostDelay = 1u << 3; // the delay comes after each sound, not before
inline constexpr std::uint32_t Interrupt = 1u << 4; // at the limit, the new one stops the oldest
}

struct SoundEventDefinition
{
	std::string name;
	Bus bus{Bus::Effects};
	// A single file (music, speech) or, when empty, the sound list.
	std::string filename;
	std::vector<std::string> sounds;
	std::vector<std::string> soundsMorning;
	std::vector<std::string> soundsEvening;
	std::vector<std::string> soundsNight;
	// Played before and after the looping part (e.g. an engine starting / stopping).
	std::vector<std::string> attack;
	std::vector<std::string> decay;
	float volume{1.0f};
	float volumeShift{0.0f}; // random reduction up to this fraction
	float minVolume{0.0f};   // quieter than this is not worth a voice
	float pitchMin{1.0f};
	float pitchMax{1.0f};
	std::uint32_t delayMinMs{0};
	std::uint32_t delayMaxMs{0};
	// At most this many instances at once (0: no limit).
	std::uint32_t limit{0};
	// Times through the list when looping (0: forever).
	std::uint32_t loopCount{0};
	SoundPriority priority{SoundPriority::Normal};
	std::uint32_t type{sound_type::World | sound_type::Everyone};
	std::uint32_t control{0};
	float minRange{0.0f};
	float maxRange{1000.0f};
	float lowPassCutoff{0.0f};

	bool Loops() const noexcept { return (control & sound_control::Loop) != 0; }
	bool Positional() const noexcept { return (type & sound_type::World) != 0 && (type & sound_type::Interface) == 0; }
};
}
