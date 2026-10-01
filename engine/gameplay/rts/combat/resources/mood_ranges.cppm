export module engine.gameplay.rts.combat.resources.mood_ranges;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// How far an idle unit looks for a victim of its own (AI::getAdjustedVisionRangeForObject with the owner type and mood
// factors): its vision range times the guard outer modifier of its controller (a human's or a computer's); a computer
// player's unit then by its mood (asleep: nowhere; alert and aggressive: times their modifiers). A passive computer
// unit only strikes back at its last attacker, unless that last hit healed it (`healingDamageType`).
export namespace engine::gameplay
{
struct MoodRanges
{
	Engine::Math::Fixed guardOuterHuman{Engine::Math::Fixed::One()};
	Engine::Math::Fixed guardOuterAi{Engine::Math::Fixed::One()};
	Engine::Math::Fixed alert{Engine::Math::Fixed::One()};
	Engine::Math::Fixed aggressive{Engine::Math::Fixed::One()};
	std::uint32_t healingDamageType{0xFFFFFFFFu};
	// The inner guard ranges (AI_VISIONFACTOR_GUARDINNER: GuardInnerModifierHuman / AI), a guard's own look.
	Engine::Math::Fixed guardInnerHuman{Engine::Math::Fixed::One()};
	Engine::Math::Fixed guardInnerAi{Engine::Math::Fixed::One()};
	// TAiData::m_forceIdleFramesCount (ForceIdleMSEC): an idle unit's first look comes this many ticks after it went idle
	// (AIUpdateInterface::resetNextMoodCheckTime from AIIdleState::onEnter).
	std::uint64_t forceIdleTicks{1};
};

// AI::getAdjustedVisionRangeForObject (AI_VISIONFACTOR_OWNERTYPE | MOOD | GUARDINNER or GUARDOUTER): a unit's vision
// by its controller's guard modifier (a human's or a computer's); a computer player's then by its mood (the attitude:
// asleep -2 nowhere, alert 1 and aggressive 2 by their modifiers). The inner one is AIGuardMachine::getStdGuardRange.
inline Engine::Math::Fixed GuardVision(const MoodRanges &moods, Engine::Math::Fixed vision, bool human, bool inner, std::int8_t attitude) noexcept
{
	const Engine::Math::Fixed reach = vision * (inner ? (human ? moods.guardInnerHuman : moods.guardInnerAi) : (human ? moods.guardOuterHuman : moods.guardOuterAi));
	if (human)
		return reach;
	return attitude == -2 ? Engine::Math::Fixed{} : attitude == 1 ? reach * moods.alert : attitude == 2 ? reach * moods.aggressive : reach;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MoodRanges>
{
	static constexpr std::string_view StableName = "engine.gameplay.mood_ranges";
};
}
