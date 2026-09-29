export module engine.gameplay.rts.veterancy.resources.skill_point_awards;
import std;

import engine.ecs.system.system;
export import engine.ecs.core.entity;

// This tick's general's points for kills (Player::addSkillPointsForKill from Object::scoreTheKill), in death order:
// the killer's player and the victim's SkillPointValue at its level; the killer and what the victim was (its definition),
// for the killer's player's cash bounty (Player::doBountyForKill, called with it). The session adds them to the players' ranks after
// the tick's systems (a promotion grants sciences, which scripts and special powers hear of). Per tick: not saved.
export namespace engine::gameplay
{
struct SkillPointAward
{
	std::uint32_t player{0};
	std::int32_t points{0};
	ecs::Entity killer;
	std::uint32_t victimDefinition{0xFFFFFFFFu};
	std::uint32_t victimPlayer{0}; // its controlling player (the bounty is its cost to that player: calcCostToBuild)
};

struct SkillPointAwards
{
	std::vector<SkillPointAward> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SkillPointAwards>
{
	static constexpr std::string_view StableName = "engine.gameplay.skill_point_awards";
};
}
