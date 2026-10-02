export module engine.gameplay.rts.teams.resources.team_roster;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;
export import engine.ecs.core.entity_codec;

// Players and their teams. A team has an owner, a lifetime state (created
// once units join it) and its members in join order. The session keeps
// membership in step with spawns, deaths and merges between ticks.
export namespace engine::gameplay
{
struct Player
{
	std::string name;
	std::vector<std::uint32_t> allies;
	std::vector<std::uint32_t> enemies;
	// Highest rank the player may reach (0: no limit) and whether it may build units (Player::m_canBuildUnits: on for a
	// player, off for a map's computer player until its scripts turn it on: AIPlayer's constructor).
	std::uint32_t rankLimit{1000}; // GameLogic::m_rankLevelLimit (PLAYER_SET_RANKLEVELLIMIT sets it for all)
	bool unitConstructionEnabled{true};
	// Its player template is a playable side (PlayableSide): kills of its objects score.
	bool playable{false};
	// A human plays it (PLAYER_HUMAN), else the computer.
	bool human{false};
	// Its default team (Player::setDefaultTeam: "team" + its name, a computer player's qualified side's); empty: that.
	std::string defaultTeam;
	// It may put up its base (PLAYER_ENABLE/DISABLE_BASE_CONSTRUCTION; on at the start).
	bool canBuildBase{true};
	// Its tunnel network's current enemy (TunnelTracker::m_curNemesisID) and when it was last seen at it
	// (m_nemesisTimestamp): the network's guards all go for it.
	ecs::Entity tunnelNemesis;
	std::uint64_t nemesisTick{0};
	// The players whose things have hurt its things (Player::m_attackedBy: a bit per player index).
	std::uint64_t attackedBy{0};
	// The tick it was last attacked (Player::m_attackedFrame; 0: never).
	std::uint64_t attackedTick{0};
	// Whether the score screen lists it (Player::m_listInScoreScreen; a script can say no).
	bool listInScoreScreen{true};
	// All its units hunt (Player::m_unitsShouldHunt: PLAYER_HUNT): hunting, they never stop for want of victims and look
	// past their attack priorities when those find none.
	bool unitsShouldHunt{false};
	// Killed (Player::killPlayer: m_isPlayerDead): object creation lists that require a live player make nothing for it.
	bool dead{false};
};

struct Team
{
	std::string name;
	std::uint32_t owner{0};
	bool singleton{true};
	bool created{false};
	std::vector<ecs::Entity> members;
	// A further instance of a team (a computer player building another of its teams: TeamFactory::createInactiveTeam)
	// names the team it is one of; the teams the level made are their own. An instance is inactive while it is being
	// built (Team::setActive starts it), and gone once deleted (its index stays, unused).
	static constexpr std::uint32_t Own = 0xFFFFFFFFu;
	std::uint32_t prototype{Own};
	bool active{true};
	bool alive{true};
	// Recruitable by other teams (TEAM_AVAILABLE_FOR_RECRUITMENT): -1 as its template says, else 0 or 1.
	std::int8_t recruitable{-1};
	// Its scripts' bookkeeping (Team::updateState / updateGenericScripts): just started (its on-create script is due),
	// the members counted and the count at which its on-destroyed script runs (-1: done), whether it was idle last
	// time, and which of its generic scripts (bits 0..15) are no longer tried.
	bool justCreated{false};
	std::int32_t unitCount{0};
	std::int32_t destroyThreshold{0};
	bool wasIdle{false};
	std::uint16_t genericDone{0};
	// Whether a living member saw an enemy at the last look, and the look before (enemy sighted / all clear).
	bool seeEnemy{false};
	bool prevSeeEnemy{false};
	// Its members go for one victim together (teamAttackCommonTarget), that victim (Team::m_commonAttackTarget; none).
	bool attackCommonTarget{false};
	ecs::Entity commonTarget;
	// The state a script gave it (Team::m_state: TEAM_SET_STATE; TEAM_STATE_IS).
	std::string state;
	// A team's attack priority set (TeamPrototype::m_attackPriorityName, kept on the team the level made: its instances
	// share it), which members take as they are made or join (AttackPriorities: index + 1; 0: none).
	std::uint16_t prioritySet{0};
};

class TeamRoster
{
public:
	std::uint32_t AddPlayer(Player player)
	{
		m_players.push_back(std::move(player));
		return static_cast<std::uint32_t>(m_players.size() - 1);
	}

	std::uint32_t AddTeam(Team team)
	{
		m_teams.push_back(std::move(team));
		const auto index = static_cast<std::uint32_t>(m_teams.size() - 1);
		Index(index);
		return index;
	}

	std::optional<std::uint32_t> FindPlayer(std::string_view name) const
	{
		for (std::uint32_t index = 0; index < m_players.size(); ++index)
			if (m_players[index].name == name)
				return index;
		return std::nullopt;
	}

	std::optional<std::uint32_t> FindTeam(std::string_view name) const
	{
		const auto found = m_firstNamed.find(name);
		return found != m_firstNamed.end() ? std::optional(found->second) : std::nullopt;
	}

	Player &PlayerAt(std::uint32_t index) { return m_players.at(index); }
	const Player &PlayerAt(std::uint32_t index) const { return m_players.at(index); }
	Team &TeamAt(std::uint32_t index) { return m_teams.at(index); }
	const Team &TeamAt(std::uint32_t index) const { return m_teams.at(index); }
	std::size_t PlayerCount() const noexcept { return m_players.size(); }
	std::size_t TeamCount() const noexcept { return m_teams.size(); }
	// Team::setActive: an inactive team starts (its on-create script comes due).
	void SetActive(std::uint32_t team)
	{
		Team &of = m_teams.at(team);
		if (of.active)
			return;
		of.active = true;
		of.justCreated = true;
		of.created = true;
	}
	// The team a team is an instance of (a level team: itself).
	std::uint32_t PrototypeOf(std::uint32_t team) const
	{
		return team < m_teams.size() && m_teams[team].prototype != Team::Own ? m_teams[team].prototype : team;
	}
	// A new, inactive instance of the team (a singleton's is itself).
	std::uint32_t CreateInstance(std::uint32_t prototype)
	{
		const Team &of = m_teams.at(prototype);
		if (of.singleton)
			return prototype;
		Team instance;
		instance.name = of.name;
		instance.owner = of.owner;
		instance.singleton = false;
		instance.prototype = prototype;
		instance.active = false;
		instance.attackCommonTarget = of.attackCommonTarget;
		m_teams.push_back(std::move(instance));
		const auto index = static_cast<std::uint32_t>(m_teams.size() - 1);
		Index(index);
		return index;
	}
	// The team's instances, newest first (TeamPrototype's instance list): the further ones, then the level's own
	// record while it holds anything, was made, or is a singleton.
	std::vector<std::uint32_t> Instances(std::uint32_t prototype) const
	{
		std::vector<std::uint32_t> out;
		InstancesInto(prototype, out);
		return out;
	}
	// The same into `out` (cleared first): a caller walking many teams reuses one buffer.
	void InstancesInto(std::uint32_t prototype, std::vector<std::uint32_t> &out) const
	{
		out.clear();
		if (prototype < m_instancesOf.size())
			for (auto it = m_instancesOf[prototype].rbegin(); it != m_instancesOf[prototype].rend(); ++it)
				if (m_teams[*it].alive)
					out.push_back(*it);
		if (prototype < m_teams.size())
		{
			const Team &own = m_teams[prototype];
			if (own.alive && (own.singleton || own.created || !own.members.empty()))
				out.push_back(prototype);
		}
	}
	// An instance deleted (its members, if any, must have left): gone, its index unused.
	void DeleteInstance(std::uint32_t team)
	{
		Team &instance = m_teams.at(team);
		if (instance.prototype == Team::Own)
		{
			instance.created = false;
			instance.active = false;
			return;
		}
		instance.alive = false;
		instance.members.clear();
	}
	// The player's default team.
	std::optional<std::uint32_t> DefaultTeam(std::uint32_t player) const
	{
		if (player >= m_players.size())
			return std::nullopt;
		const Player &of = m_players[player];
		return FindTeam(of.defaultTeam.empty() ? "team" + of.name : of.defaultTeam);
	}

	// `completes`: the team now exists (false while an AI is still building it).
	void Join(std::uint32_t team, ecs::Entity entity, bool completes = true)
	{
		Team &target = m_teams.at(team);
		target.members.push_back(entity);
		target.created = target.created || completes;
	}

	void Leave(std::uint32_t team, ecs::Entity entity)
	{
		auto &members = m_teams.at(team).members;
		members.erase(std::remove(members.begin(), members.end(), entity), members.end());
	}

	// Checkpoints: everything above, in order.
	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_players.size()));
		for (const Player &player : m_players)
		{
			writer.Text(player.name);
			for (const auto *list : {&player.allies, &player.enemies})
			{
				writer.U32(static_cast<std::uint32_t>(list->size()));
				for (const std::uint32_t other : *list)
					writer.U32(other);
			}
			writer.U32(player.rankLimit);
			writer.Flag(player.unitConstructionEnabled);
			writer.Flag(player.playable);
			writer.Flag(player.human);
			writer.Text(player.defaultTeam);
			writer.Flag(player.canBuildBase);
			ecs::WriteEntity(writer, player.tunnelNemesis);
			writer.U64(player.nemesisTick);
			writer.U64(player.attackedBy);
			writer.U64(player.attackedTick);
			writer.Flag(player.listInScoreScreen);
			writer.Flag(player.unitsShouldHunt);
			writer.Flag(player.dead);
		}
		writer.U32(static_cast<std::uint32_t>(m_teams.size()));
		for (const Team &team : m_teams)
		{
			writer.Text(team.name);
			writer.U32(team.owner);
			writer.Flag(team.singleton);
			writer.Flag(team.created);
			writer.U32(static_cast<std::uint32_t>(team.members.size()));
			for (const ecs::Entity member : team.members)
				ecs::WriteEntity(writer, member);
			writer.U32(team.prototype);
			writer.Flag(team.active);
			writer.Flag(team.alive);
			writer.U8(static_cast<std::uint8_t>(team.recruitable));
			writer.Flag(team.justCreated);
			writer.U32(static_cast<std::uint32_t>(team.unitCount));
			writer.U32(static_cast<std::uint32_t>(team.destroyThreshold));
			writer.Flag(team.wasIdle);
			writer.U32(team.genericDone);
			writer.Flag(team.seeEnemy);
			writer.Flag(team.prevSeeEnemy);
			writer.Flag(team.attackCommonTarget);
			ecs::WriteEntity(writer, team.commonTarget);
			writer.Text(team.state);
			writer.U32(team.prioritySet);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		std::vector<Player> players;
		std::vector<Team> teams;
		const auto playerCount = reader.U32();
		for (std::uint32_t index = 0; playerCount && index < *playerCount && !reader.Failed(); ++index)
		{
			Player player;
			player.name = reader.Text().value_or("");
			for (auto *list : {&player.allies, &player.enemies})
			{
				const auto count = reader.U32();
				for (std::uint32_t item = 0; count && item < *count && !reader.Failed(); ++item)
					list->push_back(reader.U32().value_or(0));
			}
			player.rankLimit = reader.U32().value_or(0);
			player.unitConstructionEnabled = reader.Flag().value_or(true);
			player.playable = reader.Flag().value_or(false);
			player.human = reader.Flag().value_or(false);
			player.defaultTeam = reader.Text().value_or("");
			player.canBuildBase = reader.Flag().value_or(true);
			player.tunnelNemesis = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			player.nemesisTick = reader.U64().value_or(0);
			player.attackedBy = reader.U64().value_or(0);
			player.attackedTick = reader.U64().value_or(0);
			player.listInScoreScreen = reader.Flag().value_or(true);
			player.unitsShouldHunt = reader.Flag().value_or(false);
			player.dead = reader.Flag().value_or(false);
			players.push_back(std::move(player));
		}
		const auto teamCount = reader.U32();
		for (std::uint32_t index = 0; teamCount && index < *teamCount && !reader.Failed(); ++index)
		{
			Team team;
			team.name = reader.Text().value_or("");
			team.owner = reader.U32().value_or(0);
			team.singleton = reader.Flag().value_or(false);
			team.created = reader.Flag().value_or(false);
			const auto count = reader.U32();
			for (std::uint32_t item = 0; count && item < *count && !reader.Failed(); ++item)
				team.members.push_back(ecs::ReadEntity(reader).value_or(ecs::Entity{}));
			team.prototype = reader.U32().value_or(Team::Own);
			team.active = reader.Flag().value_or(true);
			team.alive = reader.Flag().value_or(true);
			team.recruitable = static_cast<std::int8_t>(reader.U8().value_or(0xFF));
			team.justCreated = reader.Flag().value_or(false);
			team.unitCount = static_cast<std::int32_t>(reader.U32().value_or(0));
			team.destroyThreshold = static_cast<std::int32_t>(reader.U32().value_or(0));
			team.wasIdle = reader.Flag().value_or(false);
			team.genericDone = static_cast<std::uint16_t>(reader.U32().value_or(0));
			team.seeEnemy = reader.Flag().value_or(false);
			team.prevSeeEnemy = reader.Flag().value_or(false);
			team.attackCommonTarget = reader.Flag().value_or(false);
			team.commonTarget = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			team.state = reader.Text().value_or("");
			team.prioritySet = static_cast<std::uint16_t>(reader.U32().value_or(0));
			teams.push_back(std::move(team));
		}
		if (reader.Failed())
			return false;
		m_players = std::move(players);
		m_teams = std::move(teams);
		m_firstNamed.clear();
		m_instancesOf.clear();
		for (std::uint32_t index = 0; index < m_teams.size(); ++index)
			Index(index);
		return true;
	}

private:
	// The lookups kept beside the teams (rebuilt on load): the first team of each name (FindTeam), and each level
	// team's further instances in the order they were made (Instances).
	void Index(std::uint32_t index)
	{
		const Team &team = m_teams[index];
		m_firstNamed.try_emplace(team.name, index);
		if (m_instancesOf.size() < m_teams.size())
			m_instancesOf.resize(m_teams.size());
		if (team.prototype != Team::Own && team.prototype < m_instancesOf.size())
			m_instancesOf[team.prototype].push_back(index);
	}
	std::map<std::string, std::uint32_t, std::less<>> m_firstNamed;
	std::vector<std::vector<std::uint32_t>> m_instancesOf;
	std::vector<Player> m_players;
	std::vector<Team> m_teams;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::TeamRoster>
{
	static constexpr std::string_view StableName = "engine.gameplay.team_roster";
};
}
