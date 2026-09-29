export module engine.gameplay.rts.veterancy.definitions.veterancy;
import std;

// Veterancy (the original's VeterancyLevel and ThingTemplate experience
// fields): what an object is worth to its killer at each level
// (ExperienceValue), the points each level needs (ExperienceRequired) and
// whether it can gain any (IsTrainable). Kills of objects that do not score
// (KINDOF_IGNORED_IN_GUI) give nothing.
export namespace engine::gameplay
{
// Levels 0 (the first) to VeterancyLevelCount - 1; what each means is the game's.
inline constexpr std::size_t VeterancyLevelCount = 4;

struct VeterancyDefinition
{
	std::array<std::int32_t, VeterancyLevelCount> value{};
	std::array<std::int32_t, VeterancyLevelCount> required{};
	bool trainable{false};
	bool scoresKills{true};
	// The general's points a kill of it gives its killer's player, per level (SkillPointValue, else its ExperienceValue).
	std::array<std::int32_t, VeterancyLevelCount> skillValue{};
};
}
