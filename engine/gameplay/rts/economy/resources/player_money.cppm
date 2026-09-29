export module engine.gameplay.rts.economy.resources.player_money;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's money (whole credits, as the original), by player index, and how much of what came in it earned
// (harvested, crates, bounties, hacks: the original's ScoreKeeper::addMoneyEarned) as against given back or set.
// Simulation state: checkpointed and hashed with the session's resources.
export namespace engine::gameplay
{
class PlayerMoney
{
public:
	void Resize(std::size_t players) { m_amounts.resize(players, 0); }
	std::int64_t Balance(std::uint32_t player) const noexcept { return player < m_amounts.size() ? m_amounts[player] : 0; }

	void Deposit(std::uint32_t player, std::int64_t amount)
	{
		if (player >= m_amounts.size())
			m_amounts.resize(player + 1, 0);
		m_amounts[player] += amount;
	}

	// Money earned: deposited and counted as earned.
	void Earn(std::uint32_t player, std::int64_t amount)
	{
		Deposit(player, amount);
		AddEarned(player, amount);
	}
	// Counted as earned only (what was deposited may differ: AutoDepositUpdate counts its amount without the boost).
	void AddEarned(std::uint32_t player, std::int64_t amount)
	{
		if (player >= m_earned.size())
			m_earned.resize(player + 1, 0);
		m_earned[player] += amount;
	}
	std::int64_t Earned(std::uint32_t player) const noexcept { return player < m_earned.size() ? m_earned[player] : 0; }

	// Takes `amount` if the player has it.
	bool Withdraw(std::uint32_t player, std::int64_t amount)
	{
		if (Balance(player) < amount)
			return false;
		m_amounts[player] -= amount;
		return true;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_amounts.size()));
		for (const std::int64_t amount : m_amounts)
			writer.I64(amount);
		writer.U32(static_cast<std::uint32_t>(m_earned.size()));
		for (const std::int64_t amount : m_earned)
			writer.I64(amount);
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count)
			return false;
		std::vector<std::int64_t> amounts;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto amount = reader.I64();
			if (!amount)
				return false;
			amounts.push_back(*amount);
		}
		const auto earnedCount = reader.U32();
		if (!earnedCount)
			return false;
		std::vector<std::int64_t> earned;
		for (std::uint32_t index = 0; index < *earnedCount; ++index)
		{
			const auto amount = reader.I64();
			if (!amount)
				return false;
			earned.push_back(*amount);
		}
		m_amounts = std::move(amounts);
		m_earned = std::move(earned);
		return true;
	}

private:
	std::vector<std::int64_t> m_amounts;
	std::vector<std::int64_t> m_earned;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerMoney>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_money";
};
}
