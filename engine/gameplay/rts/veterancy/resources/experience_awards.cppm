export module engine.gameplay.rts.veterancy.resources.experience_awards;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Levels awarded this tick outside of kills (the original's ExperienceTracker::gainExpForLevel: just enough experience
// for so many more levels, e.g. a pilot joining a vehicle), taken in order by the veterancy pass as it adds the tick's
// kills (through the sink while there is one), promoting with feedback.
export namespace engine::gameplay
{
struct ExperienceAward
{
	ecs::Entity entity;
	std::uint8_t levels{1};
	bool canScale{false}; // by its experience scalar (gainExpForLevel's canScaleForBonus)
};

struct ExperienceAwards
{
	std::vector<ExperienceAward> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ExperienceAwards>
{
	static constexpr std::string_view StableName = "engine.gameplay.experience_awards";
};
}
