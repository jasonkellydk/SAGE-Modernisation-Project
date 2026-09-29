export module games.generalszh.gameplay.combat.algorithms.common_targets;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.ai.resources.ai_players;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.rts.stealth.components.stealth;

// A team's common target (Team::m_commonAttackTarget, teamAttackCommonTarget): the first victim a member of a team that
// attacks together fires at (AIAttackFireWeaponState::onEnter: setTeamTargetObject while the team has none) becomes
// the team's, for a computer player not on easy; the others then go for it (getNextMoodTarget; the tunnel guards).
// getTeamTargetObject lets it go once it is dead, hidden (stealthed and undetected; disguises are not ported), aboard
// something or an aircraft.
export namespace generalszh::gameplay
{
namespace common_target_detail
{
namespace gp = engine::gameplay;

// Team::setTeamTargetObject: only a computer player's team, not on easy.
inline bool MayShare(const GameWorld &game, std::uint32_t player)
{
	if (player >= game.roster.PlayerCount() || game.roster.PlayerAt(player).human)
		return false;
	const auto *ais = game.world.FindResource<AiPlayers>();
	const AiPlayer *ai = ais != nullptr ? ais->Of(player) : nullptr;
	return ai == nullptr || ai->difficulty != 0;
}
}

// Team::getTeamTargetObject's test of the target.
inline bool CommonTargetValid(const GameWorld &game, ecs::Entity target)
{
	namespace gp = engine::gameplay;
	if (target == ecs::Entity{} || EffectivelyDead(game, target) || game.world.Has<gp::OffMap>(target))
		return false;
	if (const auto *stealth = game.world.Get<gp::Stealth>(target); stealth != nullptr && stealth->Hidden())
		return false;
	const auto *targetable = game.world.Get<gp::Targetable>(target);
	return targetable == nullptr || (targetable->classes & gp::target_class::Aircraft) == 0;
}

// The team's common target, let go when no longer valid (getTeamTargetObject).
inline ecs::Entity TeamTarget(GameWorld &game, std::uint32_t team)
{
	if (team >= game.roster.TeamCount())
		return {};
	auto &record = game.roster.TeamAt(team);
	if (record.commonTarget != ecs::Entity{} && !CommonTargetValid(game, record.commonTarget))
		record.commonTarget = {};
	return record.commonTarget;
}

// setTeamTargetObject (none: cleared).
inline void SetTeamTarget(GameWorld &game, std::uint32_t team, ecs::Entity target)
{
	if (team >= game.roster.TeamCount())
		return;
	auto &record = game.roster.TeamAt(team);
	if (target == ecs::Entity{})
		record.commonTarget = {};
	else if (common_target_detail::MayShare(game, record.owner))
		record.commonTarget = target;
}

// After the step: each shot of the tick, in order, by a member of a team that attacks together that has no (valid)
// target yet makes its victim the team's; then each team's target is let go if no longer valid (what the next
// getTeamTargetObject would do, so the systems see only valid ones).
inline void ApplyCommonTargets(GameWorld &game)
{
	namespace gp = engine::gameplay;
	if (const auto *fired = game.world.FindResource<gp::FiredShots>())
		fired->ForEach([&](const gp::Shot &shot) {
			const auto *member = game.world.IsAlive(shot.source) ? game.world.Get<gp::TeamMember>(shot.source) : nullptr;
			if (member == nullptr || member->team >= game.roster.TeamCount() || !game.roster.TeamAt(member->team).attackCommonTarget)
				return;
			if (shot.target != ecs::Entity{} && TeamTarget(game, member->team) == ecs::Entity{})
				SetTeamTarget(game, member->team, shot.target);
		});
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
		TeamTarget(game, team);
}
}
