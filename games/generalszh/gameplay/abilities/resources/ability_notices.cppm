export module games.generalszh.gameplay.abilities.resources.ability_notices;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// What the logic told the presentation this tick about abilities and powers (the original's audio and drawable calls from
// the logic), in order. Per tick: not saved.
//   AbilityNotice: a special ability's steps (SpecialAbilityUpdate): unpacking began (UnpackSound), packing began
//   (PackSound; `success`: after its effect, the unit's task-complete voice), its effect triggered (TriggerSound), its
//   preparation began or ended (PrepSoundLoop starts, stops); the power is its SpecialPower template.
//   PowerTrigger: a special power fired (SpecialPowerModule::aboutToDoSpecialPower: its InitiateSound on the source, its
//   InitiateAtLocationSound where it lands, when it has somewhere).
//   defected: things that went over to another team (Object::defect: its VoiceDefect, a flash as if selected and the
//   defector timer's tick).
export namespace generalszh::gameplay
{
enum class AbilityCue : std::uint8_t
{
	Unpack,
	Pack,
	Trigger,
	PreparationStart,
	PreparationEnd,
};

struct AbilityNotice
{
	ecs::Entity unit;
	std::uint32_t power{0};
	AbilityCue cue{AbilityCue::Unpack};
	bool success{false};
};

struct PowerTrigger
{
	ecs::Entity source;
	std::uint32_t power{0};
	std::uint32_t player{0};
	std::optional<Engine::Math::FixedVector3> at;
};

struct AbilityNotices
{
	std::vector<AbilityNotice> abilities;
	std::vector<PowerTrigger> powers;
	std::vector<ecs::Entity> defected;
	std::vector<ecs::Entity> hijacks; // hijackers that took a vehicle (the HijackDriver sound on them)
	// Buildings sabotaged (CrateCollide::doSabotageFeedbackFX: its MiscAudio sound on it and a selection flash).
	struct Sabotage
	{
		enum class Sound : std::uint8_t
		{
			None,       // a fake building: no more feedback
			ResetTimer, // SabotageResetTimeBuilding
			Withdraw,   // MoneyWithdrawSound
			ShutDown,   // SabotageShutDownBuilding
		};
		ecs::Entity building;
		Sound sound{Sound::None};
	};
	std::vector<Sabotage> sabotages;
	// A hack's disable effect (SpecialAbilityUpdate::triggerAbilityEffect, SPECIAL_HACKER_DISABLE_BUILDING and
	// SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK, while m_doDisableFXParticles): the unit's DisableFXParticleSystem for its
	// power, riding on the target somewhere over its footprint, emitting for `ticks` (EffectDuration, doubled for a
	// small building).
	struct DisableFx
	{
		ecs::Entity unit;
		ecs::Entity target;
		std::uint32_t power{0};
		std::uint64_t ticks{0};
	};
	std::vector<DisableFx> disableFx;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AbilityNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ability_notices";
};
}
