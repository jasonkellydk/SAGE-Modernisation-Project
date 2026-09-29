export module engine.gameplay.common.identity.resources.relationships;
import std;

import engine.ecs.system.system;
export import engine.core.serialization.byte_stream;

// How each player regards every other player, as a dense matrix systems
// can read in parallel. A player is always allied with itself.
// With it, overrides by team (the original's Team::setOverrideTeamRelationship / setOverridePlayerRelationship and
// Player::setTeamRelationship): how one team regards another team or player, and how a player regards a team. What one
// thing makes of another (Object::getRelationship: its team's view of the other's team, Team::getRelationship) takes,
// in order: its team's override for that team, its team's override for that team's player, its player's override for
// that team, then the players' matrix. Teams are TeamRoster indices; NoTeam (a thing on no team) takes no override.
export namespace engine::gameplay
{
enum class Relationship : std::uint8_t
{
	Neutral,
	Allies,
	Enemies,
};

class Relationships
{
public:
	static constexpr std::uint32_t NoTeam = 0xFFFFFFFFu;

	explicit Relationships(std::size_t players = 0) : m_players(players), m_matrix(players * players, Relationship::Neutral)
	{
		for (std::size_t player = 0; player < players; ++player)
			m_matrix[player * players + player] = Relationship::Allies;
	}

	void Set(std::uint32_t from, std::uint32_t to, Relationship relationship)
	{
		if (from < m_players && to < m_players && from != to)
			m_matrix[from * m_players + to] = relationship;
	}

	Relationship Between(std::uint32_t from, std::uint32_t to) const noexcept
	{
		if (from >= m_players || to >= m_players)
			return from == to ? Relationship::Allies : Relationship::Neutral;
		return m_matrix[from * m_players + to];
	}

	bool Enemies(std::uint32_t from, std::uint32_t to) const noexcept { return Between(from, to) == Relationship::Enemies; }
	bool Allies(std::uint32_t from, std::uint32_t to) const noexcept { return Between(from, to) == Relationship::Allies; }
	std::size_t PlayerCount() const noexcept { return m_players; }

	// One thing (on `fromTeam` of `fromPlayer`) toward another (on `toTeam` of `toPlayer`), overrides first.
	Relationship Between(std::uint32_t fromTeam, std::uint32_t fromPlayer, std::uint32_t toTeam, std::uint32_t toPlayer) const noexcept
	{
		if (!m_overrides.empty())
		{
			if (fromTeam != NoTeam && toTeam != NoTeam)
				if (const auto found = Find(Kind::TeamToTeam, fromTeam, toTeam))
					return *found;
			if (fromTeam != NoTeam)
				if (const auto found = Find(Kind::TeamToPlayer, fromTeam, toPlayer))
					return *found;
			if (toTeam != NoTeam)
				if (const auto found = Find(Kind::PlayerToTeam, fromPlayer, toTeam))
					return *found;
		}
		return Between(fromPlayer, toPlayer);
	}
	bool Enemies(std::uint32_t fromTeam, std::uint32_t fromPlayer, std::uint32_t toTeam, std::uint32_t toPlayer) const noexcept
	{
		return Between(fromTeam, fromPlayer, toTeam, toPlayer) == Relationship::Enemies;
	}
	bool Allies(std::uint32_t fromTeam, std::uint32_t fromPlayer, std::uint32_t toTeam, std::uint32_t toPlayer) const noexcept
	{
		return Between(fromTeam, fromPlayer, toTeam, toPlayer) == Relationship::Allies;
	}

	// The overrides (none: removed).
	void SetTeamToTeam(std::uint32_t team, std::uint32_t other, std::optional<Relationship> relationship) { Put(Kind::TeamToTeam, team, other, relationship); }
	void SetTeamToPlayer(std::uint32_t team, std::uint32_t player, std::optional<Relationship> relationship) { Put(Kind::TeamToPlayer, team, player, relationship); }
	void SetPlayerToTeam(std::uint32_t player, std::uint32_t team, std::optional<Relationship> relationship) { Put(Kind::PlayerToTeam, player, team, relationship); }
	// Team::removeOverrideTeamRelationship(TEAM_ID_INVALID) / removeOverridePlayerRelationship(PLAYER_INDEX_INVALID): all of
	// the team's own.
	void ClearTeam(std::uint32_t team)
	{
		std::erase_if(m_overrides, [&](const Override &entry) { return entry.from == team && (entry.kind == Kind::TeamToTeam || entry.kind == Kind::TeamToPlayer); });
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U64(m_players);
		for (const Relationship relationship : m_matrix)
			writer.U8(static_cast<std::uint8_t>(relationship));
		writer.U32(static_cast<std::uint32_t>(m_overrides.size()));
		for (const Override &entry : m_overrides)
		{
			writer.U8(static_cast<std::uint8_t>(entry.kind));
			writer.U32(entry.from);
			writer.U32(entry.to);
			writer.U8(static_cast<std::uint8_t>(entry.relationship));
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto players = reader.U64();
		if (!players || *players > 4096)
			return false;
		std::vector<Relationship> matrix(static_cast<std::size_t>(*players * *players));
		for (Relationship &relationship : matrix)
		{
			const auto value = reader.U8();
			if (!value || *value > static_cast<std::uint8_t>(Relationship::Enemies))
				return false;
			relationship = static_cast<Relationship>(*value);
		}
		const auto count = reader.U32();
		if (!count)
			return false;
		std::vector<Override> overrides;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto kind = reader.U8();
			const auto from = reader.U32();
			const auto to = reader.U32();
			const auto value = reader.U8();
			if (!kind || !from || !to || !value || *kind > 2 || *value > static_cast<std::uint8_t>(Relationship::Enemies))
				return false;
			overrides.push_back({static_cast<Kind>(*kind), *from, *to, static_cast<Relationship>(*value)});
		}
		m_players = static_cast<std::size_t>(*players);
		m_matrix = std::move(matrix);
		m_overrides = std::move(overrides);
		return true;
	}

private:
	enum class Kind : std::uint8_t
	{
		TeamToTeam,
		TeamToPlayer,
		PlayerToTeam,
	};
	struct Override
	{
		Kind kind{Kind::TeamToTeam};
		std::uint32_t from{0};
		std::uint32_t to{0};
		Relationship relationship{Relationship::Neutral};
	};

	std::optional<Relationship> Find(Kind kind, std::uint32_t from, std::uint32_t to) const noexcept
	{
		for (const Override &entry : m_overrides)
			if (entry.kind == kind && entry.from == from && entry.to == to)
				return entry.relationship;
		return std::nullopt;
	}
	void Put(Kind kind, std::uint32_t from, std::uint32_t to, std::optional<Relationship> relationship)
	{
		for (auto it = m_overrides.begin(); it != m_overrides.end(); ++it)
			if (it->kind == kind && it->from == from && it->to == to)
			{
				if (relationship)
					it->relationship = *relationship;
				else
					m_overrides.erase(it);
				return;
			}
		if (relationship)
			m_overrides.push_back({kind, from, to, *relationship});
	}

	std::size_t m_players;
	std::vector<Relationship> m_matrix;
	std::vector<Override> m_overrides; // few (a mission's handful): a flat list, searched in order
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Relationships>
{
	static constexpr std::string_view StableName = "engine.gameplay.relationships";
};
}
