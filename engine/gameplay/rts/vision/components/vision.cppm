export module engine.gameplay.rts.vision.components.vision;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// What an object sees of the shroud (the original's Object::look): its shroud clearing range (the template's
// ShroudClearingRange, changed by upgrades and powers; under construction, only its footprint's bounding circle), the
// template's ShroudRevealToAllRange (a reveal to its enemies and neutrals), whether it shows itself to everyone
// (KINDOF_REVEAL_TO_ALL), the players looking through its eyes (m_visionSpiedMask), and its slot of standing looks in
// the ShroudMap (none until it first looks). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct Vision
{
	static constexpr std::uint32_t NoSlot = 0xFFFFFFFFu;

	Engine::Math::Fixed clearingRange;
	Engine::Math::Fixed revealToAllRange;
	Engine::Math::Fixed footprintRange; // its geometry's bounding circle radius
	std::uint64_t spiedMask{0};
	std::uint32_t slot{NoSlot};
	std::uint8_t revealToAll{0};
	std::uint8_t reserved[3]{};
};

// How many times each player looks through an object's eyes (Object::m_visionSpiedBy: a spy vision's reference count
// per spying player index); its Vision's spiedMask has a player's bit while that count is above 0.
struct VisionSpies
{
	static constexpr std::size_t Players = 16; // MAX_PLAYER_COUNT
	std::array<std::int32_t, Players> by{};
};

// Object::setVisionSpied: one more (or fewer) spy through its eyes for `byWhom`; true when that player's bit changed
// (the look is to be made again: handlePartitionCellMaintenance).
inline bool SetVisionSpied(VisionSpies &spies, Vision &vision, bool setting, std::uint32_t byWhom) noexcept
{
	if (byWhom >= VisionSpies::Players)
		return false;
	std::int32_t &count = spies.by[byWhom];
	count += setting ? 1 : -1;
	if (count != (setting ? 1 : 0))
		return false;
	std::uint64_t mask = 0;
	for (std::size_t player = 0; player < VisionSpies::Players; ++player)
		if (spies.by[player] > 0)
			mask |= std::uint64_t{1} << player;
	vision.spiedMask = mask;
	return true;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Vision>
{
	static constexpr std::string_view StableName = "engine.gameplay.vision";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::VisionSpies>
{
	static constexpr std::string_view StableName = "engine.gameplay.vision_spies";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::VisionSpies &value, StateHasher &hasher) noexcept
	{
		for (const std::int32_t count : value.by)
			hasher.AppendU64(static_cast<std::uint32_t>(count));
	}
};
}
