export module engine.gameplay.rts.match.resources.match_outcome;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// A match's standing (the original's VictoryConditions): which players take part (the match's players, not the
// neutral or civilian sides or observers), which of them have been defeated and which won, whether a single alliance
// is left (the match is over) and on which tick that happened. Simulation state: checkpointed and hashed.
// `fallen` lists the players defeated this tick (for the presentation's message and sound), not kept.
export namespace engine::gameplay
{
struct MatchStanding
{
	std::uint32_t player{0};
	bool defeated{false};
	bool victorious{false};
};

struct MatchOutcome
{
	bool enabled{false}; // a multiplayer or skirmish match (the original's TheRecorder->isMultiplayer())
	std::vector<MatchStanding> players;
	bool singleAllianceRemaining{false};
	std::uint64_t endTick{0};
	std::vector<std::uint32_t> fallen;

	const MatchStanding *Of(std::uint32_t player) const noexcept
	{
		for (const MatchStanding &standing : players)
			if (standing.player == player)
				return &standing;
		return nullptr;
	}

	// hasAchievedVictory / hasBeenDefeated: only once a single alliance is left.
	bool HasWon(std::uint32_t player) const noexcept
	{
		const MatchStanding *standing = Of(player);
		return singleAllianceRemaining && standing != nullptr && standing->victorious;
	}
	bool HasLost(std::uint32_t player) const noexcept
	{
		const MatchStanding *standing = Of(player);
		return singleAllianceRemaining && standing != nullptr && standing->defeated;
	}
	// The player's own defeat, whatever became of their allies.
	bool Eliminated(std::uint32_t player) const noexcept
	{
		const MatchStanding *standing = Of(player);
		return standing != nullptr && standing->defeated;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U8(enabled ? 1 : 0);
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const MatchStanding &standing : players)
		{
			writer.U32(standing.player);
			writer.U8(static_cast<std::uint8_t>((standing.defeated ? 1 : 0) | (standing.victorious ? 2 : 0)));
		}
		writer.U8(singleAllianceRemaining ? 1 : 0);
		writer.U64(endTick);
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto on = reader.U8();
		const auto count = reader.U32();
		if (!on || !count)
			return false;
		std::vector<MatchStanding> loaded;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto player = reader.U32();
			const auto flags = reader.U8();
			if (!player || !flags)
				return false;
			loaded.push_back({*player, (*flags & 1) != 0, (*flags & 2) != 0});
		}
		const auto single = reader.U8();
		const auto end = reader.U64();
		if (!single || !end)
			return false;
		enabled = *on != 0;
		players = std::move(loaded);
		singleAllianceRemaining = *single != 0;
		endTick = *end;
		fallen.clear();
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MatchOutcome>
{
	static constexpr std::string_view StableName = "engine.gameplay.match_outcome";
};
}
