export module games.generalszh.gameplay.ai.resources.retaliation_modes;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Player::m_logicalRetaliationModeEnabled for every player (a bit each): off until the player's own client says
// otherwise (MSG_ENABLE_RETALIATION_MODE, from its Retaliation option). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct RetaliationModes
{
	std::uint64_t enabled{0};

	bool On(std::uint32_t player) const noexcept { return player < 64 && (enabled >> player & 1u) != 0; }
	void Set(std::uint32_t player, bool on) noexcept
	{
		if (player >= 64)
			return;
		enabled = on ? enabled | std::uint64_t{1} << player : enabled & ~(std::uint64_t{1} << player);
	}

	void Save(engine::core::serialization::ByteWriter &writer) const { writer.U64(enabled); }
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto bits = reader.U64();
		if (!bits)
			return false;
		enabled = *bits;
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::RetaliationModes>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.retaliation_modes";
};
}
