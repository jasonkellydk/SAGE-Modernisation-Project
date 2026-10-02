export module engine.gameplay.rts.sciences.resources.player_ranks;
import std;

export import Engine.Core.Math.Fixed;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's rank as a general (Player: m_rankLevel, m_skillPoints, m_levelUp / m_levelDown, the science purchase
// points to spend, and the scale its skill point gains take). Simulation state: checkpointed and hashed.
export namespace engine::gameplay
{
struct PlayerRank
{
	std::int32_t level{1};
	std::int32_t skillPoints{0};
	std::int32_t levelUp{std::numeric_limits<std::int32_t>::max()};
	std::int32_t levelDown{0};
	std::int32_t purchasePoints{0};
	Engine::Math::Fixed skillModifier{Engine::Math::Fixed::One()};
	std::int32_t intrinsicPurchasePoints{0}; // its side's IntrinsicSciencePurchasePoints (what a reset starts from)
	std::vector<std::uint32_t> intrinsicSciences; // its side's IntrinsicSciences (what a reset gives back)
};

struct PlayerRanks
{
	std::vector<PlayerRank> players;

	PlayerRank &Of(std::uint32_t player)
	{
		if (player >= players.size())
			players.resize(player + 1);
		return players[player];
	}
	const PlayerRank *Find(std::uint32_t player) const noexcept { return player < players.size() ? &players[player] : nullptr; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const PlayerRank &rank : players)
		{
			writer.I64(rank.level);
			writer.I64(rank.skillPoints);
			writer.I64(rank.levelUp);
			writer.I64(rank.levelDown);
			writer.I64(rank.purchasePoints);
			writer.I64(rank.skillModifier.Raw());
			writer.I64(rank.intrinsicPurchasePoints);
			writer.U32(static_cast<std::uint32_t>(rank.intrinsicSciences.size()));
			for (const std::uint32_t science : rank.intrinsicSciences)
				writer.U32(science);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count > 4096)
			return false;
		std::vector<PlayerRank> loaded(*count);
		for (PlayerRank &rank : loaded)
		{
			const auto level = reader.I64(), points = reader.I64(), up = reader.I64(), down = reader.I64(), purchase = reader.I64(), modifier = reader.I64();
			const auto intrinsic = reader.I64();
			if (!level || !points || !up || !down || !purchase || !modifier || !intrinsic)
				return false;
			rank = {static_cast<std::int32_t>(*level), static_cast<std::int32_t>(*points), static_cast<std::int32_t>(*up), static_cast<std::int32_t>(*down),
				static_cast<std::int32_t>(*purchase), Engine::Math::Fixed::FromRaw(*modifier), static_cast<std::int32_t>(*intrinsic), {}};
			const auto sciences = reader.U32();
			if (!sciences || *sciences > 256)
				return false;
			for (std::uint32_t index = 0; index < *sciences; ++index)
			{
				const auto science = reader.U32();
				if (!science)
					return false;
				rank.intrinsicSciences.push_back(*science);
			}
		}
		players = std::move(loaded);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerRanks>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_ranks";
};
}
