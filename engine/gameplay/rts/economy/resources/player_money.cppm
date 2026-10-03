export module engine.gameplay.rts.economy.resources.player_money;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's money (whole credits, as the original), by player index, and how much of what came in it earned
// (harvested, crates, bounties, hacks: the original's ScoreKeeper::addMoneyEarned) as against given back or set.
// Simulation state: checkpointed and hashed with the session's resources; this tick's transactions (what the
// presentation rings) are not.
export namespace engine::gameplay
{
// A deposit or withdrawal its player hears (MiscAudio MoneyDepositSound / MoneyWithdrawSound, for that player).
struct MoneyTransaction
{
	enum class Kind : std::uint8_t
	{
		Deposit,
		Withdraw,
	};
	std::uint32_t player{0};
	Kind kind{Kind::Deposit};
};

class PlayerMoney
{
public:
	void Resize(std::size_t players) { m_amounts.resize(players, 0); }
	std::int64_t Balance(std::uint32_t player) const noexcept { return player < m_amounts.size() ? m_amounts[player] : 0; }

	// Money::deposit: `amount` in; one that is not nothing rings its player's deposit sound unless `sound` is off
	// (starting cash, a script setting the cash).
	void Deposit(std::uint32_t player, std::int64_t amount, bool sound = true)
	{
		if (player >= m_amounts.size())
			m_amounts.resize(player + 1, 0);
		m_amounts[player] += amount;
		if (sound && amount != 0)
			m_transactions.push_back({player, MoneyTransaction::Kind::Deposit});
		if (amount > 0)
			m_incomes.push_back(player);
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

	// Takes `amount` if the player has it (Money::withdraw after canAfford): one that is not nothing rings its player's
	// withdrawal sound.
	bool Withdraw(std::uint32_t player, std::int64_t amount, bool sound = true)
	{
		if (Balance(player) < amount)
			return false;
		m_amounts[player] -= amount;
		if (sound && amount != 0)
			m_transactions.push_back({player, MoneyTransaction::Kind::Withdraw});
		return true;
	}

	// Money::withdraw as such: as much of `amount` as the player has (never into debt); what it took.
	std::int64_t WithdrawUpTo(std::uint32_t player, std::int64_t amount, bool sound = true)
	{
		const std::int64_t taken = std::clamp<std::int64_t>(amount, 0, Balance(player));
		if (taken != 0)
			Withdraw(player, taken, sound);
		return taken;
	}

	// This tick's sounding deposits and withdrawals, in order (Money::triggerAudioEvent): per tick, cleared as a tick
	// begins; neither saved nor hashed.
	std::span<const MoneyTransaction> Transactions() const noexcept { return m_transactions; }
	void ClearTransactions() noexcept { m_transactions.clear(); }
	// The players of each deposit of more than nothing since its reader last took them, in order (Money::deposit's
	// income, sounding or not: the game's per-player records read them once a tick). Saved: those after the tick's
	// reading wait for the next one.
	std::span<const std::uint32_t> Incomes() const noexcept { return m_incomes; }
	void ClearIncomes() noexcept { m_incomes.clear(); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_amounts.size()));
		for (const std::int64_t amount : m_amounts)
			writer.I64(amount);
		writer.U32(static_cast<std::uint32_t>(m_earned.size()));
		for (const std::int64_t amount : m_earned)
			writer.I64(amount);
		writer.U32(static_cast<std::uint32_t>(m_incomes.size()));
		for (const std::uint32_t player : m_incomes)
			writer.U32(player);
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
		const auto incomeCount = reader.U32();
		if (!incomeCount)
			return false;
		std::vector<std::uint32_t> incomes;
		for (std::uint32_t index = 0; index < *incomeCount; ++index)
		{
			const auto player = reader.U32();
			if (!player)
				return false;
			incomes.push_back(*player);
		}
		m_amounts = std::move(amounts);
		m_earned = std::move(earned);
		m_incomes = std::move(incomes);
		return true;
	}

private:
	std::vector<std::int64_t> m_amounts;
	std::vector<std::int64_t> m_earned;
	std::vector<MoneyTransaction> m_transactions;
	std::vector<std::uint32_t> m_incomes;
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
