export module engine.gameplay.rts.upgrades.resources.player_upgrades;
import std;

export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's completed upgrades (the original's Player::m_upgradesCompleted)
// and those under research (m_upgradesInProgress),
// and a count of grants per player: its objects look at their upgrade
// triggers again when it moves on (Player::onUpgradeCompleted). Simulation
// state: checkpointed and hashed with the session's resources.
export namespace engine::gameplay
{
class PlayerUpgrades
{
public:
	void Resize(std::size_t players)
	{
		m_completed.resize(players);
		m_inProduction.resize(players);
		m_grants.resize(players, 0);
	}
	const UpgradeMask &Completed(std::uint32_t player) const noexcept
	{
		static const UpgradeMask none{};
		return player < m_completed.size() ? m_completed[player] : none;
	}
	bool InProduction(std::uint32_t player, std::uint32_t upgrade) const noexcept
	{
		return player < m_inProduction.size() && m_inProduction[player].Has(upgrade);
	}
	// Player::addUpgrade(IN_PRODUCTION) and its cancelling.
	void StartProduction(std::uint32_t player, std::uint32_t upgrade)
	{
		if (player >= m_completed.size())
			Resize(player + 1);
		m_inProduction[player].Set(upgrade);
	}
	void CancelProduction(std::uint32_t player, std::uint32_t upgrade)
	{
		if (player < m_inProduction.size())
			m_inProduction[player].Remove(UpgradeMask::Of(upgrade));
	}
	std::uint32_t Grants(std::uint32_t player) const noexcept { return player < m_grants.size() ? m_grants[player] : 0; }

	// Player::addUpgrade(COMPLETE): the bit, and its objects look again.
	void Grant(std::uint32_t player, std::uint32_t upgrade)
	{
		if (player >= m_completed.size())
			Resize(player + 1);
		m_completed[player].Set(upgrade);
		m_inProduction[player].Remove(UpgradeMask::Of(upgrade));
		++m_grants[player];
	}
	// Player::removeUpgrade: only the bit (nothing is undone).
	void Remove(std::uint32_t player, std::uint32_t upgrade)
	{
		if (player < m_completed.size())
		{
			m_completed[player].Remove(UpgradeMask::Of(upgrade));
			m_inProduction[player].Remove(UpgradeMask::Of(upgrade));
		}
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_completed.size()));
		for (std::size_t player = 0; player < m_completed.size(); ++player)
		{
			for (const std::uint64_t word : m_completed[player].bits)
				writer.U64(word);
			for (const std::uint64_t word : m_inProduction[player].bits)
				writer.U64(word);
			writer.U32(m_grants[player]);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count)
			return false;
		std::vector<UpgradeMask> completed(*count);
		std::vector<UpgradeMask> inProduction(*count);
		std::vector<std::uint32_t> grants(*count, 0);
		for (std::uint32_t player = 0; player < *count; ++player)
		{
			for (UpgradeMask *mask : {&completed[player], &inProduction[player]})
				for (std::uint64_t &word : mask->bits)
				{
					const auto value = reader.U64();
					if (!value)
						return false;
					word = *value;
				}
			const auto granted = reader.U32();
			if (!granted)
				return false;
			grants[player] = *granted;
		}
		m_completed = std::move(completed);
		m_inProduction = std::move(inProduction);
		m_grants = std::move(grants);
		return true;
	}

private:
	std::vector<UpgradeMask> m_completed;
	std::vector<UpgradeMask> m_inProduction;
	std::vector<std::uint32_t> m_grants;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerUpgrades>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_upgrades";
};
}
