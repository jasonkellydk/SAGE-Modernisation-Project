export module games.generalszh.gameplay.world.resources.match_rules;
import std;

import engine.ecs.system.system;

// The match's own rules from its setup (GameLogic's GameInfo): the superweapon restriction (GameInfo::getSuperweaponRestriction,
// 0: none), which is how many of each DeterminedBySuperweaponRestriction object a player may have at once
// (ThingTemplate::getMaxSimultaneousOfType). Fixed for the match and made again from the level, so not checkpointed.
export namespace generalszh::gameplay
{
struct MatchRules
{
	std::uint32_t superweaponRestriction{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::MatchRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.match_rules";
};
}
