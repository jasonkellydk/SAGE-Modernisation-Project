export module engine.gameplay.rts.powers.resources.shared_power_timers;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's shared special power timers (Player::m_specialPowerReadyTimerList: one ready tick per power with a
// SharedSyncedTimer, so all its command centres fire the general's power as one), in the order they were started.
// Simulation state: checkpointed and hashed.
export namespace engine::gameplay
{
struct SharedPowerTimer
{
	std::uint32_t player{0};
	std::uint32_t power{0};
	std::uint64_t readyOn{0};
};

struct SharedPowerTimers
{
	std::vector<SharedPowerTimer> timers;
	// The next countdown put on screen's place (InGameUI's lists keep the order they were added in).
	std::uint32_t nextPublicOrder{1};

	SharedPowerTimer *Find(std::uint32_t player, std::uint32_t power) noexcept
	{
		for (SharedPowerTimer &timer : timers)
			if (timer.player == player && timer.power == power)
				return &timer;
		return nullptr;
	}

	// getOrStartSpecialPowerReadyFrame: its tick, or a new timer ready now.
	std::uint64_t GetOrStart(std::uint32_t player, std::uint32_t power, std::uint64_t now)
	{
		if (const SharedPowerTimer *timer = Find(player, power))
			return timer->readyOn;
		timers.push_back({player, power, now});
		return now;
	}

	// resetOrStartSpecialPowerReadyFrame: ready a reload from now, or a new timer ready now.
	void ResetOrStart(std::uint32_t player, std::uint32_t power, std::uint64_t now, std::uint64_t reload)
	{
		if (SharedPowerTimer *timer = Find(player, power))
			timer->readyOn = now + reload;
		else
			timers.push_back({player, power, now});
	}

	// expressSpecialPowerReadyFrame: ready on that tick.
	void Express(std::uint32_t player, std::uint32_t power, std::uint64_t tick)
	{
		if (SharedPowerTimer *timer = Find(player, power))
			timer->readyOn = tick;
		else
			timers.push_back({player, power, tick});
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(nextPublicOrder);
		writer.U32(static_cast<std::uint32_t>(timers.size()));
		for (const SharedPowerTimer &timer : timers)
		{
			writer.U32(timer.player);
			writer.U32(timer.power);
			writer.U64(timer.readyOn);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto order = reader.U32();
		const auto count = reader.U32();
		if (!order || !count || *count > 65536)
			return false;
		nextPublicOrder = *order;
		std::vector<SharedPowerTimer> loaded;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto player = reader.U32(), power = reader.U32();
			const auto ready = reader.U64();
			if (!player || !power || !ready)
				return false;
			loaded.push_back({*player, *power, *ready});
		}
		timers = std::move(loaded);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SharedPowerTimers>
{
	static constexpr std::string_view StableName = "engine.gameplay.shared_power_timers";
};
}
