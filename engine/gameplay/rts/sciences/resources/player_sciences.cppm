export module engine.gameplay.rts.sciences.resources.player_sciences;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's sciences (the original's Player::m_sciences: its side's
// intrinsic sciences, and those bought with general's points or granted by
// scripts), as bits the game numbers (Science.ini's order); and those a script
// made unavailable (Player::setScienceAvailability: disabled or hidden, never
// both). Simulation state: checkpointed and hashed with the session's resources.
export namespace engine::gameplay
{
// ScienceAvailabilityNames: Available, Disabled, Hidden.
enum class ScienceAvailability : std::uint8_t
{
	Available,
	Disabled,
	Hidden,
};

class PlayerSciences
{
public:
	static constexpr std::size_t Capacity = 256;
	using Bits = std::array<std::uint64_t, Capacity / 64>;

	void Resize(std::size_t players)
	{
		m_known.resize(players);
		m_disabled.resize(players);
		m_hidden.resize(players);
	}

	bool Has(std::uint32_t player, std::uint32_t science) const noexcept { return Test(m_known, player, science); }
	// isScienceDisabled / isScienceHidden.
	bool Disabled(std::uint32_t player, std::uint32_t science) const noexcept { return Test(m_disabled, player, science); }
	bool Hidden(std::uint32_t player, std::uint32_t science) const noexcept { return Test(m_hidden, player, science); }

	// setScienceAvailability: no longer disabled or hidden, then as asked.
	void SetAvailability(std::uint32_t player, std::uint32_t science, ScienceAvailability availability)
	{
		if (science >= Capacity)
			return;
		if (player >= m_disabled.size())
		{
			m_disabled.resize(player + 1);
			m_hidden.resize(player + 1);
		}
		const std::uint64_t bit = std::uint64_t{1} << (science % 64);
		m_disabled[player][science / 64] &= ~bit;
		m_hidden[player][science / 64] &= ~bit;
		if (availability == ScienceAvailability::Disabled)
			m_disabled[player][science / 64] |= bit;
		else if (availability == ScienceAvailability::Hidden)
			m_hidden[player][science / 64] |= bit;
	}

	void Grant(std::uint32_t player, std::uint32_t science)
	{
		if (science >= Capacity)
			return;
		if (player >= m_known.size())
			m_known.resize(player + 1);
		m_known[player][science / 64] |= std::uint64_t{1} << (science % 64);
	}

	// Forgets everything the player knew.
	void Clear(std::uint32_t player)
	{
		if (player < m_known.size())
			m_known[player] = {};
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		SaveSet(writer, m_known);
		SaveSet(writer, m_disabled);
		SaveSet(writer, m_hidden);
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		std::vector<Bits> known, disabled, hidden;
		if (!LoadSet(reader, known) || !LoadSet(reader, disabled) || !LoadSet(reader, hidden))
			return false;
		m_known = std::move(known);
		m_disabled = std::move(disabled);
		m_hidden = std::move(hidden);
		return true;
	}

private:
	static bool Test(const std::vector<Bits> &set, std::uint32_t player, std::uint32_t science) noexcept
	{
		return player < set.size() && science < Capacity && ((set[player][science / 64] >> (science % 64)) & 1u) != 0;
	}
	static void SaveSet(engine::core::serialization::ByteWriter &writer, const std::vector<Bits> &set)
	{
		writer.U32(static_cast<std::uint32_t>(set.size()));
		for (const auto &words : set)
			for (const std::uint64_t word : words)
				writer.U64(word);
	}
	static bool LoadSet(engine::core::serialization::ByteReader &reader, std::vector<Bits> &set)
	{
		const auto count = reader.U32();
		if (!count || *count > 4096)
			return false;
		set.assign(*count, Bits{});
		for (auto &words : set)
			for (std::uint64_t &word : words)
			{
				const auto value = reader.U64();
				if (!value)
					return false;
				word = *value;
			}
		return true;
	}

	std::vector<Bits> m_known;
	std::vector<Bits> m_disabled;
	std::vector<Bits> m_hidden;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerSciences>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_sciences";
};
}
