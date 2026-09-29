export module engine.gameplay.rts.economy.resources.player_energy;
import std;

export import Engine.Core.Math.Fixed;
export import engine.ecs.system.chunk_outputs;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's power this tick, tallied from what lives: what it produces
// and consumes, and the supply ratio as the original reports it (production
// over consumption; with nothing consumed, the production itself, so a player
// with no power at all has a ratio of 0). Scripts read the last tally between
// ticks, so it is checkpointed too.
export namespace engine::gameplay
{
struct EnergyShare
{
	std::uint32_t player{0};
	std::int32_t amount{0};
};

struct EnergyShares : ecs::ChunkOutputs<EnergyShare>
{
};

class PlayerEnergy
{
public:
	void Clear()
	{
		std::fill(m_production.begin(), m_production.end(), 0);
		std::fill(m_consumption.begin(), m_consumption.end(), 0);
	}

	void Add(std::uint32_t player, std::int32_t amount)
	{
		if (player >= m_production.size())
		{
			m_production.resize(player + 1, 0);
			m_consumption.resize(player + 1, 0);
		}
		if (amount > 0)
			m_production[player] += amount;
		else
			m_consumption[player] -= amount;
	}

	std::int64_t Production(std::uint32_t player) const noexcept { return player < m_production.size() ? m_production[player] : 0; }
	std::int64_t Consumption(std::uint32_t player) const noexcept { return player < m_consumption.size() ? m_consumption[player] : 0; }
	bool Sufficient(std::uint32_t player) const noexcept { return Production(player) >= Consumption(player); }

	Engine::Math::Fixed SupplyRatio(std::uint32_t player) const noexcept
	{
		const std::int64_t consumed = Consumption(player);
		if (consumed == 0)
			return Engine::Math::Fixed::FromInt(Production(player));
		return Engine::Math::Fixed::FromRatio(Production(player), consumed);
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_production.size()));
		for (std::size_t player = 0; player < m_production.size(); ++player)
		{
			writer.I64(m_production[player]);
			writer.I64(m_consumption[player]);
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto players = reader.U32();
		if (!players || *players > 4096)
			return false;
		std::vector<std::int64_t> production(*players), consumption(*players);
		for (std::uint32_t player = 0; player < *players; ++player)
		{
			const auto made = reader.I64(), used = reader.I64();
			if (!made || !used)
				return false;
			production[player] = *made;
			consumption[player] = *used;
		}
		m_production = std::move(production);
		m_consumption = std::move(consumption);
		return true;
	}

private:
	std::vector<std::int64_t> m_production;
	std::vector<std::int64_t> m_consumption;
};

// How short power slows production (the original's LowEnergyPenaltyModifier,
// MinLowEnergyProductionSpeed and MaxLowEnergyProductionSpeed).
struct EnergySettings
{
	Engine::Math::Fixed penaltyModifier{Engine::Math::Fixed::One()};
	Engine::Math::Fixed minSpeed{Engine::Math::Fixed::FromRatio(1, 2)};
	Engine::Math::Fixed maxSpeed{Engine::Math::Fixed::FromRatio(4, 5)};

	// The share of full speed a player's production runs at.
	Engine::Math::Fixed ProductionSpeed(Engine::Math::Fixed supplyRatio) const noexcept
	{
		using Engine::Math::Fixed;
		const Fixed supplied = (std::min)(supplyRatio, Fixed::One());
		Fixed speed = (std::max)(Fixed::One() - (Fixed::One() - supplied) * penaltyModifier, minSpeed);
		if (supplied < Fixed::One())
			speed = (std::min)(speed, maxSpeed);
		return speed > Fixed{} ? speed : Fixed::FromRatio(1, 100);
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::EnergyShares>
{
	static constexpr std::string_view StableName = "engine.gameplay.energy_shares";
};
template<>
struct ResourceTraits<engine::gameplay::PlayerEnergy>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_energy";
};
template<>
struct ResourceTraits<engine::gameplay::EnergySettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.energy_settings";
};
}
