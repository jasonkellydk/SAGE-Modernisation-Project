export module games.generalszh.gameplay.score.resources.score_keepers;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's score keeping (the original's ScoreKeeper, one per Player): what it spent (structures finished, upgrades
// researched), the units and buildings it built, lost and captured, those it destroyed of each other player, and how
// many of each kind (by definition) of each; and whether scoring is on (GameLogic::isScoringEnabled: ENABLE_SCORING /
// DISABLE_SCORING). What it earned is its PlayerMoney's. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
inline constexpr std::size_t ScorePlayers = 16; // MAX_PLAYER_COUNT

struct ScoreKeeper
{
	std::int64_t moneySpent{0};
	std::int32_t unitsBuilt{0}, unitsLost{0};
	std::int32_t buildingsBuilt{0}, buildingsLost{0};
	std::int32_t techBuildingsCaptured{0}, factionBuildingsCaptured{0};
	std::array<std::int32_t, ScorePlayers> unitsDestroyed{}, buildingsDestroyed{};
	std::map<std::uint32_t, std::int32_t> objectsBuilt, objectsLost, objectsCaptured;
	std::array<std::map<std::uint32_t, std::int32_t>, ScorePlayers> objectsDestroyed;
};

struct ScoreKeepers
{
	bool enabled{true};
	std::vector<ScoreKeeper> players;

	ScoreKeeper &Of(std::uint32_t player)
	{
		if (player >= players.size())
			players.resize(player + 1);
		return players[player];
	}
	const ScoreKeeper *Find(std::uint32_t player) const noexcept { return player < players.size() ? &players[player] : nullptr; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		const auto map = [&](const std::map<std::uint32_t, std::int32_t> &counts) {
			writer.U32(static_cast<std::uint32_t>(counts.size()));
			for (const auto &[definition, count] : counts)
			{
				writer.U32(definition);
				writer.I64(count);
			}
		};
		writer.Flag(enabled);
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const ScoreKeeper &keeper : players)
		{
			writer.I64(keeper.moneySpent);
			for (const std::int32_t value : {keeper.unitsBuilt, keeper.unitsLost, keeper.buildingsBuilt, keeper.buildingsLost, keeper.techBuildingsCaptured,
					 keeper.factionBuildingsCaptured})
				writer.I64(value);
			for (std::size_t other = 0; other < ScorePlayers; ++other)
			{
				writer.I64(keeper.unitsDestroyed[other]);
				writer.I64(keeper.buildingsDestroyed[other]);
				map(keeper.objectsDestroyed[other]);
			}
			map(keeper.objectsBuilt);
			map(keeper.objectsLost);
			map(keeper.objectsCaptured);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto map = [&](std::map<std::uint32_t, std::int32_t> &counts) {
			counts.clear();
			for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
			{
				const std::uint32_t definition = reader.U32().value_or(0);
				counts[definition] = static_cast<std::int32_t>(reader.I64().value_or(0));
			}
		};
		const auto on = reader.Flag();
		const auto count = reader.U32();
		if (!on || !count || *count > 1024)
			return false;
		std::vector<ScoreKeeper> loaded(*count);
		for (ScoreKeeper &keeper : loaded)
		{
			keeper.moneySpent = reader.I64().value_or(0);
			for (std::int32_t *value : {&keeper.unitsBuilt, &keeper.unitsLost, &keeper.buildingsBuilt, &keeper.buildingsLost, &keeper.techBuildingsCaptured,
					 &keeper.factionBuildingsCaptured})
				*value = static_cast<std::int32_t>(reader.I64().value_or(0));
			for (std::size_t other = 0; other < ScorePlayers; ++other)
			{
				keeper.unitsDestroyed[other] = static_cast<std::int32_t>(reader.I64().value_or(0));
				keeper.buildingsDestroyed[other] = static_cast<std::int32_t>(reader.I64().value_or(0));
				map(keeper.objectsDestroyed[other]);
			}
			map(keeper.objectsBuilt);
			map(keeper.objectsLost);
			map(keeper.objectsCaptured);
		}
		if (reader.Failed())
			return false;
		enabled = *on;
		players = std::move(loaded);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::ScoreKeepers>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.score_keepers";
};
}
