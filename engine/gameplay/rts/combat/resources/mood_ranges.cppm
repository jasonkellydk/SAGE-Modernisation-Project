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
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MoodRanges>
{
	static constexpr std::string_view StableName = "engine.gameplay.mood_ranges";
};
}
