export module games.generalszh.gameplay.scripts.algorithms.team_sequences;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.containment.components.transport;

// What a team running a sequential script is doing (evaluateAndProgressAllSequentialScripts over the team as an
// AIGroup): gone when there is no such team; dead when every member is effectively dead (AIGroup::isGroupAiDead, a team
// with none too); idle when every member with an AI is idle or dead (AIGroup::isIdle); else busy.
export namespace generalszh::gameplay
{
// A sequential script's subject that is a unit (SequentialScript::m_objectID): its entity with a mark no team index has.
inline constexpr std::uint64_t UnitSubjectMark = std::uint64_t{1} << 62;
inline std::uint64_t UnitSubject(ecs::Entity unit) noexcept
{
	return UnitSubjectMark | (std::uint64_t{unit.generation} << 32) | unit.index;
}
inline bool IsUnitSubject(std::uint64_t subject) noexcept { return (subject & UnitSubjectMark) != 0 && subject != ~std::uint64_t{0}; }
inline ecs::Entity SubjectUnit(std::uint64_t subject) noexcept
{
	ecs::Entity unit;
	unit.index = static_cast<decltype(unit.index)>(subject & 0xFFFFFFFFu);
	unit.generation = static_cast<decltype(unit.generation)>((subject >> 32) & 0x3FFFFFFFu);
	return unit;
}

// The same order as the script runtime's ScriptHooks::SubjectState.
enum class TeamActivity : std::uint8_t
{
	Gone,
	Busy,
	Idle,
	Dead,
};

inline TeamActivity TeamSequenceState(const GameWorld &game, std::uint64_t team)
{
	using State = TeamActivity;
	if (team >= game.roster.TeamCount())
		return State::Gone;
	bool dead = true, idle = true;
	for (const ecs::Entity member : game.roster.TeamAt(static_cast<std::uint32_t>(team)).members)
	{
		const bool gone = EffectivelyDead(game, member);
		dead = dead && gone;
		if (!gone && game.world.IsAlive(member) && HasAi(game, member) && !IsIdle(game, member))
			idle = false;
	}
	return dead ? State::Dead : idle ? State::Idle : State::Busy;
}

// A unit running a sequential script (evaluateAndProgressAllSequentialScripts over the object): gone once destroyed; idle
// when its AI is (AIUpdateInterface::isIdle: not dying, which is its dead state); else busy (one with no AI never
// waits out an idle wait).
inline TeamActivity UnitSequenceState(const GameWorld &game, std::uint64_t subject)
{
	const ecs::Entity unit = SubjectUnit(subject);
	if (!game.world.IsAlive(unit))
		return TeamActivity::Gone;
	return HasAi(game, unit) && !EffectivelyDead(game, unit) && IsIdle(game, unit) ? TeamActivity::Idle : TeamActivity::Busy;
}

// evaluateTeamIsContained: every member aboard something (all), or any (partial); a team with none is neither.
inline bool TeamContained(const GameWorld &game, std::uint32_t team, bool all)
{
	namespace gp = engine::gameplay;
	bool any = false;
	for (const ecs::Entity member : game.roster.TeamAt(team).members)
	{
		const bool aboard = game.world.IsAlive(member) && game.world.Get<gp::Passenger>(member) != nullptr;
		if (aboard && !all)
			return true;
		if (!aboard && all)
			return false;
		any = true;
	}
	return any && all;
}
}
