export module engine.gameplay.rts.economy.resources.player_bounties;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's cash bounty (the original's Player::m_cashBountyPercent): the share of a killed enemy's cost paid to
// the killer's player, in hundredths of a percent (a bounty only ever rises: CashBountyPower keeps the larger). None
// by default.
export namespace engine::gameplay
{
class PlayerBounties
{
public:
	static constexpr std::int64_t Whole = 10000; // 100%

	std::int64_t Of(std::uint32_t player) const noexcept { return player < m_shares.size() ? m_shares[player] : 0; }

	// setCashBounty(max(current, share)).
	void Raise(std::uint32_t player, std::int64_t share)
	{
		if (player >= m_shares.size())
			m_shares.resize(player + 1, 0);
		m_shares[player] = std::max(m_shares[player], share);
	}

	// Player::doBountyForKill: ceil(cost x share), exactly (the original's float product lands on these whole values
	// for every shipped Bounty).
	std::int64_t BountyFor(std::uint32_t player, std::int64_t cost) const noexcept
	{
		const std::int64_t share = Of(player);
		if (share <= 0 || cost <= 0)
			return 0;
		return (cost * share + Whole - 1) / Whole;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_shares.size()));
		for (const std::int64_t share : m_shares)
			writer.I64(share);
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count > 4096)
			return false;
		std::vector<std::int64_t> shares;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto share = reader.I64();
			if (!share)
				return false;
			shares.push_back(*share);
		}
		m_shares = std::move(shares);
		return true;
	}

private:
	std::vector<std::int64_t> m_shares;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerBounties>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_bounties";
};
}
