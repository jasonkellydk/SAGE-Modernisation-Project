export module games.generalszh.gameplay.battleplans.resources.battle_plan_players;
import std;

export import engine.core.serialization.byte_stream;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
import engine.ecs.system.system;

// Each player's battle plans (Player::m_bombardBattlePlans, m_holdTheLineBattlePlans, m_searchAndDestroyBattlePlans:
// how many of its Strategy Centers hold each plan) and the bonuses its army has from them (Player::m_battlePlanBonuses,
// BattlePlanBonusesData: made the first time a plan comes in, then every change folded in; what new or captured troops
// are given): the armor damage and sight range scalars, a count of each plan's weapon bonus, and the kinds they reach.
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct PlanBonuses
{
	Engine::Math::Fixed armorScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed sightScalar{Engine::Math::Fixed::One()};
	std::int32_t bombardment{0};
	std::int32_t holdTheLine{0};
	std::int32_t searchAndDestroy{0};
	content::KindOfMask valid{};
	content::KindOfMask invalid{};
};

struct PlayerBattlePlans
{
	std::array<std::int32_t, 3> counts{}; // Bombardment, HoldTheLine, SearchAndDestroy
	bool made{false};                     // m_battlePlanBonuses allocated
	PlanBonuses bonuses;

	// getNumBattlePlansActive.
	std::int32_t Active() const noexcept { return counts[0] + counts[1] + counts[2]; }
};

struct BattlePlanPlayers
{
	static constexpr std::size_t Players = 16; // MAX_PLAYER_COUNT
	std::array<PlayerBattlePlans, Players> players{};

	PlayerBattlePlans *Of(std::uint32_t player) noexcept { return player < Players ? &players[player] : nullptr; }
	const PlayerBattlePlans *Of(std::uint32_t player) const noexcept { return player < Players ? &players[player] : nullptr; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		for (const PlayerBattlePlans &player : players)
		{
			for (const std::int32_t count : player.counts)
				writer.U32(static_cast<std::uint32_t>(count));
			writer.Flag(player.made);
			writer.U64(static_cast<std::uint64_t>(player.bonuses.armorScalar.Raw()));
			writer.U64(static_cast<std::uint64_t>(player.bonuses.sightScalar.Raw()));
			writer.U32(static_cast<std::uint32_t>(player.bonuses.bombardment));
			writer.U32(static_cast<std::uint32_t>(player.bonuses.holdTheLine));
			writer.U32(static_cast<std::uint32_t>(player.bonuses.searchAndDestroy));
			for (const std::uint64_t word : player.bonuses.valid)
				writer.U64(word);
			for (const std::uint64_t word : player.bonuses.invalid)
				writer.U64(word);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		for (PlayerBattlePlans &player : players)
		{
			for (std::int32_t &count : player.counts)
			{
				const auto value = reader.U32();
				if (!value)
					return false;
				count = static_cast<std::int32_t>(*value);
			}
			const auto made = reader.Flag();
			const auto armor = reader.U64();
			const auto sight = reader.U64();
			const auto bombardment = reader.U32();
			const auto holdTheLine = reader.U32();
			const auto searchAndDestroy = reader.U32();
			if (!made || !armor || !sight || !bombardment || !holdTheLine || !searchAndDestroy)
				return false;
			player.made = *made;
			player.bonuses.armorScalar = Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(*armor));
			player.bonuses.sightScalar = Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(*sight));
			player.bonuses.bombardment = static_cast<std::int32_t>(*bombardment);
			player.bonuses.holdTheLine = static_cast<std::int32_t>(*holdTheLine);
			player.bonuses.searchAndDestroy = static_cast<std::int32_t>(*searchAndDestroy);
			for (auto *mask : {&player.bonuses.valid, &player.bonuses.invalid})
				for (std::uint64_t &word : *mask)
				{
					const auto value = reader.U64();
					if (!value)
						return false;
					word = *value;
				}
		}
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BattlePlanPlayers>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_plan_players";
};
}
